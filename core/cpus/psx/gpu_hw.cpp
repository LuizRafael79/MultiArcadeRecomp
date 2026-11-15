#define GL_GLEXT_PROTOTYPES 1
#include "gpu_hw.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace multiarcaderecomp::psx {
namespace {
constexpr int kVramWidth = 1024;
constexpr int kMinVramHeight = 512;
constexpr int kMaxVramHeight = 1024;
constexpr size_t kCommandRingSlots = 4096;
constexpr size_t kCommandBatchSlots = 256;
constexpr uint32_t kMaxBatchPrimitives = 65535;
constexpr uint32_t kMaxBatchDepthLayers = kMaxBatchPrimitives * 2;
constexpr size_t kMaxBatchScanlineRows = 1'000'000;
constexpr uint32_t kTextureCacheLayers = 64;
constexpr size_t kGpuTimingQuerySlots = 64;

enum class GpuTimingKind : size_t {
    BatchDraw,
    ScanlineSetup,
    ImmediateDraw,
    RasterCompute,
    FillCompute,
    CopyCompute,
    TextureCache,
    Scanout,
    Count
};

constexpr size_t kGpuTimingKindCount = static_cast<size_t>(GpuTimingKind::Count);
constexpr const char* kGpuTimingKindNames[kGpuTimingKindCount]{
    "batch_draw", "scanline_setup", "immediate_draw", "raster_compute",
    "fill_compute", "copy_compute", "texture_cache", "scanout"};

enum class HostTimingKind : size_t {
    DrawSubmit,
    BatchFlush,
    Scanout,
    Present,
    Readback,
    Upload,
    Copy,
    Count
};

constexpr size_t kHostTimingKindCount = static_cast<size_t>(HostTimingKind::Count);
constexpr const char* kHostTimingKindNames[kHostTimingKindCount]{
    "draw_submit", "batch_flush", "scanout", "present", "readback",
    "upload", "copy"};

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
    int32_t batch_scanline[4]{};
};
static_assert(sizeof(ShaderCommand) == 240);

struct alignas(16) BatchScanline {
    int32_t start[8]{};
    int32_t step[8]{};
    int32_t span[4]{};
};
static_assert(sizeof(BatchScanline) == 80);

struct alignas(16) BatchPolygonJob {
    int32_t command_index = 0;
    int32_t padding[3]{};
};
static_assert(sizeof(BatchPolygonJob) == 16);

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
    uint32_t command_index = 0;
};
static_assert(sizeof(BatchVertex) == 72);

struct alignas(16) PolylineVertex {
    int32_t position[2]{};
    uint32_t color = 0;
    uint32_t padding = 0;
};
static_assert(sizeof(PolylineVertex) == 16);

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

