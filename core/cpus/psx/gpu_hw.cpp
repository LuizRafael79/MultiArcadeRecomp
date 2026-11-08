#define GL_GLEXT_PROTOTYPES 1
#include "gpu_hw.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <utility>

namespace mamerecomp::psx {
namespace {
constexpr int kVramWidth = 1024;
constexpr int kMinVramHeight = 512;
constexpr int kMaxVramHeight = 1024;

struct alignas(16) ShaderVertex {
    int32_t xyuv[4]{};
    uint32_t color = 0;
    uint32_t padding[3]{};
};
static_assert(sizeof(ShaderVertex) == 32);

struct alignas(16) ShaderCommand {
    uint32_t header[4]{};
    int32_t draw_area[4]{};
    int32_t offset_window[4]{};
    int32_t window_flip[4]{};
    uint32_t texture[4]{};
    ShaderVertex vertices[4]{};
    int32_t bounds[4]{};
};
static_assert(sizeof(ShaderCommand) % 16 == 0);

GLuint compile_compute_program(const std::string& source, std::string& error) {
    const GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    const char* data = source.c_str();
    glShaderSource(shader, 1, &data, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        error = "PSX OpenGL compute shader compilation failed: " + log;
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glDeleteShader(shader);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        error = "PSX OpenGL compute program link failed: " + log;
        return 0;
    }
    return program;
}

GLuint compile_graphics_program(const std::string& vertex_source,
                                const std::string& fragment_source,
                                std::string& error) {
    const auto compile_stage = [&](GLenum type, const std::string& source) -> GLuint {
        const GLuint shader = glCreateShader(type);
        const char* data = source.c_str();
        glShaderSource(shader, 1, &data, nullptr);
        glCompileShader(shader);
        GLint compiled = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (compiled == GL_TRUE) return shader;
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        error = "PSX OpenGL draw shader compilation failed: " + log;
        return 0;
    };
    const GLuint vertex = compile_stage(GL_VERTEX_SHADER, vertex_source);
    if (!vertex) return 0;
    const GLuint fragment = compile_stage(GL_FRAGMENT_SHADER, fragment_source);
    if (!fragment) {
        glDeleteShader(vertex);
        return 0;
    }
    const GLuint program = glCreateProgram();
    glAttachShader(program, vertex);
    glAttachShader(program, fragment);
    glLinkProgram(program);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    GLint linked = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (linked == GL_TRUE) return program;
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    glDeleteProgram(program);
    error = "PSX OpenGL draw program link failed: " + log;
    return 0;
}

std::string read_shader_source(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open PSX raster shader: "
                                         + std::string(path));
    return std::string(std::istreambuf_iterator<char>(input),
                       std::istreambuf_iterator<char>());
}

std::vector<uint16_t> to_words(std::span<const uint8_t> bytes) {
    std::vector<uint16_t> words(bytes.size() / 2);
    for (size_t i = 0; i < words.size(); ++i)
        words[i] = uint16_t(bytes[i * 2] | (uint16_t(bytes[i * 2 + 1]) << 8));
    return words;
}

} // namespace

struct GpuHardwareRenderer::Impl {
    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    bool owns_video = false;
    GLuint program = 0;
    GLuint draw_program = 0;
    GLuint vram[2]{};
    GLuint framebuffer[2]{};
    GLuint vertex_array = 0;
    GLuint command_buffer = 0;
    GLint origin_location = -1;
    GLint vram_height_location = -1;
    GLint draw_vram_height_location = -1;
    int vram_height = kMinVramHeight;
    unsigned current_vram = 0;
    bool vram_textures_diverged = false;
    std::array<int, 4> last_dirty_bounds{};
    std::string renderer;
    std::string init_error;

