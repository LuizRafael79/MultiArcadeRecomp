#define GL_GLEXT_PROTOTYPES 1
#include "gpu_hw.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace mamerecomp::psx {
namespace {
constexpr int kVramWidth = 1024;
constexpr int kMinVramHeight = 512;
constexpr int kMaxVramHeight = 1024;
constexpr size_t kCommandRingSlots = 4096;
constexpr size_t kCommandBatchSlots = 256;
constexpr uint32_t kMaxBatchPrimitives = 65535;
constexpr uint32_t kMaxBatchDepthLayers = kMaxBatchPrimitives * 2;
constexpr uint32_t kTextureCacheLayers = 64;

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

struct BatchVertex {
    int32_t position[2]{};
    int32_t uv[2]{};
    uint32_t color = 0;
    float depth = 0.0f;
    uint32_t set_mask = 0;
    uint32_t gouraud = 0;
    uint32_t flags = 0;
    uint32_t texture_page = 0;
    uint32_t clut = 0;
    uint32_t gpu_type = 0;
    int32_t window_state[4]{};
    uint32_t texture_layer = 0;
};
static_assert(sizeof(BatchVertex) == 68);

struct TextureCacheKey {
    uint32_t texture_page = 0;
    uint32_t gpu_type = 0;
    int32_t window_x = 0;
    int32_t window_y = 0;
    int32_t window_mask_x = 0;
    int32_t window_mask_y = 0;

    bool operator==(const TextureCacheKey&) const = default;
};

struct TextureCacheKeyHash {
    size_t operator()(const TextureCacheKey& key) const noexcept {
        size_t value = key.texture_page;
        const auto combine = [&value](uint32_t next) {
            value ^= size_t(next) + size_t(0x9e3779b9u) + (value << 6) + (value >> 2);
        };
        combine(key.gpu_type);
        combine(static_cast<uint32_t>(key.window_x));
        combine(static_cast<uint32_t>(key.window_y));
        combine(static_cast<uint32_t>(key.window_mask_x));
        combine(static_cast<uint32_t>(key.window_mask_y));
        return value;
    }
};

struct TextureCacheEntry {
    TextureCacheKey key{};
    std::array<std::array<int, 4>, 2> source_regions{};
    uint32_t source_region_count = 0;
    uint64_t last_used = 0;
    bool occupied = false;
    bool valid = false;
    bool globally_invalidated = false;
};

struct PendingBatchTexture {
    size_t first_vertex = 0;
    TextureCacheKey key{};
    uint32_t vertex_count = 0;
};

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
    struct ScanoutReadback {
        GLuint buffer = 0;
        GLsync fence = nullptr;
        uint64_t sequence = 0;
        unsigned width = 0;
        unsigned height = 0;
    };

    SDL_Window* window = nullptr;
    SDL_GLContext context = nullptr;
    bool owns_video = false;
    bool owns_window = false;
    bool owns_context = false;
    GLuint program = 0;
    GLuint scanout_program = 0;
    GLuint draw_program = 0;
    GLuint batch_draw_program = 0;
    GLuint texture_cache_program = 0;
    GLuint texture_cache_texture = 0;
    GLuint vram[2]{};
    GLuint copy_scratch = 0;
    GLuint scanout_texture = 0;
    GLuint scanout_framebuffer = 0;
    std::array<ScanoutReadback, 3> scanout_readbacks{};
    size_t next_scanout_readback = 0;
    uint64_t next_scanout_sequence = 1;
    uint64_t displayed_scanout_sequence = 0;
    unsigned scanout_cache_width = 0;
    unsigned scanout_cache_height = 0;
    std::vector<uint8_t> scanout_cache;
    GLuint framebuffer[2]{};
    GLuint batch_depth[2]{};
    GLuint vertex_array = 0;
    GLuint batch_vertex_array = 0;
    GLuint batch_vertex_buffer = 0;
    GLuint command_buffer = 0;
    std::array<GLsync, kCommandRingSlots / kCommandBatchSlots> command_batch_fences{};
    size_t next_command_slot = 0;
    size_t command_stride = sizeof(ShaderCommand);
    GLint origin_location = -1;
    GLint vram_height_location = -1;
    GLint draw_vram_height_location = -1;
    GLint batch_vram_height_location = -1;
    GLint texture_cache_vram_height_location = -1;
    GLint batch_source_location = -1;
    GLint texture_cache_texture_page_location = -1;
    GLint texture_cache_gpu_type_location = -1;
    GLint texture_cache_window_state_location = -1;
    GLint batch_texture_cache_location = -1;
    int vram_height = kMinVramHeight;
    unsigned scanout_width = 0;
    unsigned scanout_height = 0;
    unsigned current_vram = 0;
    bool vram_textures_diverged = false;
    std::array<int, 4> last_dirty_bounds{};
    uint64_t vram_write_generation = 0;
    std::vector<BatchVertex> pending_batch_vertices;
    std::vector<PendingBatchTexture> pending_batch_textures;
    std::array<TextureCacheEntry, kTextureCacheLayers> texture_cache_entries{};
    std::unordered_map<TextureCacheKey, uint32_t, TextureCacheKeyHash> texture_cache_lookup;
    uint64_t texture_cache_clock = 0;
    uint64_t texture_cache_hits = 0;
    uint64_t texture_cache_generations = 0;
    uint64_t texture_cache_invalidations = 0;
    uint64_t texture_cache_evictions = 0;
    uint64_t pending_batch_primitive_count = 0;
    uint32_t pending_batch_depth_count = 0;
    uint64_t submitted_batch_count = 0;
    uint64_t submitted_batch_primitive_count = 0;
    std::array<int, 4> pending_batch_draw_area{};
    std::array<int, 4> pending_batch_dirty_bounds{};
    std::string renderer;
    std::string init_error;
    bool debug_gl = std::getenv("MAMERECOMP_PSX_GPU_GL_CHECK") != nullptr;
    bool batching_enabled = [] {
        const char* value = std::getenv("MAMERECOMP_PSX_GPU_BATCHING");
        return !value || std::strcmp(value, "0") != 0;
    }();
    uint32_t batch_primitive_limit = [] {
        const char* value = std::getenv("MAMERECOMP_PSX_GPU_BATCH_LIMIT");
        if (!value || !*value) return kMaxBatchPrimitives;
        char* end = nullptr;
        const unsigned long parsed = std::strtoul(value, &end, 10);
        if (end == value || *end != '\0' || parsed == 0 ||
            parsed > kMaxBatchPrimitives)
            return kMaxBatchPrimitives;
        return static_cast<uint32_t>(parsed);
    }();

