#include "boards/sega/system16b/video.hpp"

#define GL_GLEXT_PROTOTYPES 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>


namespace mamerecomp {
namespace {

using Clock = std::chrono::steady_clock;


std::string read_system16b_shader_source() {
    std::ifstream input(MAMERECOMP_SYSTEM16B_SHADER_PATH, std::ios::binary);
    if (!input) throw std::runtime_error("could not open System 16B compute shader: " MAMERECOMP_SYSTEM16B_SHADER_PATH);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

std::string read_system16b_sprite_mix_shader_source() {
    std::ifstream input(MAMERECOMP_SYSTEM16B_SPRITE_MIX_SHADER_PATH, std::ios::binary);
    if (!input) throw std::runtime_error("could not open System 16B sprite mixer shader: " MAMERECOMP_SYSTEM16B_SPRITE_MIX_SHADER_PATH);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

GLuint compile_compute_shader(const std::string& source) {
    const GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const GLchar* source_pointer = source.c_str();
    glShaderSource(shader, 1, &source_pointer, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return shader;
    GLint log_size = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_size);
    std::string log(static_cast<std::size_t>(std::max(log_size, 1)), '\0');
    glGetShaderInfoLog(shader, log_size, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("compute shader compilation failed: " + log);
}

void set_texture_parameters(GLuint texture) {
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

std::uint64_t hash_bytes(std::span<const std::uint8_t> bytes) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const auto byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    }
    return hash;
}

class OpenGLTilemapRenderer {
public:
    ~OpenGLTilemapRenderer() {
        if (context_) SDL_GL_MakeCurrent(window_, context_);
        if (program_b_) glDeleteProgram(program_b_);
        if (program_b_sprite_mix_) glDeleteProgram(program_b_sprite_mix_);
        if (textures_[0] || textures_[1] || textures_[2]) glDeleteTextures(3, textures_);
        if (output_buffers_[0] || output_buffers_[1] || output_buffers_[2] || output_buffers_[3])
            glDeleteBuffers(4, output_buffers_);
        if (context_) SDL_GL_DestroyContext(context_);
        if (window_) SDL_DestroyWindow(window_);
        if (sdl_initialized_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }


    System16BFrame render_system16b(std::span<const std::uint8_t> decoded_tiles,
        std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
        chips::sega3155197::TilemapState state, std::span<const std::uint16_t> sprite_pixels = {}) {
        if (decoded_tiles.empty() || decoded_tiles.size() % 64 != 0 || decoded_tiles.size() / 64 > 128 * 256
            || tile_ram.size() < 0x10000 || text_ram.size() < 0x1000)
            throw std::invalid_argument("invalid System 16B data for the 315-5197 OpenGL renderer");
        initialize();
        require_sdl(SDL_GL_MakeCurrent(window_, context_), "OpenGL context activation failed");
        initialize_system16b_program();
        upload_atlas_if_needed(decoded_tiles);
        upload_ram(textures_[1], 256, 256, tile_ram.data());
        upload_ram(textures_[2], 256, 16, text_ram.data());

        System16BFrame frame;
        const auto pixel_count = static_cast<std::size_t>(System16BFrame::width * System16BFrame::height);
        frame.palette_indices.resize(pixel_count);
        frame.tile_priorities.resize(pixel_count);
        frame.shadow_pixels.assign(pixel_count, 0);
        glUseProgram(program_b_);
        for (unsigned unit = 0; unit < 3; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, textures_[unit]);
        }
        glUniform1i(glGetUniformLocation(program_b_, "uDisplayEnable"), state.display_enable ? 1 : 0);
        glUniform1i(glGetUniformLocation(program_b_, "uFlip"), state.flip ? 1 : 0);
        glUniform1i(glGetUniformLocation(program_b_, "uRegistersLatched"), state.registers_latched ? 1 : 0);
        glUniform1ui(glGetUniformLocation(program_b_, "uBankSize"), state.bank_size);
        std::array<GLuint, 8> banks{};
        std::transform(state.banks.begin(), state.banks.end(), banks.begin(),
            [](std::uint8_t value) { return static_cast<GLuint>(value); });
        glUniform1uiv(glGetUniformLocation(program_b_, "uBanks"),
            static_cast<GLsizei>(banks.size()), banks.data());
        std::array<GLuint, 4> latched_pages{}, latched_yscroll{}, latched_xscroll{};
        std::transform(state.latched_pages.begin(), state.latched_pages.end(), latched_pages.begin(),
            [](std::uint16_t value) { return static_cast<GLuint>(value); });
        std::transform(state.latched_yscroll.begin(), state.latched_yscroll.end(), latched_yscroll.begin(),
            [](std::uint16_t value) { return static_cast<GLuint>(value); });
        std::transform(state.latched_xscroll.begin(), state.latched_xscroll.end(), latched_xscroll.begin(),
            [](std::uint16_t value) { return static_cast<GLuint>(value); });
        glUniform1uiv(glGetUniformLocation(program_b_, "uLatchedPages"),
            static_cast<GLsizei>(latched_pages.size()), latched_pages.data());
        glUniform1uiv(glGetUniformLocation(program_b_, "uLatchedYscroll"),
            static_cast<GLsizei>(latched_yscroll.size()), latched_yscroll.data());
        glUniform1uiv(glGetUniformLocation(program_b_, "uLatchedXscroll"),
            static_cast<GLsizei>(latched_xscroll.size()), latched_xscroll.data());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, output_buffers_[0]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, output_buffers_[1]);
        glDispatchCompute(40, 28, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

        const bool mix_sprites = sprite_pixels.size() == pixel_count;
        if (mix_sprites) {
            const auto buffer_bytes = static_cast<GLsizeiptr>(pixel_count * sizeof(std::uint32_t));
            std::vector<std::uint32_t> sprites(pixel_count);
            std::transform(sprite_pixels.begin(), sprite_pixels.end(), sprites.begin(),
                [](std::uint16_t value) { return static_cast<std::uint32_t>(value); });
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, output_buffers_[2]);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, buffer_bytes, sprites.data());
            initialize_system16b_sprite_mix_program();
            glUseProgram(program_b_sprite_mix_);
            for (unsigned binding = 0; binding < 4; ++binding)
                glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, output_buffers_[binding]);
            glDispatchCompute(40, 28, 1);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);
        }