struct alignas(16) TextureCacheJob {
    uint32_t layer = 0;
    uint32_t texture_page = 0;
    uint32_t gpu_type = 0;
    uint32_t padding = 0;
    int32_t window_state[4]{};
};
static_assert(sizeof(TextureCacheJob) == 32);

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
    struct HostCpuTimer {
        Impl* owner = nullptr;
        HostTimingKind kind = HostTimingKind::DrawSubmit;
        std::chrono::steady_clock::time_point start{};

        HostCpuTimer(Impl& impl, HostTimingKind timing_kind)
            : owner(&impl), kind(timing_kind),
              start(impl.host_cpu_timing_enabled
                  ? std::chrono::steady_clock::now()
                  : std::chrono::steady_clock::time_point{}) {}
        HostCpuTimer(const HostCpuTimer&) = delete;
        HostCpuTimer& operator=(const HostCpuTimer&) = delete;
        ~HostCpuTimer() {
            if (!owner || !owner->host_cpu_timing_enabled) return;
            const size_t index = static_cast<size_t>(kind);
            owner->host_cpu_timing_ns[index] += static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - start).count());
            ++owner->host_cpu_timing_calls[index];
        }
    };

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
    GLuint batch_scanline_setup_program = 0;
    GLuint texture_cache_program = 0;
    GLuint polyline_program = 0;
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
    GLuint batch_command_buffer = 0;
    GLuint texture_cache_job_buffer = 0;
    GLuint batch_scanline_buffer = 0;
    GLuint batch_scanline_job_buffer = 0;
    GLuint polyline_buffer = 0;
    GLuint command_buffer = 0;
    std::array<GLsync, kCommandRingSlots / kCommandBatchSlots> command_batch_fences{};
    std::array<GLuint, kGpuTimingQuerySlots> batch_gpu_timing_queries{};
    std::array<bool, kGpuTimingQuerySlots> batch_gpu_timing_pending{};
    std::array<GpuTimingKind, kGpuTimingQuerySlots> batch_gpu_timing_kinds{};
    std::array<uint64_t, kGpuTimingQuerySlots> batch_gpu_timing_units{};
    size_t next_command_slot = 0;
    size_t next_batch_gpu_timing_query = 0;
    std::array<uint64_t, kGpuTimingKindCount> gpu_timing_total_ns{};
    std::array<uint64_t, kGpuTimingKindCount> gpu_timing_samples{};
    std::array<uint64_t, kGpuTimingKindCount> gpu_timing_units{};
    std::array<uint64_t, kGpuTimingKindCount> gpu_timing_dropped{};
    const bool host_cpu_timing_enabled =
        std::getenv("MULTIARCADERECOMP_PSX_GPU_CPU_PROFILE") != nullptr;
    std::array<uint64_t, kHostTimingKindCount> host_cpu_timing_ns{};
    std::array<uint64_t, kHostTimingKindCount> host_cpu_timing_calls{};
    size_t command_stride = sizeof(ShaderCommand);
    GLint origin_location = -1;
    GLint vram_height_location = -1;
    GLint draw_vram_height_location = -1;
    GLint batch_vram_height_location = -1;
    GLint batch_polygon_job_count_location = -1;
    GLint scanout_vram_location = -1;
    GLint scanout_display_start_location = -1;
    GLint scanout_source_offset_location = -1;
    GLint scanout_destination_offset_location = -1;
    GLint scanout_copy_size_location = -1;
    GLint scanout_vram_height_location = -1;
    GLint scanout_rgb24_location = -1;
    GLint scanout_display_enabled_location = -1;
    GLint texture_cache_vram_height_location = -1;
    GLint batch_source_location = -1;
    GLint batch_texture_cache_location = -1;
    GLint polyline_vertex_count_location = -1;
    GLint polyline_draw_area_location = -1;
    GLint polyline_draw_offset_location = -1;
    GLint polyline_vram_height_location = -1;
    GLint polyline_flags_location = -1;
    GLint polyline_abr_location = -1;
    GLint polyline_interlaced_location = -1;
    GLint polyline_active_line_lsb_location = -1;
    int vram_height = kMinVramHeight;
    unsigned scanout_width = 0;
    unsigned scanout_height = 0;
    unsigned current_vram = 0;
    bool vram_textures_diverged = false;
    std::array<int, 4> last_dirty_bounds{};
    uint64_t vram_write_generation = 0;
    std::vector<BatchVertex> pending_batch_vertices;
    std::vector<ShaderCommand> pending_batch_commands;
    std::vector<BatchPolygonJob> pending_batch_polygon_jobs;
    size_t pending_batch_scanline_count = 0;
    int pending_batch_max_scanline_rows = 0;
    std::vector<PendingBatchTexture> pending_batch_textures;
    std::unordered_set<TextureCacheKey, TextureCacheKeyHash> pending_batch_texture_keys;
    std::array<TextureCacheEntry, kTextureCacheLayers> texture_cache_entries{};
    std::unordered_map<TextureCacheKey, uint32_t, TextureCacheKeyHash> texture_cache_lookup;
    uint64_t texture_cache_clock = 0;
    uint64_t texture_cache_hits = 0;
    uint64_t texture_cache_generations = 0;
    uint64_t texture_cache_dispatches = 0;
    uint64_t texture_cache_max_pages_per_dispatch = 0;
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
    bool debug_gl = std::getenv("MULTIARCADERECOMP_PSX_GPU_GL_CHECK") != nullptr;
    bool batching_enabled = [] {
        const char* value = std::getenv("MULTIARCADERECOMP_PSX_GPU_BATCHING");
        return !value || std::strcmp(value, "0") != 0;
    }();
    bool texture_cache_enabled = [] {
        const char* value = std::getenv("MULTIARCADERECOMP_PSX_GPU_TEXTURE_CACHE");
        return !value || std::strcmp(value, "0") != 0;
    }();
    bool gpu_timing_enabled = [] {
        const char* value = std::getenv("MULTIARCADERECOMP_PSX_GPU_TIMING");
        return value && std::strcmp(value, "0") != 0;
    }();
    uint32_t batch_primitive_limit = [] {
        const char* value = std::getenv("MULTIARCADERECOMP_PSX_GPU_BATCH_LIMIT");
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
        if (gpu_timing_enabled) {
            // Collect the opt-in diagnostics after the emulation workload so
            // query reads never serialize ordinary batch submission.
            glFinish();
            for (size_t slot = 0; slot < batch_gpu_timing_queries.size(); ++slot)
                collect_gpu_timing(slot, true);
            for (size_t kind = 0; kind < kGpuTimingKindCount; ++kind) {
                if (!gpu_timing_samples[kind] && !gpu_timing_dropped[kind]) continue;
                std::cerr << "PSX OpenGL GPU timing kind=" << kGpuTimingKindNames[kind]
                          << " samples=" << gpu_timing_samples[kind]
                          << " units=" << gpu_timing_units[kind]
                          << " total_ms="
                          << (double(gpu_timing_total_ns[kind]) / 1'000'000.0)
                          << " average_us=";
                if (gpu_timing_samples[kind])
                    std::cerr << (double(gpu_timing_total_ns[kind]) /
                                  double(gpu_timing_samples[kind]) / 1'000.0);
                else
                    std::cerr << 0.0;
                std::cerr << " dropped=" << gpu_timing_dropped[kind] << '\n';
            }
        }
        if (program) glDeleteProgram(program);
        if (scanout_program) glDeleteProgram(scanout_program);
        if (draw_program) glDeleteProgram(draw_program);
        if (batch_draw_program) glDeleteProgram(batch_draw_program);
        if (batch_scanline_setup_program) glDeleteProgram(batch_scanline_setup_program);
        if (texture_cache_program) glDeleteProgram(texture_cache_program);
        if (polyline_program) glDeleteProgram(polyline_program);
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
        if (batch_command_buffer) glDeleteBuffers(1, &batch_command_buffer);
        if (texture_cache_job_buffer) glDeleteBuffers(1, &texture_cache_job_buffer);
        if (batch_scanline_buffer) glDeleteBuffers(1, &batch_scanline_buffer);
        if (batch_scanline_job_buffer) glDeleteBuffers(1, &batch_scanline_job_buffer);
        if (polyline_buffer) glDeleteBuffers(1, &polyline_buffer);
        if (command_buffer) glDeleteBuffers(1, &command_buffer);
        for (GLsync fence : command_batch_fences)
            if (fence) glDeleteSync(fence);
        if (batch_gpu_timing_queries[0])
            glDeleteQueries(static_cast<GLsizei>(batch_gpu_timing_queries.size()),
                            batch_gpu_timing_queries.data());
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

    void collect_gpu_timing(size_t slot, bool wait) {
        if (!batch_gpu_timing_pending[slot]) return;
        if (!wait) {
            GLint available = GL_FALSE;
            glGetQueryObjectiv(batch_gpu_timing_queries[slot],
                               GL_QUERY_RESULT_AVAILABLE, &available);
            if (available != GL_TRUE) return;
        }
        GLuint64 elapsed_ns = 0;
        glGetQueryObjectui64v(batch_gpu_timing_queries[slot], GL_QUERY_RESULT,
                              &elapsed_ns);
        const size_t kind = static_cast<size_t>(batch_gpu_timing_kinds[slot]);
        gpu_timing_total_ns[kind] += elapsed_ns;
        gpu_timing_units[kind] += batch_gpu_timing_units[slot];
        ++gpu_timing_samples[kind];
        batch_gpu_timing_pending[slot] = false;
    }

    size_t begin_gpu_timing(GpuTimingKind kind) {
        if (!gpu_timing_enabled) return kGpuTimingQuerySlots;
        for (size_t attempt = 0; attempt < batch_gpu_timing_queries.size(); ++attempt) {
            const size_t slot = (next_batch_gpu_timing_query + attempt) %
                                batch_gpu_timing_queries.size();
            collect_gpu_timing(slot, false);
            if (batch_gpu_timing_pending[slot]) continue;
            glBeginQuery(GL_TIME_ELAPSED, batch_gpu_timing_queries[slot]);
            batch_gpu_timing_kinds[slot] = kind;
            next_batch_gpu_timing_query = (slot + 1) % batch_gpu_timing_queries.size();
            return slot;
        }
        ++gpu_timing_dropped[static_cast<size_t>(kind)];
        return kGpuTimingQuerySlots;
    }

    void end_gpu_timing(size_t slot, uint64_t units = 1) {
        if (slot >= batch_gpu_timing_queries.size()) return;
        glEndQuery(GL_TIME_ELAPSED);
        batch_gpu_timing_pending[slot] = true;
        batch_gpu_timing_units[slot] = units;
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
            window = SDL_CreateWindow("MultiArcadeRecomp PSX GPU", 32, 32,
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
        program = compile_compute_program(read_shader_source(MULTIARCADERECOMP_PSX_GPU_SHADER_PATH), init_error);
        if (!program) throw std::runtime_error(init_error);
        scanout_program = compile_compute_program(
            read_shader_source(MULTIARCADERECOMP_PSX_GPU_SCANOUT_SHADER_PATH), init_error);
        if (!scanout_program) throw std::runtime_error(init_error);
        draw_program = compile_graphics_program(
            read_shader_source(MULTIARCADERECOMP_PSX_GPU_VERTEX_SHADER_PATH),
            read_shader_source(MULTIARCADERECOMP_PSX_GPU_FRAGMENT_SHADER_PATH), init_error);
        if (!draw_program) throw std::runtime_error(init_error);
        std::string batch_fragment_source =
            read_shader_source(MULTIARCADERECOMP_PSX_GPU_BATCH_FRAGMENT_SHADER_PATH);
        if (!texture_cache_enabled) {
            const size_t version_end = batch_fragment_source.find('\n');
            batch_fragment_source.insert(version_end == std::string::npos
                    ? batch_fragment_source.size() : version_end + 1,
                "#define MULTIARCADERECOMP_PSX_NO_TEXTURE_CACHE\n");
        }
        batch_draw_program = compile_graphics_program(
            read_shader_source(MULTIARCADERECOMP_PSX_GPU_BATCH_VERTEX_SHADER_PATH),
            batch_fragment_source, init_error);
        if (!batch_draw_program) throw std::runtime_error(init_error);
        batch_scanline_setup_program = compile_compute_program(
            read_shader_source(MULTIARCADERECOMP_PSX_GPU_BATCH_SCANLINE_SHADER_PATH), init_error);
        if (!batch_scanline_setup_program) throw std::runtime_error(init_error);
        if (texture_cache_enabled) {
            texture_cache_program = compile_compute_program(
                read_shader_source(MULTIARCADERECOMP_PSX_GPU_TEXTURE_CACHE_SHADER_PATH), init_error);
            if (!texture_cache_program) throw std::runtime_error(init_error);
        }
        polyline_program = compile_compute_program(
            read_shader_source(MULTIARCADERECOMP_PSX_GPU_POLYLINE_SHADER_PATH), init_error);
        if (!polyline_program) throw std::runtime_error(init_error);
        origin_location = glGetUniformLocation(program, "uDispatchOrigin");
        vram_height_location = glGetUniformLocation(program, "uVramHeight");
        draw_vram_height_location = glGetUniformLocation(draw_program, "uVramHeight");
        batch_vram_height_location = glGetUniformLocation(batch_draw_program, "uVramHeight");
        batch_polygon_job_count_location = glGetUniformLocation(
            batch_scanline_setup_program, "uPolygonJobCount");
        scanout_vram_location = glGetUniformLocation(scanout_program, "uVram");
        scanout_display_start_location = glGetUniformLocation(scanout_program, "uDisplayStart");
        scanout_source_offset_location = glGetUniformLocation(scanout_program, "uSourceOffset");
        scanout_destination_offset_location = glGetUniformLocation(scanout_program, "uDestinationOffset");
        scanout_copy_size_location = glGetUniformLocation(scanout_program, "uCopySize");
        scanout_vram_height_location = glGetUniformLocation(scanout_program, "uVramHeight");
        scanout_rgb24_location = glGetUniformLocation(scanout_program, "uRgb24");
        scanout_display_enabled_location = glGetUniformLocation(scanout_program, "uDisplayEnabled");
        batch_texture_cache_location = glGetUniformLocation(batch_draw_program, "uTextureCache");
        batch_source_location = glGetUniformLocation(batch_draw_program, "uVram");
        polyline_vertex_count_location = glGetUniformLocation(polyline_program, "uVertexCount");
        polyline_draw_area_location = glGetUniformLocation(polyline_program, "uDrawArea");
        polyline_draw_offset_location = glGetUniformLocation(polyline_program, "uDrawOffset");
        polyline_vram_height_location = glGetUniformLocation(polyline_program, "uVramHeight");
        polyline_flags_location = glGetUniformLocation(polyline_program, "uFlags");
        polyline_abr_location = glGetUniformLocation(polyline_program, "uAbr");
        polyline_interlaced_location = glGetUniformLocation(polyline_program, "uInterlaced");
        polyline_active_line_lsb_location = glGetUniformLocation(polyline_program, "uActiveLineLsb");
        if (texture_cache_enabled) {
            texture_cache_vram_height_location = glGetUniformLocation(texture_cache_program, "uVramHeight");
        }
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
        if (texture_cache_enabled) {
            glGenTextures(1, &texture_cache_texture);
            glBindTexture(GL_TEXTURE_2D_ARRAY, texture_cache_texture);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexStorage3D(GL_TEXTURE_2D_ARRAY, 1, GL_R16UI, 256, 256,
                           static_cast<GLsizei>(kTextureCacheLayers));
        }
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
        if (gpu_timing_enabled)
            glGenQueries(static_cast<GLsizei>(batch_gpu_timing_queries.size()),
                         batch_gpu_timing_queries.data());
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
        glEnableVertexAttribArray(12);
        glVertexAttribIPointer(12, 1, GL_UNSIGNED_INT, sizeof(BatchVertex),
                               reinterpret_cast<const void*>(offsetof(BatchVertex, command_index)));
        glGenBuffers(1, &batch_command_buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, batch_command_buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_STREAM_DRAW);
        glGenBuffers(1, &texture_cache_job_buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, texture_cache_job_buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_STREAM_DRAW);
        glGenBuffers(1, &batch_scanline_buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, batch_scanline_buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_STREAM_DRAW);
        glGenBuffers(1, &batch_scanline_job_buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, batch_scanline_job_buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_STREAM_DRAW);
        glGenBuffers(1, &polyline_buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, polyline_buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER, 0, nullptr, GL_STREAM_DRAW);
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
        glUniform1i(scanout_vram_location, 0);
        glUseProgram(draw_program);
        glUniform1i(glGetUniformLocation(draw_program, "uSource"), 0);
        glUseProgram(batch_draw_program);
        glUniform1i(batch_texture_cache_location, 1);
        glUniform1i(batch_source_location, 0);
        if (texture_cache_enabled) {
            glUseProgram(texture_cache_program);
            glUniform1i(glGetUniformLocation(texture_cache_program, "uSource"), 0);
        }
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
        // ABR selects destination blending; it does not change the source
        // texture indices/colors stored in a cache layer. Keep it in the draw
        // command, but exclude it from the cached texture identity.
        const uint32_t abr_mask = command.texture[2] == 2u ? 0x60u : 0x180u;
        const uint32_t texture_page = command.texture[0] & ~abr_mask;
        return {texture_page, command.texture[2],
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

    uint32_t ensure_texture_cache_layer(const TextureCacheKey& key,
                                        std::array<bool, kTextureCacheLayers>& protected_layers,
                                        std::array<bool, kTextureCacheLayers>& queued,
                                        std::vector<TextureCacheJob>& jobs) {
        uint32_t layer = 0;
        auto found = texture_cache_lookup.find(key);
        if (found != texture_cache_lookup.end()) {
            layer = found->second;
            auto& entry = texture_cache_entries[layer];
            protected_layers[layer] = true;
            entry.last_used = ++texture_cache_clock;
            if (entry.valid) {
                ++texture_cache_hits;
                return layer;
            }
        } else {
            uint32_t replacement = 0;
            uint64_t oldest = UINT64_MAX;
            bool found_unprotected = false;
            for (uint32_t i = 0; i < kTextureCacheLayers; ++i) {
                const auto& entry = texture_cache_entries[i];
                if (protected_layers[i]) continue;
                if (!entry.occupied) {
                    replacement = i;
                    found_unprotected = true;
                    break;
                }
                if (entry.last_used < oldest) {
                    oldest = entry.last_used;
                    replacement = i;
                    found_unprotected = true;
                }
            }
            if (!found_unprotected)
                throw std::runtime_error("PSX batch exceeded protected texture-cache layers");
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
            protected_layers[layer] = true;
        }

        auto& entry = texture_cache_entries[layer];
        if (queued[layer]) {
            ++texture_cache_hits;
            return layer;
        }
        TextureCacheJob job{};
        job.layer = layer;
        job.texture_page = key.texture_page;
        job.gpu_type = key.gpu_type;
        job.window_state[0] = key.window_x;
        job.window_state[1] = key.window_y;
        job.window_state[2] = key.window_mask_x;
        job.window_state[3] = key.window_mask_y;
        jobs.push_back(job);
        queued[layer] = true;
        return layer;
    }

    void prepare_batch_texture_cache() {
        if (!texture_cache_enabled || pending_batch_textures.empty()) return;
        std::array<bool, kTextureCacheLayers> protected_layers{};
        std::array<bool, kTextureCacheLayers> queued{};
        std::vector<TextureCacheJob> jobs;
        jobs.reserve(std::min(pending_batch_textures.size(),
                              static_cast<size_t>(kTextureCacheLayers)));
        // Preserve all resident entries referenced by this batch before an
        // LRU miss can choose a replacement slot.
        for (const auto& pending : pending_batch_textures) {
            const auto found = texture_cache_lookup.find(pending.key);
            if (found != texture_cache_lookup.end())
                protected_layers[found->second] = true;
        }
        for (const auto& pending : pending_batch_textures) {
            const uint32_t layer = ensure_texture_cache_layer(
                pending.key, protected_layers, queued, jobs);
            for (uint32_t i = 0; i < pending.vertex_count; ++i)
                pending_batch_vertices[pending.first_vertex + i].texture_layer = layer;
        }
        if (!jobs.empty()) {
            glUseProgram(texture_cache_program);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, vram[current_vram]);
            glBindImageTexture(1, texture_cache_texture, 0, GL_TRUE, 0,
                               GL_WRITE_ONLY, GL_R16UI);
            glUniform1i(texture_cache_vram_height_location, vram_height);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, texture_cache_job_buffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                         static_cast<GLsizeiptr>(jobs.size() * sizeof(TextureCacheJob)),
                         jobs.data(), GL_STREAM_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, texture_cache_job_buffer);
            const size_t timing_query = begin_gpu_timing(GpuTimingKind::TextureCache);
            glDispatchCompute(32, 32, static_cast<GLuint>(jobs.size()));
            end_gpu_timing(timing_query, jobs.size());
            ++texture_cache_dispatches;
            texture_cache_max_pages_per_dispatch = std::max<uint64_t>(
                texture_cache_max_pages_per_dispatch, jobs.size());
            for (const auto& job : jobs) {
                texture_cache_entries[job.layer].valid = true;
                ++texture_cache_generations;
            }
        }
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
    }

    void append_batch_polygon_job(ShaderCommand& command,
                                  uint32_t command_index,
                                  int top, int bottom) {
        if (command.header[0] != 0u) return;
        command.batch_scanline[0] = static_cast<int32_t>(pending_batch_scanline_count);
        command.batch_scanline[1] = top;
        command.batch_scanline[2] = bottom - top + 1;
        pending_batch_polygon_jobs.push_back({static_cast<int32_t>(command_index), {0, 0, 0}});
        pending_batch_scanline_count += static_cast<size_t>(command.batch_scanline[2]);
        pending_batch_max_scanline_rows = std::max(
            pending_batch_max_scanline_rows, command.batch_scanline[2]);
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
        bool must_flush = false;
        const bool uses_texture_cache = texture_cache_enabled && (flags & 2u) != 0u;
        const TextureCacheKey texture_key = uses_texture_cache
            ? make_texture_cache_key(command) : TextureCacheKey{};
        if (uses_texture_cache && !pending_batch_texture_keys.contains(texture_key) &&
            pending_batch_texture_keys.size() >= kTextureCacheLayers)
            must_flush = true;
        if (command.header[0] == 0u &&
            pending_batch_scanline_count + static_cast<size_t>(bottom - top + 1) >
                kMaxBatchScanlineRows)
            must_flush = true;
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
            pending_batch_draw_area[0] = std::min(pending_batch_draw_area[0], draw_area[0]);
            pending_batch_draw_area[1] = std::min(pending_batch_draw_area[1], draw_area[1]);
            pending_batch_draw_area[2] = std::max(pending_batch_draw_area[2], draw_area[2]);
            pending_batch_draw_area[3] = std::max(pending_batch_draw_area[3], draw_area[3]);
            pending_batch_dirty_bounds[0] = std::min(pending_batch_dirty_bounds[0], left);
            pending_batch_dirty_bounds[1] = std::min(pending_batch_dirty_bounds[1], top);
            pending_batch_dirty_bounds[2] = std::max(pending_batch_dirty_bounds[2], right);
            pending_batch_dirty_bounds[3] = std::max(pending_batch_dirty_bounds[3], bottom);
        }
        ++pending_batch_primitive_count;
        const uint32_t set_mask = (flags & 16u) ? 0x8000u : 0u;
        const uint32_t command_index = static_cast<uint32_t>(pending_batch_commands.size());
        ShaderCommand batch_command = command;
        append_batch_polygon_job(batch_command, command_index, top, bottom);
        pending_batch_commands.push_back(batch_command);
        const size_t first_vertex = pending_batch_vertices.size();
        const unsigned output_count = 6;
        if (uses_texture_cache) {
            pending_batch_texture_keys.insert(texture_key);
            pending_batch_textures.push_back({first_vertex,
                texture_key, output_count});
        }
        // Polygons use a screen-aligned bounding quad. The fragment shader
        // evaluates the original GP0 vertices with the PSX's 16.16 scanline
        // stepping, avoiding hardware triangle interpolation differences.
        // Rectangle primitives keep their native two-triangle geometry.
        const std::array<std::array<int, 2>, 6> polygon_positions{{
            {{left, top}}, {{right + 1, top}}, {{left, bottom + 1}},
            {{left, bottom + 1}}, {{right + 1, top}}, {{right + 1, bottom + 1}}}};
        const unsigned indices[6] = {0, 1, 2, 2, 1, 3};
        const bool polygon = command.header[0] == 0u;
        for (unsigned i = 0; i < output_count; ++i) {
            BatchVertex vertex{};
            if (command.header[0] == 0u) {
                vertex.position[0] = polygon_positions[i][0];
                vertex.position[1] = polygon_positions[i][1];
            } else {
                const auto& source = command.vertices[vertex_count == 4 ? indices[i] : i];
                vertex.position[0] = source.xyuv[0];
                vertex.position[1] = source.xyuv[1];
            }
            const auto& source = command.vertices[vertex_count == 4 ? indices[i] :
                                                   std::min(i, vertex_count - 1)];
            vertex.uv[0] = source.xyuv[2];
            vertex.uv[1] = source.xyuv[3];
            vertex.color = (flags & (1u | 2u)) ? source.color : command.header[3];
            const uint32_t vertex_depth = pending_batch_depth_count +
                (polygon || vertex_count != 4 ? 1u : (i < 3 ? 1u : 2u));
            vertex.depth = static_cast<float>(vertex_depth) /
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
            vertex.command_index = command_index;
            pending_batch_vertices.push_back(vertex);
        }
        pending_batch_depth_count += !polygon && vertex_count == 4 ? 2u : 1u;
        return true;
    }

    void flush_draw_batch() {
        if (pending_batch_vertices.empty()) return;
        HostCpuTimer host_timer(*this, HostTimingKind::BatchFlush);
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
        if (texture_cache_enabled) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D_ARRAY, texture_cache_texture);
        }
        glBindVertexArray(batch_vertex_array);
        glBindBuffer(GL_ARRAY_BUFFER, batch_vertex_buffer);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(pending_batch_vertices.size() * sizeof(BatchVertex)),
                     pending_batch_vertices.data(), GL_STREAM_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, batch_command_buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     static_cast<GLsizeiptr>(pending_batch_commands.size() * sizeof(ShaderCommand)),
                     pending_batch_commands.data(), GL_STREAM_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, batch_command_buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, batch_scanline_job_buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     static_cast<GLsizeiptr>(pending_batch_polygon_jobs.size() *
                                             sizeof(BatchPolygonJob)),
                     pending_batch_polygon_jobs.data(), GL_STREAM_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, batch_scanline_job_buffer);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, batch_scanline_buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     static_cast<GLsizeiptr>(pending_batch_scanline_count * sizeof(BatchScanline)),
                     nullptr, GL_STREAM_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, batch_scanline_buffer);
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT | GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT);
        if (!pending_batch_polygon_jobs.empty()) {
            glUseProgram(batch_scanline_setup_program);
            glUniform1i(batch_polygon_job_count_location,
                        static_cast<GLint>(pending_batch_polygon_jobs.size()));
            const size_t setup_query = begin_gpu_timing(GpuTimingKind::ScanlineSetup);
            glDispatchCompute(static_cast<GLuint>(pending_batch_polygon_jobs.size()),
                              static_cast<GLuint>((pending_batch_max_scanline_rows + 63) / 64),
                              1);
            end_gpu_timing(setup_query, pending_batch_scanline_count);
            glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        }
        glUseProgram(batch_draw_program);
        const size_t timing_query = begin_gpu_timing(GpuTimingKind::BatchDraw);
        glDrawArrays(GL_TRIANGLES, 0,
                     static_cast<GLsizei>(pending_batch_vertices.size()));
        end_gpu_timing(timing_query, pending_batch_primitive_count);
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
        pending_batch_commands.clear();
        pending_batch_polygon_jobs.clear();
        pending_batch_scanline_count = 0;
        pending_batch_max_scanline_rows = 0;
        pending_batch_textures.clear();
        pending_batch_texture_keys.clear();
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
        const GpuTimingKind timing_kind = command.header[0] == 2u
            ? GpuTimingKind::FillCompute
            : command.header[0] == 4u ? GpuTimingKind::CopyCompute
                                      : GpuTimingKind::RasterCompute;
        const size_t timing_query = begin_gpu_timing(timing_kind);
        glDispatchCompute((max_x - min_x + 1 + 7) / 8,
                          (max_y - min_y + 1 + 7) / 8, 1);
        end_gpu_timing(timing_query);
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

    void execute_polyline(std::span<const GpuHwVertex> vertices,
                          const GpuHwState& state, bool gouraud,
                          bool semi_transparent) {
        flush_draw_batch();
        if (vertices.size() < 2) return;
        if (!make_current())
            throw std::runtime_error("could not activate PSX OpenGL context");

        int min_x = kVramWidth - 1, min_y = vram_height - 1;
        int max_x = 0, max_y = 0;
        std::array<PolylineVertex, 2> inline_upload{};
        std::vector<PolylineVertex> overflow_upload;
        std::span<const PolylineVertex> upload;
        if (vertices.size() <= inline_upload.size()) {
            upload = std::span<PolylineVertex>(inline_upload.data(), vertices.size());
        } else {
            overflow_upload.reserve(vertices.size());
        }
        for (size_t i = 0; i < vertices.size(); ++i) {
            const auto& vertex = vertices[i];
            const int x = vertex.x + state.offset_x;
            const int y = vertex.y + state.offset_y;
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
            const PolylineVertex uploaded{{vertex.x, vertex.y}, vertex.color, 0};
            if (vertices.size() <= inline_upload.size())
                inline_upload[i] = uploaded;
            else
                overflow_upload.push_back(uploaded);
        }
        if (vertices.size() > inline_upload.size()) upload = overflow_upload;
        min_x = std::clamp(min_x, 0, kVramWidth - 1);
        min_y = std::clamp(min_y, 0, vram_height - 1);
        max_x = std::clamp(max_x, 0, kVramWidth - 1);
        max_y = std::clamp(max_y, 0, vram_height - 1);
        if (min_x > max_x || min_y > max_y) return;

        glUseProgram(polyline_program);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, polyline_buffer);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     static_cast<GLsizeiptr>(upload.size() * sizeof(PolylineVertex)),
                     upload.data(), GL_STREAM_DRAW);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, polyline_buffer);
        glBindImageTexture(0, vram[current_vram], 0, GL_FALSE, 0,
                           GL_READ_WRITE, GL_R16UI);
        glUniform1i(polyline_vertex_count_location,
                    static_cast<GLint>(vertices.size()));
        glUniform4i(polyline_draw_area_location, state.draw_x1, state.draw_y1,
                    state.draw_x2, state.draw_y2);
        glUniform2i(polyline_draw_offset_location, state.offset_x, state.offset_y);
        glUniform1i(polyline_vram_height_location, vram_height);
        const uint32_t flags = (gouraud ? 1u : 0u) |
            (state.check_mask ? 2u : 0u) | (state.dither_enable ? 4u : 0u) |
            (semi_transparent ? 8u : 0u) | (state.set_mask ? 16u : 0u);
        glUniform1ui(polyline_flags_location, flags);
        glUniform1ui(polyline_abr_location, state.gpu_type == 2u
            ? ((state.texture_page >> 5) & 3u)
            : ((state.texture_page >> 7) & 3u));
        glUniform1i(polyline_interlaced_location,
                    state.interlaced_rendering ? 1 : 0);
        glUniform1ui(polyline_active_line_lsb_location,
                     state.active_line_lsb & 1u);
        const size_t timing_query = begin_gpu_timing(GpuTimingKind::RasterCompute);
        glDispatchCompute(1, 1, 1);
        end_gpu_timing(timing_query, vertices.size() - 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                        GL_TEXTURE_FETCH_BARRIER_BIT |
                        GL_TEXTURE_UPDATE_BARRIER_BIT |
                        GL_FRAMEBUFFER_BARRIER_BIT);
        finish_in_place(min_x, min_y, max_x, max_y);
        if (debug_gl && glGetError() != GL_NO_ERROR)
            throw std::runtime_error("PSX OpenGL polyline rasterization failed");
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
        const size_t timing_query = begin_gpu_timing(GpuTimingKind::ImmediateDraw);
        glDrawArrays(GL_TRIANGLES, 0, command.header[1] == 4u ? 6 : 3);
        end_gpu_timing(timing_query);
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
        glUniform2i(scanout_display_start_location,
                    static_cast<GLint>(display_start_x), static_cast<GLint>(display_start_y));
        glUniform2i(scanout_source_offset_location, source_x, source_y);
        glUniform2i(scanout_destination_offset_location, destination_x, destination_y);
        glUniform2i(scanout_copy_size_location, copy_width, copy_height);
        glUniform1i(scanout_vram_height_location, static_cast<GLint>(source_vram_height));
        glUniform1i(scanout_rgb24_location, rgb24 ? 1 : 0);
        glUniform1i(scanout_display_enabled_location, display_enabled ? 1 : 0);
        const size_t timing_query = begin_gpu_timing(GpuTimingKind::Scanout);
        glDispatchCompute((width + 7) / 8, (height + 7) / 8, 1);
        end_gpu_timing(timing_query);
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
    const bool dither = state.dither_enable &&
        (kind == 3u || kind == 5u || (kind == 0u &&
            (gouraud || (textured && !raw_texture))));
    command.header[2] = (gouraud ? 1u : 0u) | (textured ? 2u : 0u) |
                        (raw_texture ? 4u : 0u) | (semi_transparent ? 8u : 0u) |
                        (state.set_mask ? 16u : 0u) | (state.check_mask ? 32u : 0u) |
                        (dither ? 64u : 0u) |
                        (state.interlaced_rendering ? 128u : 0u);
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
    command.texture[3] = (state.gpu_type == 2
        ? ((state.texture_page >> 5) & 3u) : ((state.texture_page >> 7) & 3u)) |
        ((state.active_line_lsb & 1u) << 8);
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
    if (impl_->host_cpu_timing_enabled) {
        std::cout.flush();
        std::cerr << "PSX GPU host CPU profile";
        for (size_t i = 0; i < kHostTimingKindCount; ++i) {
            if (!impl_->host_cpu_timing_calls[i]) continue;
            std::cerr << ' ' << kHostTimingKindNames[i] << '='
                      << (double(impl_->host_cpu_timing_ns[i]) / 1'000'000.0)
                      << "ms/" << impl_->host_cpu_timing_calls[i];
        }
        std::cerr << '\n';
    }
    if (std::getenv("MULTIARCADERECOMP_PSX_GPU_BATCH_STATS")) {
        std::cerr << "PSX GPU batch submissions=" << impl_->submitted_batch_count
                  << " primitives=" << impl_->submitted_batch_primitive_count;
        if (impl_->submitted_batch_count)
            std::cerr << " average=" << (double(impl_->submitted_batch_primitive_count) /
                                            double(impl_->submitted_batch_count));
        std::cerr << "; PSX texture cache generations=" << impl_->texture_cache_generations
                  << " dispatches=" << impl_->texture_cache_dispatches
                  << " max_pages_per_dispatch="
                  << impl_->texture_cache_max_pages_per_dispatch
                  << " hits=" << impl_->texture_cache_hits
                  << " invalidations=" << impl_->texture_cache_invalidations
                  << " evictions=" << impl_->texture_cache_evictions << '\n';
    }
}