    ~Impl() {
        make_current();
        if (program) glDeleteProgram(program);
        if (scanout_program) glDeleteProgram(scanout_program);
        if (draw_program) glDeleteProgram(draw_program);
        if (batch_draw_program) glDeleteProgram(batch_draw_program);
        if (texture_cache_program) glDeleteProgram(texture_cache_program);
        if (texture_cache_texture) glDeleteTextures(1, &texture_cache_texture);
        if (vram[0] || vram[1]) glDeleteTextures(2, vram);
        if (copy_scratch) glDeleteTextures(1, &copy_scratch);
        if (scanout_texture) glDeleteTextures(1, &scanout_texture);
        if (scanout_framebuffer) glDeleteFramebuffers(1, &scanout_framebuffer);
        if (batch_depth[0] || batch_depth[1]) glDeleteRenderbuffers(2, batch_depth);
        for (auto& readback : scanout_readbacks) {
            if (readback.fence) glDeleteSync(readback.fence);
            if (readback.buffer) glDeleteBuffers(1, &readback.buffer);
        }
        if (framebuffer[0] || framebuffer[1]) glDeleteFramebuffers(2, framebuffer);
        if (vertex_array) glDeleteVertexArrays(1, &vertex_array);
        if (batch_vertex_array) glDeleteVertexArrays(1, &batch_vertex_array);
        if (batch_vertex_buffer) glDeleteBuffers(1, &batch_vertex_buffer);
        if (command_buffer) glDeleteBuffers(1, &command_buffer);
        for (GLsync fence : command_batch_fences)
            if (fence) glDeleteSync(fence);
        if (owns_context && context) SDL_GL_DestroyContext(context);
        if (owns_window && window) SDL_DestroyWindow(window);
        if (owns_video) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    bool make_current() const {
        if (!context || !window) return false;
        // The GP0 path calls this for each command. SDL_GL_MakeCurrent is
        // needlessly expensive when the PSX context is already current (the
        // common case between vblanks), so only switch after another SDL
        // renderer/context has actually taken ownership of the thread.
        if (SDL_GL_GetCurrentContext() == context &&
            SDL_GL_GetCurrentWindow() == window)
            return true;
        return SDL_GL_MakeCurrent(window, context);
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
        if (!window || !context) {
            if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4) ||
                !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3) ||
                !SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE))
                throw std::runtime_error(std::string("PSX OpenGL context attributes failed: ") + SDL_GetError());
            window = SDL_CreateWindow("MameRecomp PSX GPU", 32, 32,
                                      SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
            if (!window) throw std::runtime_error(std::string("hidden PSX OpenGL window failed: ") + SDL_GetError());
            owns_window = true;
            context = SDL_GL_CreateContext(window);
            if (!context) throw std::runtime_error(std::string("PSX OpenGL context creation failed: ") + SDL_GetError());
            owns_context = true;
        }
        if (!make_current()) throw std::runtime_error(std::string("PSX OpenGL context activation failed: ") + SDL_GetError());
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major < 4 || (major == 4 && minor < 3))
            throw std::runtime_error("PSX hardware rasterizer requires OpenGL 4.3");
        if (const auto* name = glGetString(GL_RENDERER)) renderer = reinterpret_cast<const char*>(name);
        program = compile_compute_program(read_shader_source(MAMERECOMP_PSX_GPU_SHADER_PATH), init_error);
        if (!program) throw std::runtime_error(init_error);
        scanout_program = compile_compute_program(
            read_shader_source(MAMERECOMP_PSX_GPU_SCANOUT_SHADER_PATH), init_error);
        if (!scanout_program) throw std::runtime_error(init_error);
        draw_program = compile_graphics_program(
            read_shader_source(MAMERECOMP_PSX_GPU_VERTEX_SHADER_PATH),
            read_shader_source(MAMERECOMP_PSX_GPU_FRAGMENT_SHADER_PATH), init_error);
        if (!draw_program) throw std::runtime_error(init_error);
        batch_draw_program = compile_graphics_program(
            read_shader_source(MAMERECOMP_PSX_GPU_BATCH_VERTEX_SHADER_PATH),
            read_shader_source(MAMERECOMP_PSX_GPU_BATCH_FRAGMENT_SHADER_PATH), init_error);
        if (!batch_draw_program) throw std::runtime_error(init_error);
        texture_cache_program = compile_compute_program(
            read_shader_source(MAMERECOMP_PSX_GPU_TEXTURE_CACHE_SHADER_PATH), init_error);
        if (!texture_cache_program) throw std::runtime_error(init_error);
        origin_location = glGetUniformLocation(program, "uDispatchOrigin");
        vram_height_location = glGetUniformLocation(program, "uVramHeight");
        draw_vram_height_location = glGetUniformLocation(draw_program, "uVramHeight");
        batch_vram_height_location = glGetUniformLocation(batch_draw_program, "uVramHeight");
        batch_texture_cache_location = glGetUniformLocation(batch_draw_program, "uTextureCache");
        batch_source_location = glGetUniformLocation(batch_draw_program, "uVram");
        texture_cache_vram_height_location = glGetUniformLocation(texture_cache_program, "uVramHeight");
        texture_cache_texture_page_location = glGetUniformLocation(texture_cache_program, "uTexturePage");
        texture_cache_gpu_type_location = glGetUniformLocation(texture_cache_program, "uGpuType");
        texture_cache_window_state_location = glGetUniformLocation(texture_cache_program, "uWindowState");
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
        glGenTextures(1, &texture_cache_texture);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texture_cache_texture);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_R16UI, 256, 256,
                       static_cast<GLsizei>(kTextureCacheLayers));
        glGenTextures(1, &copy_scratch);
        glBindTexture(GL_TEXTURE_2D, copy_scratch);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, kVramWidth, kMaxVramHeight, 0,
                     GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr);
        glGenTextures(1, &scanout_texture);
        glBindTexture(GL_TEXTURE_2D, scanout_texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 640, 512, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenFramebuffers(1, &scanout_framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, scanout_framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, scanout_texture, 0);
        glDrawBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("PSX OpenGL scanout framebuffer is incomplete");
        for (auto& readback : scanout_readbacks) {
            glGenBuffers(1, &readback.buffer);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, readback.buffer);
            glBufferData(GL_PIXEL_PACK_BUFFER, 640 * 512 * 4, nullptr, GL_STREAM_READ);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glGenFramebuffers(2, framebuffer);
        glGenRenderbuffers(2, batch_depth);
        for (unsigned i = 0; i < 2; ++i) {
            glBindFramebuffer(GL_FRAMEBUFFER, framebuffer[i]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, vram[i], 0);
            glBindRenderbuffer(GL_RENDERBUFFER, batch_depth[i]);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                                  kVramWidth, vram_height);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                      GL_RENDERBUFFER, batch_depth[i]);
            glDrawBuffer(GL_COLOR_ATTACHMENT0);
            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                throw std::runtime_error("PSX OpenGL R16UI framebuffer is incomplete");
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glGenVertexArrays(1, &vertex_array);
        glBindVertexArray(vertex_array);
        glGenVertexArrays(1, &batch_vertex_array);
        glBindVertexArray(batch_vertex_array);
        glGenBuffers(1, &batch_vertex_buffer);
        glBindBuffer(GL_ARRAY_BUFFER, batch_vertex_buffer);
        glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribIPointer(0, 2, GL_INT, sizeof(BatchVertex),
                               reinterpret_cast<const void*>(offsetof(BatchVertex, position)));
        glEnableVertexAttribArray(1);
        glVertexAttribIPointer(1, 1, GL_UNSIGNED_INT, sizeof(BatchVertex),
                               reinterpret_cast<const void*>(offsetof(BatchVertex, color)));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(BatchVertex),
                              reinterpret_cast<const void*>(offsetof(BatchVertex, depth)));
        glEnableVertexAttribArray(3);
        glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(BatchVertex),
                               reinterpret_cast<const void*>(offsetof(BatchVertex, set_mask)));
        glEnableVertexAttribArray(4);
        glVertexAttribIPointer(4, 1, GL_UNSIGNED_INT, sizeof(BatchVertex),
                               reinterpret_cast<const void*>(offsetof(BatchVertex, gouraud)));
        glEnableVertexAttribArray(5);
        glVertexAttribIPointer(5, 2, GL_INT, sizeof(BatchVertex),
                               reinterpret_cast<const void*>(offsetof(BatchVertex, uv)));
        glEnableVertexAttribArray(6);
        glVertexAttribIPointer(6, 1, GL_UNSIGNED_INT, sizeof(BatchVertex),
                               reinterpret_cast<const void*>(offsetof(BatchVertex, flags)));
        glEnableVertexAttribArray(7);
        glVertexAttribIPointer(7, 1, GL_UNSIGNED_INT, sizeof(BatchVertex),
                               reinterpret_cast<const void*>(offsetof(BatchVertex, texture_page)));
        glEnableVertexAttribArray(8);
        glVertexAttribIPointer(8, 1, GL_UNSIGNED_INT, sizeof(BatchVertex),
                               reinterpret_cast<const void*>(offsetof(BatchVertex, clut)));
        glEnableVertexAttribArray(9);
        glVertexAttribIPointer(9, 1, GL_UNSIGNED_INT, sizeof(BatchVertex),
                               reinterpret_cast<const void*>(offsetof(BatchVertex, gpu_type)));
        glEnableVertexAttribArray(10);
        glVertexAttribIPointer(10, 4, GL_INT, sizeof(BatchVertex),
                               reinterpret_cast<const void*>(offsetof(BatchVertex, window_state)));
        glEnableVertexAttribArray(11);
        glVertexAttribIPointer(11, 1, GL_UNSIGNED_INT, sizeof(BatchVertex),
                               reinterpret_cast<const void*>(offsetof(BatchVertex, texture_layer)));
        GLint storage_alignment = 1;
        glGetIntegerv(GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT, &storage_alignment);
        const size_t alignment = static_cast<size_t>(std::max(storage_alignment, 1));
        command_stride = (sizeof(ShaderCommand) + alignment - 1) / alignment * alignment;
        glGenBuffers(1, &command_buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, command_buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     static_cast<GLsizeiptr>(command_stride * kCommandRingSlots),
                     nullptr, GL_STREAM_DRAW);
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "uSource"), 1);
        glUseProgram(scanout_program);
        glUniform1i(glGetUniformLocation(scanout_program, "uVram"), 0);
        glUseProgram(draw_program);
        glUniform1i(glGetUniformLocation(draw_program, "uSource"), 0);
        glUseProgram(batch_draw_program);
        glUniform1i(batch_texture_cache_location, 1);
        glUniform1i(batch_source_location, 0);
        glUseProgram(texture_cache_program);
        glUniform1i(glGetUniformLocation(texture_cache_program, "uSource"), 0);
        glBindVertexArray(0);
        const GLenum error = glGetError();
        if (error != GL_NO_ERROR) throw std::runtime_error("PSX OpenGL resource creation failed");
        return true;
    }

    void upload_full(std::span<const uint8_t> bytes) {
        flush_draw_batch();
        if (!make_current()) throw std::runtime_error("could not activate PSX OpenGL context");
        const auto words = to_words(bytes);
        for (GLuint texture : vram) {
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kVramWidth, vram_height,
                            GL_RED_INTEGER, GL_UNSIGNED_SHORT, words.data());
        }
        current_vram = 0;
        vram_textures_diverged = false;
        invalidate_texture_cache_all();
        ++vram_write_generation;
    }

    void read_full(std::vector<uint8_t>& bytes) {
        flush_draw_batch();
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
        invalidate_texture_cache_region(min_x, min_y, max_x, max_y);
        ++vram_write_generation;
    }

    void finish_in_place(int min_x, int min_y, int max_x, int max_y) {
        invalidate_texture_cache_region(min_x, min_y, max_x, max_y);
        ++vram_write_generation;
        if (!vram_textures_diverged) {
            last_dirty_bounds = {min_x, min_y, max_x, max_y};
            vram_textures_diverged = true;
            return;
        }
        last_dirty_bounds[0] = std::min(last_dirty_bounds[0], min_x);
        last_dirty_bounds[1] = std::min(last_dirty_bounds[1], min_y);
        last_dirty_bounds[2] = std::max(last_dirty_bounds[2], max_x);
        last_dirty_bounds[3] = std::max(last_dirty_bounds[3], max_y);
    }

    size_t upload_command(const ShaderCommand& command) {
        const size_t slot = next_command_slot++ % kCommandRingSlots;
        const size_t batch = slot / kCommandBatchSlots;
        GLsync& fence = command_batch_fences[batch];
        if (slot % kCommandBatchSlots == 0 && fence) {
            GLenum wait_result = GL_TIMEOUT_EXPIRED;
            while (wait_result == GL_TIMEOUT_EXPIRED)
                wait_result = glClientWaitSync(fence, GL_SYNC_FLUSH_COMMANDS_BIT,
                                               1'000'000'000ull);
            glDeleteSync(fence);
            fence = nullptr;
            if (wait_result == GL_WAIT_FAILED)
                throw std::runtime_error("waiting for PSX OpenGL command slot failed");
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, command_buffer);
        const GLintptr offset = static_cast<GLintptr>(slot * command_stride);
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, offset, sizeof(command), &command);
        glBindBufferRange(GL_SHADER_STORAGE_BUFFER, 0, command_buffer, offset,
                          sizeof(command));
        return slot;
    }

    void mark_command_slot(size_t slot) {
        if (slot % kCommandBatchSlots == kCommandBatchSlots - 1) {
            const size_t batch = slot / kCommandBatchSlots;
            command_batch_fences[batch] = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        }
    }

    bool texture_read_overlaps_pending_writes(const ShaderCommand& command) const {
        if (pending_batch_vertices.empty() || (command.header[2] & 2u) == 0)
            return false;

        const uint32_t tpage = command.texture[0];
        const uint32_t gpu_type = command.texture[2];
        const int depth = gpu_type == 2u ? int((tpage >> 7) & 3u)
                                         : int((tpage >> 9) & 3u);
        if (depth == 3) return false;
        if (gpu_type != 2u && (tpage & 0x2000u) != 0u)
            return true; // conservatively flush interleaved texture layouts

        const int words_per_pixel_group = depth == 0 ? 4 : depth == 1 ? 2 : 1;
        const int page_x = int(tpage & 15u) * 64;
        const int page_y = gpu_type == 2u
            ? int(((tpage & 0x10u) << 4) | ((tpage & 0x800u) >> 2))
            : int(tpage & 0x60u) << 3;
        const int source_x = page_x + command.offset_window[2] / words_per_pixel_group;
        const int source_y = page_y + command.offset_window[3];
        const int source_width = (command.window_flip[0] & 255) /
                                 words_per_pixel_group + 1;
        const int source_height = (command.window_flip[1] & 255) + 1;
        const auto& dirty = pending_batch_dirty_bounds;
        const auto intersects = [&](int x0, int y0, int x1, int y1) {
            return !(x1 < dirty[0] || dirty[2] < x0 ||
                     y1 < dirty[1] || dirty[3] < y0);
        };

        if (source_x < 0 || source_y < 0 || source_x + source_width > kVramWidth ||
            source_y + source_height > vram_height)
            return true; // wrapped source addressing is handled conservatively
        if (intersects(source_x, source_y, source_x + source_width - 1,
                       source_y + source_height - 1))
            return true;

        if (depth < 2) {
            const int clut_x = int(command.texture[1] & 63u) * 16;
            const int clut_y = int((command.texture[1] >> 6) & 1023u);
            const int clut_width = depth == 0 ? 16 : 256;
            if (clut_y >= vram_height || clut_x + clut_width > kVramWidth)
                return true;
            if (intersects(clut_x, clut_y, clut_x + clut_width - 1, clut_y))
                return true;
        }
        return false;
    }

    TextureCacheKey make_texture_cache_key(const ShaderCommand& command) const {
        return {command.texture[0], command.texture[2],
                command.offset_window[2], command.offset_window[3],
                command.window_flip[0], command.window_flip[1]};
    }

    void set_texture_cache_source_regions(TextureCacheEntry& entry) {
        entry.source_region_count = 0;
        entry.globally_invalidated = false;
        const auto& key = entry.key;
        const int depth = key.gpu_type == 2u ? int((key.texture_page >> 7) & 3u)
                                             : int((key.texture_page >> 9) & 3u);
        if (depth == 3) return;
        if (key.gpu_type != 2u && (key.texture_page & 0x2000u) != 0u) {
            entry.globally_invalidated = true;
            return;
        }

        const int words_per_pixel_group = depth == 0 ? 4 : depth == 1 ? 2 : 1;
        const int page_x = int(key.texture_page & 15u) * 64;
        const int page_y = key.gpu_type == 2u
            ? int(((key.texture_page & 0x10u) << 4) |
                  ((key.texture_page & 0x800u) >> 2))
            : int(key.texture_page & 0x60u) << 3;
        const int x = page_x + key.window_x / words_per_pixel_group;
        const int y = page_y + key.window_y;
        const int width = (key.window_mask_x & 255) / words_per_pixel_group + 1;
        const int height = (key.window_mask_y & 255) + 1;
        if (x < 0 || y < 0 || x + width > kVramWidth || y + height > vram_height) {
            entry.globally_invalidated = true;
            return;
        }
        entry.source_regions[entry.source_region_count++] =
            {x, y, x + width - 1, y + height - 1};
    }

    void invalidate_texture_cache_all() {
        for (auto& entry : texture_cache_entries) {
            if (entry.occupied && entry.valid) {
                entry.valid = false;
                ++texture_cache_invalidations;
            }
        }
    }

    void invalidate_texture_cache_region(int left, int top, int right, int bottom) {
        for (auto& entry : texture_cache_entries) {
            if (!entry.occupied || !entry.valid) continue;
            bool overlaps = entry.globally_invalidated;
            for (uint32_t i = 0; !overlaps && i < entry.source_region_count; ++i) {
                const auto& region = entry.source_regions[i];
                overlaps = !(right < region[0] || region[2] < left ||
                             bottom < region[1] || region[3] < top);
            }
            if (overlaps) {
                entry.valid = false;
                ++texture_cache_invalidations;
            }
        }
    }

    uint32_t ensure_texture_cache_layer(const TextureCacheKey& key) {
        uint32_t layer = 0;
        auto found = texture_cache_lookup.find(key);
        if (found != texture_cache_lookup.end()) {
            layer = found->second;
            auto& entry = texture_cache_entries[layer];
            entry.last_used = ++texture_cache_clock;
            if (entry.valid) {
                ++texture_cache_hits;
                return layer;
            }
        } else {
            uint32_t replacement = 0;
            uint64_t oldest = UINT64_MAX;
            for (uint32_t i = 0; i < kTextureCacheLayers; ++i) {
                const auto& entry = texture_cache_entries[i];
                if (!entry.occupied) {
                    replacement = i;
                    break;
                }
                if (entry.last_used < oldest) {
                    oldest = entry.last_used;
                    replacement = i;
                }
            }
            layer = replacement;
            auto& entry = texture_cache_entries[layer];
            if (entry.occupied) {
                texture_cache_lookup.erase(entry.key);
                ++texture_cache_evictions;
            }
            entry = {};
            entry.key = key;
            entry.occupied = true;
            entry.last_used = ++texture_cache_clock;
            set_texture_cache_source_regions(entry);
            texture_cache_lookup.emplace(key, layer);
        }

        auto& entry = texture_cache_entries[layer];
        glUseProgram(texture_cache_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, vram[current_vram]);
        glBindImageTexture(1, texture_cache_texture, 0, GL_FALSE, layer,
                           GL_WRITE_ONLY, GL_R16UI);
        glUniform1i(texture_cache_vram_height_location, vram_height);
        glUniform1ui(texture_cache_texture_page_location, key.texture_page);
        glUniform1ui(texture_cache_gpu_type_location, key.gpu_type);
        glUniform4i(texture_cache_window_state_location,
                    key.window_x, key.window_y,
                    key.window_mask_x, key.window_mask_y);
        glDispatchCompute(32, 32, 1);
        entry.valid = true;
        ++texture_cache_generations;
        return layer;
    }

    void prepare_batch_texture_cache() {
        if (pending_batch_textures.empty()) return;
        for (const auto& pending : pending_batch_textures) {
            const uint32_t layer = ensure_texture_cache_layer(pending.key);
            for (uint32_t i = 0; i < pending.vertex_count; ++i)
                pending_batch_vertices[pending.first_vertex + i].texture_layer = layer;
        }
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    bool queue_opaque_primitive(const ShaderCommand& command) {
        if (!batching_enabled) return false;
        const unsigned vertex_count = command.header[1];
        const uint32_t flags = command.header[2];
        if (vertex_count != 3 && vertex_count != 4)
            return false;

        const int left = std::clamp(command.bounds[0], 0, kVramWidth - 1);
        const int top = std::clamp(command.bounds[1], 0, vram_height - 1);
        const int right = std::clamp(command.bounds[2], 0, kVramWidth - 1);
        const int bottom = std::clamp(command.bounds[3], 0, vram_height - 1);
        if (left > right || top > bottom) return true;

        const std::array<int, 4> draw_area{
            std::clamp(command.draw_area[0], 0, kVramWidth - 1),
            std::clamp(command.draw_area[1], 0, vram_height - 1),
            std::clamp(command.draw_area[2], 0, kVramWidth - 1),
            std::clamp(command.draw_area[3], 0, vram_height - 1)};
        if (draw_area[0] > draw_area[2] || draw_area[1] > draw_area[3])
            return true;
        bool must_flush = !pending_batch_vertices.empty() &&
            pending_batch_draw_area != draw_area;
        const bool reads_destination = (flags & (8u | 32u)) != 0;
        if (!pending_batch_vertices.empty() && reads_destination) {
            const auto& dirty = pending_batch_dirty_bounds;
            must_flush |= !(right < dirty[0] || dirty[2] < left ||
                            bottom < dirty[1] || dirty[3] < top);
        }
        if (texture_read_overlaps_pending_writes(command)) must_flush = true;
        if (pending_batch_primitive_count >= batch_primitive_limit) must_flush = true;
        if (must_flush) flush_draw_batch();
        if (pending_batch_vertices.empty()) {
            pending_batch_draw_area = draw_area;
            pending_batch_dirty_bounds = {left, top, right, bottom};
        } else {
            pending_batch_dirty_bounds[0] = std::min(pending_batch_dirty_bounds[0], left);
            pending_batch_dirty_bounds[1] = std::min(pending_batch_dirty_bounds[1], top);
            pending_batch_dirty_bounds[2] = std::max(pending_batch_dirty_bounds[2], right);
            pending_batch_dirty_bounds[3] = std::max(pending_batch_dirty_bounds[3], bottom);
        }
        ++pending_batch_primitive_count;
        const uint32_t set_mask = (flags & 16u) ? 0x8000u : 0u;

        const unsigned indices[6] = {0, 1, 2, 2, 1, 3};
        const unsigned output_count = vertex_count == 4 ? 6 : 3;
        const size_t first_vertex = pending_batch_vertices.size();
        if (flags & 2u)
            pending_batch_textures.push_back({first_vertex,
                make_texture_cache_key(command), output_count});
        for (unsigned i = 0; i < output_count; ++i) {
            const auto& source = command.vertices[vertex_count == 4 ? indices[i] : i];
            BatchVertex vertex{};
            vertex.position[0] = source.xyuv[0];
            vertex.position[1] = source.xyuv[1];
            vertex.uv[0] = source.xyuv[2];
            vertex.uv[1] = source.xyuv[3];
            vertex.color = (flags & (1u | 2u)) ? source.color : command.header[3];
            const uint32_t triangle_depth = pending_batch_depth_count +
                ((vertex_count == 4 && i >= 3) ? 2u : 1u);
            vertex.depth = static_cast<float>(triangle_depth) /
                           static_cast<float>(kMaxBatchDepthLayers);
            vertex.set_mask = set_mask;
            vertex.gouraud = (flags & 1u) ? 1u : 0u;
            vertex.flags = flags;
            vertex.texture_page = command.texture[0];
            vertex.clut = command.texture[1];
            vertex.gpu_type = command.texture[2];
            vertex.window_state[0] = command.offset_window[2];
            vertex.window_state[1] = command.offset_window[3];
            vertex.window_state[2] = command.window_flip[0];
            vertex.window_state[3] = command.window_flip[1];
            pending_batch_vertices.push_back(vertex);
        }
        pending_batch_depth_count += vertex_count == 4 ? 2u : 1u;
        return true;
    }

    void flush_draw_batch() {
        if (pending_batch_vertices.empty()) return;
        if (!make_current()) throw std::runtime_error("could not activate PSX OpenGL context");

        const unsigned destination_vram = prepare_destination();
        prepare_batch_texture_cache();
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, framebuffer[destination_vram]);
        glViewport(0, 0, kVramWidth, vram_height);
        glEnable(GL_SCISSOR_TEST);
        glScissor(pending_batch_draw_area[0], pending_batch_draw_area[1],
                  pending_batch_draw_area[2] - pending_batch_draw_area[0] + 1,
                  pending_batch_draw_area[3] - pending_batch_draw_area[1] + 1);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_GREATER);
        glDepthMask(GL_TRUE);
        constexpr GLfloat clear_depth = 0.0f;
        glClearBufferfv(GL_DEPTH, 0, &clear_depth);
        glDisable(GL_DITHER);
        glUseProgram(batch_draw_program);
        glUniform1i(batch_vram_height_location, vram_height);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, vram[current_vram]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D_ARRAY, texture_cache_texture);
        glBindVertexArray(batch_vertex_array);
        glBindBuffer(GL_ARRAY_BUFFER, batch_vertex_buffer);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(pending_batch_vertices.size() * sizeof(BatchVertex)),
                     pending_batch_vertices.data(), GL_STREAM_DRAW);
        glDrawArrays(GL_TRIANGLES, 0,
                     static_cast<GLsizei>(pending_batch_vertices.size()));
        glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                        GL_TEXTURE_UPDATE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        finish_write(destination_vram, pending_batch_dirty_bounds[0],
                     pending_batch_dirty_bounds[1], pending_batch_dirty_bounds[2],
                     pending_batch_dirty_bounds[3]);
        ++submitted_batch_count;
        submitted_batch_primitive_count += pending_batch_primitive_count;
        pending_batch_primitive_count = 0;
        pending_batch_depth_count = 0;
        pending_batch_vertices.clear();
        pending_batch_textures.clear();
        if (debug_gl && glGetError() != GL_NO_ERROR)
            throw std::runtime_error("PSX OpenGL batch rasterization failed");
    }

    void execute(ShaderCommand command, GLuint source_override = 0) {
        flush_draw_batch();
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
        const bool in_place = command.header[0] == 2u || command.header[0] == 3u;
        const unsigned destination_vram = in_place ? current_vram : prepare_destination();
        glUseProgram(program);
        const size_t command_slot = upload_command(command);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, source_override ? source_override :
                      (in_place ? copy_scratch : vram[current_vram]));
        glBindImageTexture(0, vram[destination_vram], 0, GL_FALSE, 0,
                           GL_READ_WRITE, GL_R16UI);
        glUniform2i(origin_location, min_x, min_y);
        glUniform1i(vram_height_location, vram_height);
        glDispatchCompute((max_x - min_x + 1 + 7) / 8,
                          (max_y - min_y + 1 + 7) / 8, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                        GL_TEXTURE_UPDATE_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
        if (in_place) finish_in_place(min_x, min_y, max_x, max_y);
        else finish_write(destination_vram, min_x, min_y, max_x, max_y);
        mark_command_slot(command_slot);
        if (debug_gl) {
            const GLenum error = glGetError();
            if (error != GL_NO_ERROR) throw std::runtime_error("PSX OpenGL raster dispatch failed");
        }
    }

    void execute_draw(ShaderCommand command) {
        flush_draw_batch();
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
        const size_t command_slot = upload_command(command);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, vram[current_vram]);
        glBindVertexArray(vertex_array);
        glUniform1i(draw_vram_height_location, vram_height);
        glDrawArrays(GL_TRIANGLES, 0, command.header[1] == 4u ? 6 : 3);
        glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                        GL_TEXTURE_UPDATE_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        finish_write(destination_vram, min_x, min_y, max_x, max_y);
        mark_command_slot(command_slot);
        if (debug_gl) {
            const GLenum error = glGetError();
            if (error != GL_NO_ERROR) throw std::runtime_error("PSX OpenGL triangle raster failed");
        }
    }

    std::vector<uint8_t> scanout_rgb(unsigned width, unsigned height,
                                     bool display_enabled, bool rgb24,
                                     unsigned display_start_x, unsigned display_start_y,
                                     unsigned source_vram_height, int source_x, int source_y,
                                     int destination_x, int destination_y,
                                     int copy_width, int copy_height,
                                     bool asynchronous, bool direct_present) {
        flush_draw_batch();
        if (!make_current()) throw std::runtime_error("could not activate PSX OpenGL context");
        if (!width || !height || width > 640 || height > 512 || !source_vram_height)
            throw std::runtime_error("invalid PSX OpenGL scanout dimensions");
        if (scanout_width != width || scanout_height != height) {
            glBindTexture(GL_TEXTURE_2D, scanout_texture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(width),
                         static_cast<GLsizei>(height), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            scanout_width = width;
            scanout_height = height;
        }
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
        glUseProgram(scanout_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, vram[current_vram]);
        glBindImageTexture(1, scanout_texture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
        glUniform2i(glGetUniformLocation(scanout_program, "uDisplayStart"),
                    static_cast<GLint>(display_start_x), static_cast<GLint>(display_start_y));
        glUniform2i(glGetUniformLocation(scanout_program, "uSourceOffset"), source_x, source_y);
        glUniform2i(glGetUniformLocation(scanout_program, "uDestinationOffset"),
                    destination_x, destination_y);
        glUniform2i(glGetUniformLocation(scanout_program, "uCopySize"), copy_width, copy_height);
        glUniform1i(glGetUniformLocation(scanout_program, "uVramHeight"),
                    static_cast<GLint>(source_vram_height));
        glUniform1i(glGetUniformLocation(scanout_program, "uRgb24"), rgb24 ? 1 : 0);
        glUniform1i(glGetUniformLocation(scanout_program, "uDisplayEnabled"),
                    display_enabled ? 1 : 0);
        glDispatchCompute((width + 7) / 8, (height + 7) / 8, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_TEXTURE_UPDATE_BARRIER_BIT |
                        GL_FRAMEBUFFER_BARRIER_BIT);
        scanout_width = width;
        scanout_height = height;

        glBindTexture(GL_TEXTURE_2D, scanout_texture);
        if (direct_present) return {};
        if (asynchronous) {
            if (scanout_cache_width != width || scanout_cache_height != height) {
                scanout_cache_width = width;
                scanout_cache_height = height;
                scanout_cache.assign(size_t(width) * height * 3, 0);
                displayed_scanout_sequence = 0;
            }

            for (auto& readback : scanout_readbacks) {
                if (!readback.fence) continue;
                const GLenum status = glClientWaitSync(
                    readback.fence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
                if (status != GL_ALREADY_SIGNALED && status != GL_CONDITION_SATISFIED) {
                    if (status == GL_WAIT_FAILED && debug_gl)
                        throw std::runtime_error("waiting for PSX scanout readback failed");
                    continue;
                }
                if (readback.width == width && readback.height == height &&
                    readback.sequence >= displayed_scanout_sequence) {
                    const size_t rgba_size = size_t(width) * height * 4;
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, readback.buffer);
                    const auto* rgba = static_cast<const uint8_t*>(glMapBufferRange(
                        GL_PIXEL_PACK_BUFFER, 0, static_cast<GLsizeiptr>(rgba_size),
                        GL_MAP_READ_BIT));
                    if (rgba) {
                        for (size_t pixel = 0; pixel < size_t(width) * height; ++pixel) {
                            scanout_cache[pixel * 3] = rgba[pixel * 4];
                            scanout_cache[pixel * 3 + 1] = rgba[pixel * 4 + 1];
                            scanout_cache[pixel * 3 + 2] = rgba[pixel * 4 + 2];
                        }
                        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
                        displayed_scanout_sequence = readback.sequence;
                    }
                    glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                }
                glDeleteSync(readback.fence);
                readback.fence = nullptr;
            }

            for (size_t attempt = 0; attempt < scanout_readbacks.size(); ++attempt) {
                const size_t slot = (next_scanout_readback + attempt) % scanout_readbacks.size();
                auto& readback = scanout_readbacks[slot];
                if (readback.fence) continue;
                glBindBuffer(GL_PIXEL_PACK_BUFFER, readback.buffer);
                glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
                readback.fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
                readback.sequence = next_scanout_sequence++;
                readback.width = width;
                readback.height = height;
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                next_scanout_readback = (slot + 1) % scanout_readbacks.size();
                break;
            }
            // Submit at vblank so the following frame can poll a completed
            // transfer without synchronously waiting for the GPU.
            glFlush();
            if (debug_gl) {
                const GLenum error = glGetError();
                if (error != GL_NO_ERROR) throw std::runtime_error("PSX OpenGL async scanout failed");
            }
            return scanout_cache;
        }

        std::vector<uint8_t> rgba(size_t(width) * height * 4);
        glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        std::vector<uint8_t> rgb(size_t(width) * height * 3);
        for (size_t pixel = 0; pixel < size_t(width) * height; ++pixel) {
            rgb[pixel * 3] = rgba[pixel * 4];
            rgb[pixel * 3 + 1] = rgba[pixel * 4 + 1];
            rgb[pixel * 3 + 2] = rgba[pixel * 4 + 2];
        }
        if (debug_gl) {
            const GLenum error = glGetError();
            if (error != GL_NO_ERROR) throw std::runtime_error("PSX OpenGL scanout failed");
        }
        return rgb;
    }

    bool present_scanout() {
        if (!make_current()) throw std::runtime_error("could not activate PSX OpenGL context");
        if (!scanout_width || !scanout_height) return false;
        int output_width = 0;
        int output_height = 0;
        if (!SDL_GetWindowSizeInPixels(window, &output_width, &output_height) ||
            output_width <= 0 || output_height <= 0)
            throw std::runtime_error("could not query PSX OpenGL window pixel size");

        int destination_width = output_width;
        int destination_height = output_height;
        constexpr int aspect_width = 4;
        constexpr int aspect_height = 3;
        if (static_cast<int64_t>(output_width) * aspect_height >
            static_cast<int64_t>(output_height) * aspect_width) {
            destination_width = output_height * aspect_width / aspect_height;
        } else {
            destination_height = output_width * aspect_height / aspect_width;
        }
        const int left = (output_width - destination_width) / 2;
        const int bottom = (output_height - destination_height) / 2;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, scanout_framebuffer);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
        glDrawBuffer(GL_BACK);
        // GP0 draws leave scissor enabled for their VRAM bounds. Blits and
        // clears honor that state too, so reset it before scaling scanout to
        // the host window or only the last primitive's rectangle is shown.
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glViewport(0, 0, output_width, output_height);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBlitFramebuffer(0, static_cast<GLint>(scanout_height),
                          static_cast<GLint>(scanout_width), 0,
                          left, bottom, left + destination_width, bottom + destination_height,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
        SDL_GL_SwapWindow(window);
        if (debug_gl && glGetError() != GL_NO_ERROR)
            throw std::runtime_error("PSX OpenGL direct scanout presentation failed");
        return true;
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

GpuHardwareRenderer::GpuHardwareRenderer(std::span<const uint8_t> initial_vram,
                                         void* native_window, void* native_context)
    : impl_(std::make_unique<Impl>()) {
    if (native_window || native_context) {
        if (!native_window || !native_context)
            throw std::invalid_argument("PSX OpenGL external window and context must be provided together");
        impl_->window = static_cast<SDL_Window*>(native_window);
        impl_->context = static_cast<SDL_GLContext>(native_context);
    }
    impl_->initialize(initial_vram);
}
GpuHardwareRenderer::~GpuHardwareRenderer() {
    if (std::getenv("MAMERECOMP_PSX_GPU_BATCH_STATS")) {
        std::cerr << "PSX GPU batch submissions=" << impl_->submitted_batch_count
                  << " primitives=" << impl_->submitted_batch_primitive_count;
        if (impl_->submitted_batch_count)
            std::cerr << " average=" << (double(impl_->submitted_batch_primitive_count) /
                                            double(impl_->submitted_batch_count));
        std::cerr << "; PSX texture cache generations=" << impl_->texture_cache_generations
                  << " hits=" << impl_->texture_cache_hits
                  << " invalidations=" << impl_->texture_cache_invalidations
                  << " evictions=" << impl_->texture_cache_evictions << '\n';
    }
}

void GpuHardwareRenderer::draw_polygon(std::span<const GpuHwVertex> vertices,
        const GpuHwState& state, bool textured, bool gouraud, bool raw_texture,
        bool semi_transparent, uint32_t clut) {
    auto command = make_command(0, vertices, state, gouraud, textured,
                               raw_texture, semi_transparent, clut);
    command.header[3] = vertices.empty() ? 0 : vertices.front().color;
    if (impl_->queue_opaque_primitive(command)) return;
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
    if (impl_->queue_opaque_primitive(command)) return;
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
    // With integer-positioned edges, OpenGL evaluates the first fragment at
    // x/y + 0.5. A flipped UV decreases from the edge coordinate, so integer
    // truncation would select u-1/v-1 on the first pixel. DuckStation's
    // GPU_HW starts flipped rectangles at texcoord+1 and subtracts the extent
    // at the far edge, preserving the PSX's first texel.
    const int u_start = u + (state.flip_x < 0 ? 1 : 0);
    const int v_start = v + (state.flip_y < 0 ? 1 : 0);
    const int u_end = state.flip_x < 0 ? u_start - width : u + width;
    const int v_end = state.flip_y < 0 ? v_start - height : v + height;
    const std::array<GpuHwVertex, 4> vertices{{
        {x, y, u_start, v_start, color},
        {x + width, y, u_end, v_start, color},
        {x, y + height, u_start, v_end, color},
        {x + width, y + height, u_end, v_end, color}}};
    auto command = make_command(1, vertices, state, false, true, raw_texture,
                                semi_transparent, clut);
    command.header[3] = color;
    if (impl_->queue_opaque_primitive(command)) return;
    impl_->execute_draw(command);
}

void GpuHardwareRenderer::sync_to_cpu(std::vector<uint8_t>& vram) { impl_->read_full(vram); }
void GpuHardwareRenderer::sync_rect_to_cpu(std::vector<uint8_t>& bytes,
        unsigned x, unsigned y, unsigned width, unsigned height) {
    impl_->flush_draw_batch();
    if (!width || !height) return;
    if (!impl_->make_current())
        throw std::runtime_error("could not activate PSX OpenGL context");
    const unsigned x0 = x & 1023u;
    const unsigned y0 = y % unsigned(impl_->vram_height);
    const unsigned first_width = std::min(width, unsigned(kVramWidth) - x0);
    const unsigned first_height = std::min(height,
        unsigned(impl_->vram_height) - y0);
    std::vector<uint16_t> pixels(size_t(width) * height);
    const auto read_piece = [&](unsigned src_x, unsigned src_y,
                                unsigned piece_width, unsigned piece_height,
                                unsigned dst_x, unsigned dst_y) {
        if (!piece_width || !piece_height) return;
        std::vector<uint16_t> piece(size_t(piece_width) * piece_height);
        glReadPixels(int(src_x), int(src_y), int(piece_width), int(piece_height),
                     GL_RED_INTEGER, GL_UNSIGNED_SHORT, piece.data());
        for (unsigned row = 0; row < piece_height; ++row)
            std::copy_n(piece.data() + size_t(row) * piece_width, piece_width,
                        pixels.data() + size_t(dst_y + row) * width + dst_x);
    };
    glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_TEXTURE_UPDATE_BARRIER_BIT);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, impl_->framebuffer[impl_->current_vram]);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    GLint previous_pack_alignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &previous_pack_alignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    read_piece(x0, y0, first_width, first_height, 0, 0);
    if (first_width < width)
        read_piece(0, y0, width - first_width, first_height, first_width, 0);
    if (first_height < height)
        read_piece(x0, 0, first_width, height - first_height, 0, first_height);
    if (first_width < width && first_height < height)
        read_piece(0, 0, width - first_width, height - first_height,
                   first_width, first_height);
    glPixelStorei(GL_PACK_ALIGNMENT, previous_pack_alignment);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    if (impl_->debug_gl && glGetError() != GL_NO_ERROR)
        throw std::runtime_error("PSX OpenGL partial VRAM readback failed");
    if (bytes.size() < size_t(kVramWidth) * impl_->vram_height * 2)
        bytes.resize(size_t(kVramWidth) * impl_->vram_height * 2);
    for (unsigned row = 0; row < height; ++row)
        for (unsigned col = 0; col < width; ++col) {
            const unsigned px = (x0 + col) & 1023u;
            const unsigned py = (y0 + row) % unsigned(impl_->vram_height);
            const uint16_t pixel = pixels[size_t(row) * width + col];
            const size_t at = (size_t(py) * kVramWidth + px) * 2;
            bytes[at] = uint8_t(pixel);
            bytes[at + 1] = uint8_t(pixel >> 8);
        }
}
uint64_t GpuHardwareRenderer::vram_write_generation() {
    impl_->flush_draw_batch();
    return impl_->vram_write_generation;
}
void GpuHardwareRenderer::sync_from_cpu(std::span<const uint8_t> vram) { impl_->upload_full(vram); }

void GpuHardwareRenderer::upload_rect(std::span<const uint8_t> bytes, unsigned x,
        unsigned y, unsigned width, unsigned height) {
    impl_->flush_draw_batch();
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
    ++impl_->vram_write_generation;
    const int x0 = int(x & 1023u);
    const int y0 = int(y % unsigned(impl_->vram_height));
    impl_->invalidate_texture_cache_region(x0, y0,
        x0 + int(first_width) - 1, y0 + int(first_height) - 1);
    if (first_width < width)
        impl_->invalidate_texture_cache_region(0, y0,
            int(width - first_width) - 1, y0 + int(first_height) - 1);
    if (first_height < height)
        impl_->invalidate_texture_cache_region(x0, 0,
            x0 + int(first_width) - 1, int(height - first_height) - 1);
    if (first_width < width && first_height < height)
        impl_->invalidate_texture_cache_region(0, 0,
            int(width - first_width) - 1, int(height - first_height) - 1);
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void GpuHardwareRenderer::copy_rect(int sx, int sy, int dx, int dy, int width,
        int height, const GpuHwState& state) {
    if (width <= 0 || height <= 0) return;
    sx &= 1023;
    const int destination_x = dx & 1023;
    sy = (sy % impl_->vram_height + impl_->vram_height) % impl_->vram_height;
    dy = (dy % impl_->vram_height + impl_->vram_height) % impl_->vram_height;

    impl_->flush_draw_batch();
    if (!impl_->make_current()) throw std::runtime_error("could not activate PSX OpenGL context");

    // Snapshot the complete source rectangle before writing any destination
    // pixels. This preserves overlap semantics even when either rectangle
    // wraps at the VRAM edge and the copy needs several dispatches.
    glFlush();
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
    int source_row = 0;
    while (source_row < height) {
        const int source_y = (sy + source_row) % impl_->vram_height;
        const int source_height = std::min(height - source_row,
                                           impl_->vram_height - source_y);
        int source_column = 0;
        while (source_column < width) {
            const int source_x = (sx + source_column) & 1023;
            const int source_width = std::min(width - source_column,
                                              kVramWidth - source_x);
            glCopyImageSubData(impl_->vram[impl_->current_vram], GL_TEXTURE_2D, 0,
                               source_x, source_y, 0,
                               impl_->copy_scratch, GL_TEXTURE_2D, 0,
                               source_column, source_row, 0,
                               source_width, source_height, 1);
            source_column += source_width;
        }
        source_row += source_height;
    }
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

    // GP0(80h) is a VRAM-to-VRAM transfer, so keep both the source snapshot
    // and the destination writes on the device. Split at VRAM boundaries;
    // ping-ponging the integer VRAM images makes overlapping copies safe.
    int row = 0;
    while (row < height) {
        const int chunk_height = std::min(height - row, impl_->vram_height - dy);
        int row_destination_x = destination_x;
        int column = 0;
        while (column < width) {
            const int chunk_width = std::min(width - column,
                                             kVramWidth - row_destination_x);
            ShaderCommand command{};
            command.header[0] = 4;
            command.header[2] = (state.set_mask ? 16u : 0u) |
                                (state.check_mask ? 32u : 0u);
            command.bounds[0] = row_destination_x;
            command.bounds[1] = dy;
            command.bounds[2] = row_destination_x + chunk_width - 1;
            command.bounds[3] = dy + chunk_height - 1;
            command.vertices[0].xyuv[0] = column;
            command.vertices[0].xyuv[1] = row;
            impl_->execute(command, impl_->copy_scratch);
            column += chunk_width;
            row_destination_x = 0;
        }
        row += chunk_height;
        dy = 0;
    }
}

std::vector<uint8_t> GpuHardwareRenderer::scanout_rgb(
    unsigned width, unsigned height, bool display_enabled, bool rgb24,
    unsigned display_start_x, unsigned display_start_y, unsigned vram_height,
    int source_x, int source_y, int destination_x, int destination_y,
    int copy_width, int copy_height, bool asynchronous, bool direct_present) {
    return impl_->scanout_rgb(width, height, display_enabled, rgb24,
                              display_start_x, display_start_y, vram_height,
                              source_x, source_y, destination_x, destination_y,
                              copy_width, copy_height, asynchronous, direct_present);
}

bool GpuHardwareRenderer::present_scanout() { return impl_->present_scanout(); }

const std::string& GpuHardwareRenderer::renderer_name() const noexcept { return impl_->renderer; }

} // namespace mamerecomp::psx