        std::vector<std::uint32_t> palette(pixel_count), priority(pixel_count), shadows;
        if (mix_sprites) shadows.resize(pixel_count);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, output_buffers_[0]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
            static_cast<GLsizeiptr>(palette.size() * sizeof(std::uint32_t)), palette.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, output_buffers_[1]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
            static_cast<GLsizeiptr>(priority.size() * sizeof(std::uint32_t)), priority.data());
        if (mix_sprites) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, output_buffers_[3]);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                static_cast<GLsizeiptr>(shadows.size() * sizeof(std::uint32_t)), shadows.data());
        }
        for (std::size_t i = 0; i < pixel_count; ++i) {
            frame.palette_indices[i] = static_cast<std::uint16_t>(palette[i]);
            frame.tile_priorities[i] = static_cast<std::uint8_t>(priority[i]);
            if (mix_sprites) frame.shadow_pixels[i] = static_cast<std::uint8_t>(shadows[i]);
        }
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR)
            throw std::runtime_error("System 16B OpenGL tilemap renderer failed with error " + std::to_string(error));
        return frame;
    }

    void mix_system16b_sprites(System16BFrame& frame, std::span<const std::uint16_t> sprite_pixels) {
        const auto pixel_count = static_cast<std::size_t>(System16BFrame::width * System16BFrame::height);
        if (frame.palette_indices.size() != pixel_count || frame.tile_priorities.size() != pixel_count
            || frame.shadow_pixels.size() != pixel_count || sprite_pixels.size() != pixel_count)
            throw std::invalid_argument("System 16B OpenGL sprite mixing requires complete tile and sprite planes");
        initialize();
        require_sdl(SDL_GL_MakeCurrent(window_, context_), "OpenGL context activation failed");
        initialize_system16b_sprite_mix_program();

        std::vector<std::uint32_t> palette(pixel_count), priorities(pixel_count), sprites(pixel_count);
        for (std::size_t i = 0; i < pixel_count; ++i) {
            palette[i] = frame.palette_indices[i];
            priorities[i] = frame.tile_priorities[i];
            sprites[i] = sprite_pixels[i];
        }
        const auto buffer_bytes = static_cast<GLsizeiptr>(pixel_count * sizeof(std::uint32_t));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, output_buffers_[0]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, buffer_bytes, palette.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, output_buffers_[1]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, buffer_bytes, priorities.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, output_buffers_[2]);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, buffer_bytes, sprites.data());

        glUseProgram(program_b_sprite_mix_);
        for (unsigned binding = 0; binding < 4; ++binding)
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, output_buffers_[binding]);
        glDispatchCompute(40, 28, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

        std::vector<std::uint32_t> mixed_palette(pixel_count), shadows(pixel_count);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, output_buffers_[0]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, buffer_bytes, mixed_palette.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, output_buffers_[3]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, buffer_bytes, shadows.data());
        for (std::size_t i = 0; i < pixel_count; ++i) {
            frame.palette_indices[i] = static_cast<std::uint16_t>(mixed_palette[i]);
            frame.shadow_pixels[i] = static_cast<std::uint8_t>(shadows[i]);
        }
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR)
            throw std::runtime_error("System 16B OpenGL sprite mixer failed with error " + std::to_string(error));
    }

    static OpenGLTilemapRenderer& instance() {
        static OpenGLTilemapRenderer renderer;
        return renderer;
    }