void GpuHardwareRenderer::draw_polygon(std::span<const GpuHwVertex> vertices,
        const GpuHwState& state, bool textured, bool gouraud, bool raw_texture,
        bool semi_transparent, uint32_t clut) {
    Impl::HostCpuTimer host_timer(*impl_, HostTimingKind::DrawSubmit);
    auto command = make_command(0, vertices, state, gouraud, textured,
                               raw_texture, semi_transparent, clut);
    command.header[3] = vertices.empty() ? 0 : vertices.front().color;
    if (impl_->queue_opaque_primitive(command)) return;
    impl_->execute(command);
}

void GpuHardwareRenderer::fill_rect(int x, int y, int width, int height, uint16_t color,
                                    const GpuHwState& state) {
    Impl::HostCpuTimer host_timer(*impl_, HostTimingKind::DrawSubmit);
    width = std::clamp(width, 0, kVramWidth);
    height = std::clamp(height, 0, impl_->vram_height);
    if (!width || !height) return;
    const int wrapped_x = (x % kVramWidth + kVramWidth) % kVramWidth;
    const int wrapped_y = (y % impl_->vram_height + impl_->vram_height) % impl_->vram_height;
    const int first_width = std::min(width, kVramWidth - wrapped_x);
    const int first_height = std::min(height, impl_->vram_height - wrapped_y);
    const auto submit = [&](int left, int top, int w, int h) {
        if (w <= 0 || h <= 0) return;
        GpuHwState fill_state = state;
        // GP0(02) is MAME's FrameBufferRectangleDraw: unlike normal drawing
        // primitives, it addresses VRAM directly and ignores the GP0(E5)
        // drawing offset. The software path already passes unshifted x/y.
        fill_state.offset_x = 0;
        fill_state.offset_y = 0;
        fill_state.draw_x1 = left;
        fill_state.draw_y1 = top;
        fill_state.draw_x2 = left + w - 1;
        fill_state.draw_y2 = top + h - 1;
        fill_state.set_mask = false;
        fill_state.check_mask = false;
        const std::array<GpuHwVertex, 4> vertices{{
            {left, top, 0, 0, color}, {left + w, top, 0, 0, color},
            {left, top + h, 0, 0, color},
            {left + w, top + h, 0, 0, color}}};
        ShaderCommand command = make_command(1, vertices, fill_state,
            false, false, false, false, 0);
        command.header[2] = state.interlaced_rendering ? 128u : 0u;
        command.header[3] = color;
        if (!impl_->queue_opaque_primitive(command))
            impl_->execute_draw(command);
    };
    submit(wrapped_x, wrapped_y, first_width, first_height);
    submit(0, wrapped_y, width - first_width, first_height);
    submit(wrapped_x, 0, first_width, height - first_height);
    submit(0, 0, width - first_width, height - first_height);
}

