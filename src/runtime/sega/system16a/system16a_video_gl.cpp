#include "boards/sega/system16a/video.hpp"

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

std::string read_shader_source() {
    std::ifstream input(MAMERECOMP_SYSTEM16A_SHADER_PATH, std::ios::binary);
    if (!input) throw std::runtime_error("could not open System 16A compute shader: " MAMERECOMP_SYSTEM16A_SHADER_PATH);
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
        if (program_) glDeleteProgram(program_);
        if (textures_[0] || textures_[1] || textures_[2]) glDeleteTextures(3, textures_);
        if (output_buffers_[0] || output_buffers_[1] || output_buffers_[2] || output_buffers_[3])
            glDeleteBuffers(4, output_buffers_);
        if (context_) SDL_GL_DestroyContext(context_);
        if (window_) SDL_DestroyWindow(window_);
        if (sdl_initialized_) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    System16AFrame render(std::span<const std::uint8_t> decoded_tiles,
        std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
        System16ARenderOptions options, System16AVideoProfile* profile) {
        if (decoded_tiles.empty() || decoded_tiles.size() % 64 != 0
            || decoded_tiles.size() / 64 > 128 * 256 || tile_ram.size() < 0x8000 || text_ram.size() < 0x1000) {
            throw std::invalid_argument("invalid System 16A data for OpenGL tilemap rendering");
        }

        const auto setup_start = Clock::now();
        const bool was_initialized = initialized_;
        initialize();
        // SDL_RenderPresent on the visible game window can leave its own
        // context current. Rebind the hidden compute context before touching
        // any GL object; otherwise the next frame dispatches against the wrong
        // context, where program/buffer names are invalid or unallocated.
        require_sdl(SDL_GL_MakeCurrent(window_, context_), "OpenGL context activation failed");
        initialize_system16a_program();
        if (profile && !was_initialized)
            profile->gpu_setup_ns += elapsed_ns(setup_start);

        const auto upload_start = Clock::now();
        upload_atlas_if_needed(decoded_tiles);
        upload_ram(textures_[1], 256, 128, tile_ram.data());
        upload_ram(textures_[2], 256, 16, text_ram.data());
        if (profile) profile->gpu_upload_ns += elapsed_ns(upload_start);

        System16AFrame frame;
        const auto pixel_count = static_cast<std::size_t>(System16AFrame::width * System16AFrame::height);
        frame.palette_indices.resize(pixel_count);
        frame.tile_priorities.resize(pixel_count);
        frame.shadow_pixels.assign(pixel_count, 0);

        const auto dispatch_start = Clock::now();
        glUseProgram(program_);
        for (unsigned unit = 0; unit < 3; ++unit) {
            glActiveTexture(GL_TEXTURE0 + unit);
            glBindTexture(GL_TEXTURE_2D, textures_[unit]);
        }
        glUniform1i(glGetUniformLocation(program_, "uDisplayEnable"), options.display_enable ? 1 : 0);
        glUniform1i(glGetUniformLocation(program_, "uFlip"), options.flip ? 1 : 0);
        glUniform1i(glGetUniformLocation(program_, "uRowScrollEnable"), options.row_scroll_enabled ? 1 : 0);
        glUniform1i(glGetUniformLocation(program_, "uColumnScrollEnable"), options.column_scroll_enabled ? 1 : 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, output_buffers_[0]);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, output_buffers_[1]);
        glDispatchCompute(40, 28, 1);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_BUFFER_UPDATE_BARRIER_BIT);

        std::vector<std::uint32_t> palette_indices(pixel_count);
        std::vector<std::uint32_t> priorities(pixel_count);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, output_buffers_[0]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
            static_cast<GLsizeiptr>(palette_indices.size() * sizeof(std::uint32_t)), palette_indices.data());
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, output_buffers_[1]);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
            static_cast<GLsizeiptr>(priorities.size() * sizeof(std::uint32_t)), priorities.data());
        for (std::size_t pixel = 0; pixel < pixel_count; ++pixel) {
            frame.palette_indices[pixel] = static_cast<std::uint16_t>(palette_indices[pixel]);
            frame.tile_priorities[pixel] = static_cast<std::uint8_t>(priorities[pixel]);
        }
        if (profile) profile->gpu_dispatch_readback_ns += elapsed_ns(dispatch_start);
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR)
            throw std::runtime_error("OpenGL tilemap renderer failed with error " + std::to_string(error));
        return frame;
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

    void initialize_system16a_program() {
        if (program_) return;
        const GLuint shader = compile_compute_shader(read_shader_source());
        program_ = glCreateProgram();
        glAttachShader(program_, shader);
        glLinkProgram(program_);
        glDeleteShader(shader);
        GLint linked = GL_FALSE;
        glGetProgramiv(program_, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            GLint log_size = 0;
            glGetProgramiv(program_, GL_INFO_LOG_LENGTH, &log_size);
            std::string log(static_cast<std::size_t>(std::max(log_size, 1)), '\0');
            glGetProgramInfoLog(program_, log_size, nullptr, log.data());
            throw std::runtime_error("System 16A compute shader link failed: " + log);
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
    GLuint program_ = 0;
    GLuint textures_[3]{};
    GLuint output_buffers_[4]{};
    std::uint64_t atlas_hash_ = 0;
    std::size_t atlas_source_size_ = 0;
    bool sdl_initialized_ = false;
    bool initialized_ = false;
    bool atlas_ready_ = false;
};

} // namespace

bool system16a_opengl_backend_available() { return true; }

System16AFrame render_system16a_tilemaps_opengl(std::span<const std::uint8_t> decoded_tiles,
    std::span<const std::uint8_t> tile_ram, std::span<const std::uint8_t> text_ram,
    System16ARenderOptions options, System16AVideoProfile* profile) {
    return OpenGLTilemapRenderer::instance().render(decoded_tiles, tile_ram, text_ram, options, profile);
}

} // namespace mamerecomp