private:
    static std::uint64_t elapsed_ns(Clock::time_point start) {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count());
    }

    static void require_sdl(bool ok, const char* action) {
        if (!ok) throw std::runtime_error(std::string(action) + ": " + SDL_GetError());
    }

    void initialize() {
        if (initialized_) return;
        require_sdl(SDL_Init(SDL_INIT_VIDEO), "SDL video initialization failed");
        sdl_initialized_ = true;
        require_sdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4), "OpenGL major version setup failed");
        require_sdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3), "OpenGL minor version setup failed");
        require_sdl(SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE), "OpenGL profile setup failed");
        window_ = SDL_CreateWindow("MameRecomp OpenGL tilemap backend", 64, 64, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!window_) throw std::runtime_error(std::string("OpenGL window creation failed: ") + SDL_GetError());
        context_ = SDL_GL_CreateContext(window_);
        if (!context_) throw std::runtime_error(std::string("OpenGL context creation failed: ") + SDL_GetError());
        require_sdl(SDL_GL_MakeCurrent(window_, context_), "OpenGL context activation failed");
        GLint major = 0;
        GLint minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major < 4 || (major == 4 && minor < 3))
            throw std::runtime_error("OpenGL 4.3 or newer is required for the compute-shader backend");
        std::cout << "OpenGL video device: " << glGetString(GL_RENDERER)
                  << " (" << glGetString(GL_VERSION) << ")\n";

        for (auto& texture : textures_) {
            glGenTextures(1, &texture);
            set_texture_parameters(texture);
        }
        glGenBuffers(4, output_buffers_);
        const auto output_bytes = static_cast<GLsizeiptr>(SegaVideoFrame::width * SegaVideoFrame::height * sizeof(std::uint32_t));
        for (const auto buffer : output_buffers_) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER, output_bytes, nullptr, GL_DYNAMIC_READ);
        }
        initialized_ = true;
    }

    void initialize_system16b_program() {
        if (program_b_) return;
        const GLuint shader = compile_compute_shader(read_system16b_shader_source());
        program_b_ = glCreateProgram();
        glAttachShader(program_b_, shader);
        glLinkProgram(program_b_);
        glDeleteShader(shader);
        GLint linked = GL_FALSE;
        glGetProgramiv(program_b_, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            GLint log_size = 0;
            glGetProgramiv(program_b_, GL_INFO_LOG_LENGTH, &log_size);
            std::string log(static_cast<std::size_t>(std::max(log_size, 1)), '\0');
            glGetProgramInfoLog(program_b_, log_size, nullptr, log.data());
            glDeleteProgram(program_b_);
            program_b_ = 0;
            throw std::runtime_error("System 16B compute shader link failed: " + log);
        }
    }

    void initialize_system16b_sprite_mix_program() {
        if (program_b_sprite_mix_) return;
        const GLuint shader = compile_compute_shader(read_system16b_sprite_mix_shader_source());
        program_b_sprite_mix_ = glCreateProgram();
        glAttachShader(program_b_sprite_mix_, shader);
        glLinkProgram(program_b_sprite_mix_);
        glDeleteShader(shader);
        GLint linked = GL_FALSE;
        glGetProgramiv(program_b_sprite_mix_, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            GLint log_size = 0;
            glGetProgramiv(program_b_sprite_mix_, GL_INFO_LOG_LENGTH, &log_size);
            std::string log(static_cast<std::size_t>(std::max(log_size, 1)), '\0');
            glGetProgramInfoLog(program_b_sprite_mix_, log_size, nullptr, log.data());
            glDeleteProgram(program_b_sprite_mix_);
            program_b_sprite_mix_ = 0;
            throw std::runtime_error("System 16B sprite mixer shader link failed: " + log);
        }
    }

    void upload_atlas_if_needed(std::span<const std::uint8_t> decoded_tiles) {
        const auto hash = hash_bytes(decoded_tiles);
        if (atlas_ready_ && hash == atlas_hash_ && decoded_tiles.size() == atlas_source_size_) return;
        constexpr std::uint32_t columns = 128;
        const auto tile_count = static_cast<std::uint32_t>(decoded_tiles.size() / 64);
        const auto rows = (tile_count + columns - 1) / columns;
        const auto width = columns * 8;
        const auto height = rows * 8;
        std::vector<std::uint8_t> atlas(static_cast<std::size_t>(width) * height, 0);
        for (std::uint32_t tile = 0; tile < tile_count; ++tile) {
            const auto tile_x = (tile % columns) * 8;
            const auto tile_y = (tile / columns) * 8;
            for (std::uint32_t y = 0; y < 8; ++y) {
                const auto source = decoded_tiles.begin() + static_cast<std::ptrdiff_t>(tile * 64 + y * 8);
                const auto destination = atlas.begin() + static_cast<std::ptrdiff_t>((tile_y + y) * width + tile_x);
                std::copy_n(source, 8, destination);
            }
        }
        glBindTexture(GL_TEXTURE_2D, textures_[0]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, static_cast<GLsizei>(width), static_cast<GLsizei>(height),
            0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, atlas.data());
        atlas_hash_ = hash;
        atlas_source_size_ = decoded_tiles.size();
        atlas_ready_ = true;
    }

    static void upload_ram(GLuint texture, GLsizei width, GLsizei height, const std::uint8_t* bytes) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, width, height, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, bytes);
    }

    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    GLuint program_b_ = 0;
    GLuint program_b_sprite_mix_ = 0;
    GLuint textures_[3]{};
    GLuint output_buffers_[4]{};
    std::uint64_t atlas_hash_ = 0;
    std::size_t atlas_source_size_ = 0;
    bool sdl_initialized_ = false;
    bool initialized_ = false;
    bool atlas_ready_ = false;
};

} // namespace