    ~Impl() {
        make_current();
        if (program) glDeleteProgram(program);
        if (draw_program) glDeleteProgram(draw_program);
        if (vram[0] || vram[1]) glDeleteTextures(2, vram);
        if (framebuffer[0] || framebuffer[1]) glDeleteFramebuffers(2, framebuffer);
        if (vertex_array) glDeleteVertexArrays(1, &vertex_array);
        if (command_buffer) glDeleteBuffers(1, &command_buffer);
        if (context) SDL_GL_DestroyContext(context);
        if (window) SDL_DestroyWindow(window);
        if (owns_video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    bool make_current() const {
        return context && window && SDL_GL_MakeCurrent(window, context);
    }

    bool initialize(std::span<const uint8_t> initial_vram) {
        if (program) return make_current();
        vram_height = std::clamp<int>(
            static_cast<int>(initial_vram.size() / (kVramWidth * sizeof(uint16_t))),
            kMinVramHeight, kMaxVramHeight);
        if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
            if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
                throw std::runtime_error(std::string("SDL video init for PSX GPU failed: ") + SDL_GetError());
            owns_video = true;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        window = SDL_CreateWindow("MameRecomp PSX GPU", 32, 32,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!window) throw std::runtime_error(std::string("hidden PSX OpenGL window failed: ") + SDL_GetError());
        context = SDL_GL_CreateContext(window);
        if (!context) throw std::runtime_error(std::string("PSX OpenGL context creation failed: ") + SDL_GetError());
        if (!make_current()) throw std::runtime_error(std::string("PSX OpenGL context activation failed: ") + SDL_GetError());
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major < 4 || (major == 4 && minor < 3))
            throw std::runtime_error("PSX hardware rasterizer requires OpenGL 4.3");
        if (const auto* name = glGetString(GL_RENDERER)) renderer = reinterpret_cast<const char*>(name);
        program = compile_compute_program(read_shader_source(MAMERECOMP_PSX_GPU_SHADER_PATH), init_error);
        if (!program) throw std::runtime_error(init_error);
        draw_program = compile_graphics_program(
            read_shader_source(MAMERECOMP_PSX_GPU_VERTEX_SHADER_PATH),
            read_shader_source(MAMERECOMP_PSX_GPU_FRAGMENT_SHADER_PATH), init_error);
        if (!draw_program) throw std::runtime_error(init_error);
        origin_location = glGetUniformLocation(program, "uDispatchOrigin");
        vram_height_location = glGetUniformLocation(program, "uVramHeight");
        draw_vram_height_location = glGetUniformLocation(draw_program, "uVramHeight");
        glGenTextures(2, vram);
        const auto words = to_words(initial_vram);
        for (GLuint texture : vram) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, kVramWidth, vram_height, 0,
                         GL_RED_INTEGER, GL_UNSIGNED_SHORT, words.data());
        }
        glGenFramebuffers(2, framebuffer);
        for (unsigned i = 0; i < 2; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, vram[i], 0);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("PSX OpenGL R16UI framebuffer is incomplete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glGenVertexArrays(1, &vertex_array);
        glBindVertexArray(vertex_array);
        glGenBuffers(1, &command_buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, command_buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, sizeof(ShaderCommand), nullptr, GL_STREAM_DRAW);
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "uSource"), 1);
        glUseProgram(draw_program);
        glUniform1i(glGetUniformLocation(draw_program, "uSource"), 0);
        glBindVertexArray(0);
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR) throw std::runtime_error("PSX OpenGL resource creation failed");
        return true;
    }

    void upload_full(std::span<const uint8_t> bytes) {
        if (!make_current()) throw std::runtime_error("could not activate PSX OpenGL context");
        const auto words = to_words(bytes);
        for (GLuint texture : vram) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kVramWidth, vram_height,
                            GL_RED_INTEGER, GL_UNSIGNED_SHORT, words.data());
        }
        current_vram = 0;
        vram_textures_diverged = false;
    }

    void read_full(std::vector<uint8_t>& bytes) {
        if (!make_current()) throw std::runtime_error("could not activate PSX OpenGL context");
        std::vector<uint16_t> words(kVramWidth * vram_height);
        glBindTexture(GL_TEXTURE_2D, vram[current_vram]);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RED_INTEGER, GL_UNSIGNED_SHORT, words.data());
        bytes.resize(std::max(bytes.size(), words.size() * sizeof(uint16_t)));
        for (size_t i = 0; i < words.size(); ++i) {
            bytes[i * 2] = uint8_t(words[i]);
            bytes[i * 2 + 1] = uint8_t(words[i] >> 8);
        }
    }

    unsigned prepare_destination() {
        const unsigned destination_vram = current_vram ^ 1u;
        if (vram_textures_diverged) {
            glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT |
                            GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                            GL_FRAMEBUFFER_BARRIER_BIT);
            const int copy_width = last_dirty_bounds[2] - last_dirty_bounds[0] + 1;
            const int copy_height = last_dirty_bounds[3] - last_dirty_bounds[1] + 1;
            glCopyImageSubData(vram[current_vram], GL_TEXTURE_2D, 0,
                               last_dirty_bounds[0], last_dirty_bounds[1], 0,
                               vram[destination_vram], GL_TEXTURE_2D, 0,
                               last_dirty_bounds[0], last_dirty_bounds[1], 0,
                               copy_width, copy_height, 1);
        }
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT |
                        GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_FRAMEBUFFER_BARRIER_BIT);
        return destination_vram;
    }

    void finish_write(unsigned destination_vram, int min_x, int min_y,
                      int max_x, int max_y) {
        current_vram = destination_vram;
        vram_textures_diverged = true;
        last_dirty_bounds = {min_x, min_y, max_x, max_y};
    }

    void execute(ShaderCommand command) {
        if (!make_current()) throw std::runtime_error("could not activate PSX OpenGL context");
        const int min_x = std::clamp(command.bounds[0], 0, kVramWidth - 1);
        const int min_y = std::clamp(command.bounds[1], 0, vram_height - 1);
        const int max_x = std::clamp(command.bounds[2], 0, kVramWidth - 1);
        const int max_y = std::clamp(command.bounds[3], 0, vram_height - 1);
        if (min_x > max_x || min_y > max_y) return;
        command.bounds[0] = min_x;
        command.bounds[1] = min_y;
        command.bounds[2] = max_x;
        command.bounds[3] = max_y;
        const unsigned destination_vram = prepare_destination();
        glUseProgram(program);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, command_buffer);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(command), &command);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, command_buffer);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, vram[current_vram]);
        glBindImageTexture(0, vram[destination_vram], 0, GL_FALSE, 0,
                           GL_READ_WRITE, GL_R16UI);
        glUniform2i(origin_location, min_x, min_y);
        glUniform1i(vram_height_location, vram_height);
        glDispatchCompute((max_x - min_x + 1 + 7) / 8,
                          (max_y - min_y + 1 + 7) / 8, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                        GL_TEXTURE_UPDATE_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
        finish_write(destination_vram, min_x, min_y, max_x, max_y);
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR) throw std::runtime_error("PSX OpenGL raster dispatch failed");
    }

    void execute_draw(ShaderCommand command) {
        if (!make_current()) throw std::runtime_error("could not activate PSX OpenGL context");
        const int min_x = std::clamp(command.bounds[0], 0, kVramWidth - 1);
        const int min_y = std::clamp(command.bounds[1], 0, vram_height - 1);
        const int max_x = std::clamp(command.bounds[2], 0, kVramWidth - 1);
        const int max_y = std::clamp(command.bounds[3], 0, vram_height - 1);
        if (min_x > max_x || min_y > max_y) return;
        const int clip_left = std::clamp(command.draw_area[0], 0, kVramWidth - 1);
        const int clip_top = std::clamp(command.draw_area[1], 0, vram_height - 1);
        const int clip_right = std::clamp(command.draw_area[2], 0, kVramWidth - 1);
        const int clip_bottom = std::clamp(command.draw_area[3], 0, vram_height - 1);
        if (clip_left > clip_right || clip_top > clip_bottom) return;

        const unsigned destination_vram = prepare_destination();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer[destination_vram]);
        glViewport(0, 0, kVramWidth, vram_height);
        glEnable(GL_SCISSOR_TEST);
        glScissor(clip_left, clip_top,
                  clip_right - clip_left + 1, clip_bottom - clip_top + 1);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_DITHER);
        glUseProgram(draw_program);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, command_buffer);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(command), &command);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, command_buffer);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, vram[current_vram]);
        glBindVertexArray(vertex_array);
        glUniform1i(draw_vram_height_location, vram_height);
        glDrawArrays(GL_TRIANGLES, 0, command.header[1] == 4u ? 6 : 3);
        glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                        GL_TEXTURE_UPDATE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        finish_write(destination_vram, min_x, min_y, max_x, max_y);
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR) throw std::runtime_error("PSX OpenGL triangle raster failed");
    }
};