void GpuHardwareRenderer::draw_rect(int x, int y, int width, int height,
        uint32_t color, const GpuHwState& state, bool semi_transparent) {
    Impl::HostCpuTimer host_timer(*impl_, HostTimingKind::DrawSubmit);
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
    Impl::HostCpuTimer host_timer(*impl_, HostTimingKind::DrawSubmit);
    const int left = std::min(a.x, b.x);
    const int top = std::min(a.y, b.y);
    const int right = std::max(a.x, b.x);
    const int bottom = std::max(a.y, b.y);
    const std::array<GpuHwVertex, 4> bounds{{
        {left, top, 0, 0, a.color}, {right + 1, top, 0, 0, a.color},
        {left, bottom + 1, 0, 0, b.color},
        {right + 1, bottom + 1, 0, 0, b.color}}};
    auto command = make_command(5, bounds, state, gouraud, false, false,
                                semi_transparent, 0);
    command.bounds[0] = left + state.offset_x;
    command.bounds[1] = top + state.offset_y;
    command.bounds[2] = right + state.offset_x;
    command.bounds[3] = bottom + state.offset_y;
    command.batch_scanline[0] = a.x + state.offset_x;
    command.batch_scanline[1] = a.y + state.offset_y;
    command.batch_scanline[2] = b.x + state.offset_x;
    command.batch_scanline[3] = b.y + state.offset_y;
    command.vertices[0].color = a.color;
    command.vertices[1].color = b.color;
    command.header[3] = uint32_t(((a.color >> 3) & 31u) |
        (((a.color >> 11) & 31u) << 5) | (((a.color >> 19) & 31u) << 10));
    if (!impl_->queue_opaque_primitive(command)) {
        const std::array<GpuHwVertex, 2> line{a, b};
        impl_->execute_polyline(line, state, gouraud, semi_transparent);
    }
}