System16BFrame render_system16b_tilemaps_opengl(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    chips::sega3155197::TilemapState state) {
    std::vector<std::uint8_t> latched_text;
    if (state.registers_latched) {
        latched_text.assign(text_ram.begin(), text_ram.end());
        const auto write_word = [&](std::size_t offset, std::uint16_t value) {
            if (offset + 1 >= latched_text.size()) return;
            latched_text[offset] = static_cast<std::uint8_t>(value >> 8);
            latched_text[offset + 1] = static_cast<std::uint8_t>(value);
        };
        for (unsigned i = 0; i < 4; ++i) {
            write_word(0xe80U + i * 2U, state.latched_pages[i]);
            write_word(0xe90U + i * 2U, state.latched_yscroll[i]);
            write_word(0xe98U + i * 2U, state.latched_xscroll[i]);
        }
    }
    return OpenGLTilemapRenderer::instance().render_system16b(decoded_tiles, tile_ram,
        state.registers_latched ? std::span<const std::uint8_t>(latched_text) : text_ram, state);
}

System16BFrame render_system16b_frame_opengl(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    chips::sega3155197::TilemapState state, std::span<const std::uint16_t> sprite_pixels) {
    std::vector<std::uint8_t> latched_text;
    if (state.registers_latched) {
        latched_text.assign(text_ram.begin(), text_ram.end());
        const auto write_word = [&](std::size_t offset, std::uint16_t value) {
            if (offset + 1 >= latched_text.size()) return;
            latched_text[offset] = static_cast<std::uint8_t>(value >> 8);
            latched_text[offset + 1] = static_cast<std::uint8_t>(value);
        };
        for (unsigned i = 0; i < 4; ++i) {
            write_word(0xe80U + i * 2U, state.latched_pages[i]);
            write_word(0xe90U + i * 2U, state.latched_yscroll[i]);
            write_word(0xe98U + i * 2U, state.latched_xscroll[i]);
        }
    }
    return OpenGLTilemapRenderer::instance().render_system16b(decoded_tiles, tile_ram,
        state.registers_latched ? std::span<const std::uint8_t>(latched_text) : text_ram, state, sprite_pixels);
}

void mix_system16b_sprites_opengl(System16BFrame& frame, std::span<const std::uint16_t> sprite_pixels) {
    OpenGLTilemapRenderer::instance().mix_system16b_sprites(frame, sprite_pixels);
}

} // namespace mamerecomp