namespace {
ShaderCommand make_command(uint32_t kind, std::span<const GpuHwVertex> vertices,
                           const GpuHwState& state, bool gouraud,
                           bool textured, bool raw_texture,
                           bool semi_transparent, uint32_t clut) {
    ShaderCommand command{};
    command.header[0] = kind;
    command.header[1] = static_cast<uint32_t>(vertices.size());
    command.header[2] = (gouraud ? 1u : 0u) | (textured ? 2u : 0u) |
                        (raw_texture ? 4u : 0u) | (semi_transparent ? 8u : 0u) |
                        (state.set_mask ? 16u : 0u) | (state.check_mask ? 32u : 0u);
    command.draw_area[0] = state.draw_x1;
    command.draw_area[1] = state.draw_y1;
    command.draw_area[2] = state.draw_x2;
    command.draw_area[3] = state.draw_y2;
    command.offset_window[0] = state.offset_x;
    command.offset_window[1] = state.offset_y;
    command.offset_window[2] = state.window_x;
    command.offset_window[3] = state.window_y;
    command.window_flip[0] = state.window_mask_x;
    command.window_flip[1] = state.window_mask_y;
    command.window_flip[2] = state.flip_x;
    command.window_flip[3] = state.flip_y;
    command.texture[0] = state.texture_page;
    command.texture[1] = clut;
    command.texture[2] = state.gpu_type;
    command.texture[3] = state.gpu_type == 2
        ? ((state.texture_page >> 5) & 3u) : ((state.texture_page >> 7) & 3u);
    int min_x = kVramWidth - 1, min_y = kMaxVramHeight - 1, max_x = 0, max_y = 0;
    for (size_t i = 0; i < vertices.size() && i < 4; ++i) {
        auto& out = command.vertices[i];
        out.xyuv[0] = vertices[i].x + state.offset_x;
        out.xyuv[1] = vertices[i].y + state.offset_y;
        out.xyuv[2] = vertices[i].u;
        out.xyuv[3] = vertices[i].v;
        out.color = vertices[i].color;
        min_x = std::min(min_x, out.xyuv[0]); max_x = std::max(max_x, out.xyuv[0]);
        min_y = std::min(min_y, out.xyuv[1]); max_y = std::max(max_y, out.xyuv[1]);
    }
    if (vertices.empty()) min_x = min_y = max_x = max_y = 0;
    command.bounds[0] = min_x;
    command.bounds[1] = min_y;
    command.bounds[2] = max_x;
    command.bounds[3] = max_y;
    return command;
}
}