void GpuHardwareRenderer::draw_polyline(
        std::span<const GpuHwVertex> vertices, const GpuHwState& state,
        bool gouraud, bool semi_transparent) {
    Impl::HostCpuTimer host_timer(*impl_, HostTimingKind::DrawSubmit);
    impl_->execute_polyline(vertices, state, gouraud, semi_transparent);
}

void GpuHardwareRenderer::draw_textured_rect(int x, int y, int width, int height,
        int u, int v, uint32_t color, uint32_t clut, const GpuHwState& state,
        bool raw_texture, bool semi_transparent) {
    Impl::HostCpuTimer host_timer(*impl_, HostTimingKind::DrawSubmit);
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

void GpuHardwareRenderer::sync_to_cpu(std::vector<uint8_t>& vram) {
    Impl::HostCpuTimer host_timer(*impl_, HostTimingKind::Readback);
    impl_->read_full(vram);
}
void GpuHardwareRenderer::sync_rect_to_cpu(std::vector<uint8_t>& bytes,
        unsigned x, unsigned y, unsigned width, unsigned height) {
    Impl::HostCpuTimer host_timer(*impl_, HostTimingKind::Readback);
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
void GpuHardwareRenderer::sync_from_cpu(std::span<const uint8_t> vram) {
    Impl::HostCpuTimer host_timer(*impl_, HostTimingKind::Upload);
    impl_->upload_full(vram);
}

void GpuHardwareRenderer::upload_rect(std::span<const uint8_t> bytes, unsigned x,
        unsigned y, unsigned width, unsigned height) {
    Impl::HostCpuTimer host_timer(*impl_, HostTimingKind::Upload);
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
    glBindTexture(GL_TEXTURE_2D, impl_->vram[impl_->current_vram]);
    const unsigned upload_x0 = x & 1023u;
    const unsigned upload_y0 = y % unsigned(impl_->vram_height);
    const unsigned first_width = std::min(width,
        unsigned(kVramWidth) - upload_x0);
    const unsigned first_height = std::min(height,
        unsigned(impl_->vram_height) - upload_y0);
    GLint previous_unpack_alignment = 4;
    GLint previous_unpack_row_length = 0;
    GLint previous_unpack_swap_bytes = GL_FALSE;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_unpack_alignment);
    glGetIntegerv(GL_UNPACK_ROW_LENGTH, &previous_unpack_row_length);
    glGetIntegerv(GL_UNPACK_SWAP_BYTES, &previous_unpack_swap_bytes);
    // Read directly from the little-endian CPU mirror with its 1024-word row
    // stride, avoiding a temporary repack for every CPU-to-VRAM transfer.
    // GL_UNSIGNED_SHORT follows host byte order, so swap only on big-endian
    // systems. Two-byte alignment remains correct for odd-width rectangles.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 2);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, kVramWidth);
    glPixelStorei(GL_UNPACK_SWAP_BYTES,
                  std::endian::native == std::endian::big ? GL_TRUE : GL_FALSE);
    const auto* first_row = bytes.data() + size_t(upload_y0) * kVramWidth * 2;
    for (GLuint texture : impl_->vram) {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexSubImage2D(GL_TEXTURE_2D, 0, int(upload_x0), int(upload_y0),
                        int(first_width), int(first_height), GL_RED_INTEGER,
                        GL_UNSIGNED_SHORT, first_row + size_t(upload_x0) * 2);
        if (first_width < width)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, int(upload_y0),
                            int(width - first_width), int(first_height), GL_RED_INTEGER,
                            GL_UNSIGNED_SHORT, first_row);
        if (first_height < height) {
            glTexSubImage2D(GL_TEXTURE_2D, 0, int(x & 1023u), 0, int(first_width),
                            int(height - first_height), GL_RED_INTEGER,
                            GL_UNSIGNED_SHORT, bytes.data() + size_t(upload_x0) * 2);
            if (first_width < width)
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, int(width - first_width),
                                int(height - first_height), GL_RED_INTEGER,
                                GL_UNSIGNED_SHORT, bytes.data());
        }
    }
    glPixelStorei(GL_UNPACK_SWAP_BYTES, previous_unpack_swap_bytes);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, previous_unpack_row_length);
    glPixelStorei(GL_UNPACK_ALIGNMENT, previous_unpack_alignment);
    impl_->vram_textures_diverged = false;
    ++impl_->vram_write_generation;
    const int cache_x0 = int(upload_x0);
    const int cache_y0 = int(upload_y0);
    impl_->invalidate_texture_cache_region(cache_x0, cache_y0,
        cache_x0 + int(first_width) - 1, cache_y0 + int(first_height) - 1);
    if (first_width < width)
        impl_->invalidate_texture_cache_region(0, cache_y0,
            int(width - first_width) - 1, cache_y0 + int(first_height) - 1);
    if (first_height < height)
        impl_->invalidate_texture_cache_region(cache_x0, 0,
            cache_x0 + int(first_width) - 1, int(height - first_height) - 1);
    if (first_width < width && first_height < height)
        impl_->invalidate_texture_cache_region(0, 0,
            int(width - first_width) - 1, int(height - first_height) - 1);
    glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                    GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
}

void GpuHardwareRenderer::copy_rect(int sx, int sy, int dx, int dy, int width,
        int height, const GpuHwState& state) {
    Impl::HostCpuTimer host_timer(*impl_, HostTimingKind::Copy);
    if (width <= 0 || height <= 0) return;
    sx &= 1023;
    const int destination_x = dx & 1023;
    sy = (sy % impl_->vram_height + impl_->vram_height) % impl_->vram_height;
    dy = (dy % impl_->vram_height + impl_->vram_height) % impl_->vram_height;

    impl_->flush_draw_batch();
    if (!impl_->make_current()) throw std::runtime_error("could not activate PSX OpenGL context");

    // Snapshot the complete source rectangle before writing any destination
    // pixels. This preserves overlap semantics even when either rectangle
    // wraps at the VRAM edge and the copy needs several dispatches. OpenGL
    // preserves command order in this context; the barrier makes prior draw
    // and image writes visible to the texture-copy operation without forcing
    // an extra host-side submission here.
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

    // The common GP0(80h) transfer has no mask semantics. Once the complete
    // source has been snapshotted, copy its integer VRAM pixels directly on
    // the GPU instead of dispatching the general per-pixel raster shader.
    // Keep the shader path below for either mask flag, where destination
    // pixels must be inspected or modified.
    if (!state.check_mask && !state.set_mask) {
        int row = 0;
        while (row < height) {
            const int destination_y = (dy + row) % impl_->vram_height;
            const int chunk_height = std::min(height - row,
                impl_->vram_height - destination_y);
            int column = 0;
            while (column < width) {
                const int destination_x = (dx + column) & 1023;
                const int chunk_width = std::min(width - column,
                                                 kVramWidth - destination_x);
                glCopyImageSubData(impl_->copy_scratch, GL_TEXTURE_2D, 0,
                                   column, row, 0,
                                   impl_->vram[impl_->current_vram], GL_TEXTURE_2D, 0,
                                   destination_x, destination_y, 0,
                                   chunk_width, chunk_height, 1);
                impl_->finish_in_place(destination_x, destination_y,
                    destination_x + chunk_width - 1,
                    destination_y + chunk_height - 1);
                column += chunk_width;
            }
            row += chunk_height;
        }
        glMemoryBarrier(GL_TEXTURE_UPDATE_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT |
                        GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_FRAMEBUFFER_BARRIER_BIT);
        return;
    }

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
    Impl::HostCpuTimer host_timer(*impl_, HostTimingKind::Scanout);
    return impl_->scanout_rgb(width, height, display_enabled, rgb24,
                              display_start_x, display_start_y, vram_height,
                              source_x, source_y, destination_x, destination_y,
                              copy_width, copy_height, asynchronous, direct_present);
}

bool GpuHardwareRenderer::present_scanout() {
    Impl::HostCpuTimer host_timer(*impl_, HostTimingKind::Present);
    return impl_->present_scanout();
}

const std::string& GpuHardwareRenderer::renderer_name() const noexcept { return impl_->renderer; }

} // namespace multiarcaderecomp::psx