GpuHardwareRenderer::GpuHardwareRenderer(std::span<const uint8_t> initial_vram)
    : impl_(std::make_unique<Impl>()) {
    impl_->initialize(initial_vram);
}
GpuHardwareRenderer::~GpuHardwareRenderer() = default;

void GpuHardwareRenderer::draw_polygon(std::span<const GpuHwVertex> vertices,
        const GpuHwState& state, bool textured, bool gouraud, bool raw_texture,
        bool semi_transparent, uint32_t clut) {
    auto command = make_command(0, vertices, state, gouraud, textured,
                               raw_texture, semi_transparent, clut);
    command.header[3] = vertices.empty() ? 0 : vertices.front().color;
    impl_->execute_draw(command);
}

void GpuHardwareRenderer::fill_rect(int x, int y, int width, int height, uint16_t color) {
    width = std::clamp(width, 0, kVramWidth);
    height = std::clamp(height, 0, impl_->vram_height);
    if (!width || !height) return;
    const int wrapped_x = (x % kVramWidth + kVramWidth) % kVramWidth;
    const int wrapped_y = (y % impl_->vram_height + impl_->vram_height) % impl_->vram_height;
    const int first_width = std::min(width, kVramWidth - wrapped_x);
    const int first_height = std::min(height, impl_->vram_height - wrapped_y);
    const auto submit = [&](int left, int top, int w, int h) {
        if (w <= 0 || h <= 0) return;
        ShaderCommand command{};
        command.header[0] = 2;
        command.header[3] = color;
        command.bounds[0] = left;
        command.bounds[1] = top;
        command.bounds[2] = left + w - 1;
        command.bounds[3] = top + h - 1;
        command.texture[2] = 2;
        impl_->execute(command);
    };
    submit(wrapped_x, wrapped_y, first_width, first_height);
    submit(0, wrapped_y, width - first_width, first_height);
    submit(wrapped_x, 0, first_width, height - first_height);
    submit(0, 0, width - first_width, height - first_height);
}

void GpuHardwareRenderer::draw_rect(int x, int y, int width, int height,
        uint32_t color, const GpuHwState& state, bool semi_transparent) {
    const std::array<GpuHwVertex, 4> vertices{{
        {x, y, 0, 0, color}, {x + width, y, 0, 0, color},
        {x, y + height, 0, 0, color}, {x + width, y + height, 0, 0, color}}};
    auto command = make_command(1, vertices, state, false, false, false,
                                semi_transparent, 0);
    command.header[3] = color;
    impl_->execute_draw(command);
}

void GpuHardwareRenderer::draw_line(const GpuHwVertex& a, const GpuHwVertex& b,
        const GpuHwState& state, bool gouraud, bool semi_transparent) {
    const std::array<GpuHwVertex, 2> vertices{a, b};
    auto command = make_command(3, vertices, state, gouraud, false, false,
                                semi_transparent, 0);
    command.header[3] = uint32_t(((a.color >> 3) & 31u) |
        (((a.color >> 11) & 31u) << 5) | (((a.color >> 19) & 31u) << 10));
    impl_->execute(command);
}

void GpuHardwareRenderer::draw_textured_rect(int x, int y, int width, int height,
        int u, int v, uint32_t color, uint32_t clut, const GpuHwState& state,
        bool raw_texture, bool semi_transparent) {
    const std::array<GpuHwVertex, 4> vertices{{
        {x, y, u, v, color},
        {x + width, y, u + width * state.flip_x, v, color},
        {x, y + height, u, v + height * state.flip_y, color},
        {x + width, y + height, u + width * state.flip_x,
         v + height * state.flip_y, color}}};
    auto command = make_command(1, vertices, state, false, true, raw_texture,
                                semi_transparent, clut);
    command.header[3] = color;
    impl_->execute_draw(command);
}

void GpuHardwareRenderer::sync_to_cpu(std::vector<uint8_t>& vram) { impl_->read_full(vram); }
void GpuHardwareRenderer::sync_from_cpu(std::span<const uint8_t> vram) { impl_->upload_full(vram); }

void GpuHardwareRenderer::upload_rect(std::span<const uint8_t> bytes, unsigned x,
        unsigned y, unsigned width, unsigned height) {
    if (!impl_->make_current()) throw std::runtime_error("could not activate PSX OpenGL context");
    if (impl_->vram_textures_diverged) {
        const auto& dirty = impl_->last_dirty_bounds;
        glCopyImageSubData(impl_->vram[impl_->current_vram], GL_TEXTURE_2D, 0,
                           dirty[0], dirty[1], 0,
                           impl_->vram[impl_->current_vram ^ 1u], GL_TEXTURE_2D, 0,
                           dirty[0], dirty[1], 0,
                           dirty[2] - dirty[0] + 1, dirty[3] - dirty[1] + 1, 1);
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    }
    std::vector<uint16_t> packed(size_t(width) * height);
    for (unsigned row = 0; row < height; ++row)
        for (unsigned col = 0; col < width; ++col) {
            const size_t at = (size_t((y + row) % unsigned(impl_->vram_height)) * kVramWidth +
                               ((x + col) & 1023u)) * 2;
            packed[size_t(row) * width + col] = uint16_t(bytes[at] | (uint16_t(bytes[at + 1]) << 8));
        }
    glBindTexture(GL_TEXTURE_2D, impl_->vram[impl_->current_vram]);
    const unsigned first_width = std::min(width, unsigned(kVramWidth) - (x & 1023u));
    const unsigned first_height = std::min(height, unsigned(impl_->vram_height) - (y % unsigned(impl_->vram_height)));
    glPixelStorei(GL_UNPACK_ROW_LENGTH, int(width));
    for (GLuint texture : impl_->vram) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, int(x & 1023u), int(y % unsigned(impl_->vram_height)),
                        int(first_width), int(first_height), GL_RED_INTEGER,
                        GL_UNSIGNED_SHORT, packed.data());
        if (first_width < width)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, int(y % unsigned(impl_->vram_height)),
                            int(width - first_width), int(first_height), GL_RED_INTEGER,
                            GL_UNSIGNED_SHORT, packed.data() + first_width);
        if (first_height < height) {
            const auto* next_rows = packed.data() + size_t(first_height) * width;
            glTexSubImage2D(GL_TEXTURE_2D, 0, int(x & 1023u), 0, int(first_width),
                            int(height - first_height), GL_RED_INTEGER,
                            GL_UNSIGNED_SHORT, next_rows);
            if (first_width < width)
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, int(width - first_width),
                                int(height - first_height), GL_RED_INTEGER,
                                GL_UNSIGNED_SHORT, next_rows + first_width);
        }
    }
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    impl_->vram_textures_diverged = false;
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void GpuHardwareRenderer::copy_rect(int sx, int sy, int dx, int dy, int width,
        int height, const GpuHwState& state) {
    std::vector<uint8_t> shadow;
    sync_to_cpu(shadow);
    std::vector<uint16_t> temp(size_t(width) * height);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const size_t at = (size_t((sy + y) % impl_->vram_height) * kVramWidth + size_t((sx + x) & 1023)) * 2;
            temp[size_t(y) * width + x] = uint16_t(shadow[at] | (uint16_t(shadow[at + 1]) << 8));
        }
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int px = (dx + x + state.offset_x) & 1023;
            const int py = (dy + y + state.offset_y) % impl_->vram_height;
            if (px < state.draw_x1 || px > state.draw_x2 || py < state.draw_y1 || py > state.draw_y2) continue;
            const size_t at = (size_t(py) * kVramWidth + size_t(px)) * 2;
            const uint16_t old = uint16_t(shadow[at] | (uint16_t(shadow[at + 1]) << 8));
            if (state.check_mask && (old & 0x8000)) continue;
            const uint16_t value = uint16_t(temp[size_t(y) * width + x] | (state.set_mask ? 0x8000 : 0));
            shadow[at] = uint8_t(value);
            shadow[at + 1] = uint8_t(value >> 8);
        }
    sync_from_cpu(shadow);
}

const std::string& GpuHardwareRenderer::renderer_name() const noexcept { return impl_->renderer; }

} // namespace mamerecomp::psx
