#define GL_GLEXT_PROTOTYPES 1
#include "midway/vunit/core/vunit_video_gpu.hpp"

#include <SDL3/SDL.h>
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <string>

namespace multiarcaderecomp::vunit {
namespace {
constexpr unsigned kTextureWidth = 4096;
constexpr unsigned kTextureHeight = 2048;
constexpr std::size_t kPageWords = 0x40000;
constexpr unsigned kTileSize = 8;
constexpr unsigned kTilesX = 512 / kTileSize;
constexpr unsigned kTilesY = 400 / kTileSize;

struct alignas(16) PackedCommand {
    float vertices[16];
    std::uint32_t meta[4]; // color base, render page, texture base, mode/dither
    std::int32_t bounds[4];
};
static_assert(sizeof(PackedCommand) == 96);

constexpr const char* kRasterShader = R"glsl(
#version 430 core
layout(local_size_x = 8, local_size_y = 8) in;
layout(r16ui, binding = 0) uniform uimage2D uDestination0;
layout(r16ui, binding = 1) uniform uimage2D uDestination1;
layout(binding = 1) uniform usampler2D uTexture;

struct RasterCommand {
    vec4 vertices[4];
    uvec4 meta;
    ivec4 bounds;
};
layout(std430, binding = 2) readonly buffer CommandBuffer { RasterCommand commands[]; };
layout(std430, binding = 3) readonly buffer TileOffsetBuffer { uint tile_offsets[]; };
layout(std430, binding = 4) readonly buffer TileCommandBuffer { uint tile_commands[]; };
uniform uint uTilesX;

float edge_value(vec4 a, vec4 b, vec2 p) {
    return (p.x - a.x) * (b.y - a.y) - (p.y - a.y) * (b.x - a.x);
}

bool triangle_uv(vec4 a, vec4 b, vec4 c, vec2 p, out vec2 uv) {
    float area = edge_value(a, b, c.xy);
    if (abs(area) < 1.0e-6) return false;
    float wa = edge_value(b, c, p) / area;
    float wb = edge_value(c, a, p) / area;
    float wc = 1.0 - wa - wb;
    if (wa < -1.0e-6 || wb < -1.0e-6 || wc < -1.0e-6) return false;
    uv = vec2(wa * a.z + wb * b.z + wc * c.z,
              wa * a.w + wb * b.w + wc * c.w);
    return true;
}

void main() {
    int x = int(gl_GlobalInvocationID.x);
    int y = int(gl_GlobalInvocationID.y);
    if (x >= 512 || y >= 400) return;
    vec2 pixel_center = vec2(float(x) + 0.5, float(y) + 0.5);
    uint tile = uint(y / 8) * uTilesX + uint(x / 8);
    bool changed0 = false;
    bool changed1 = false;
    uint value0 = 0u;
    uint value1 = 0u;
    for (uint n = tile_offsets[tile]; n < tile_offsets[tile + 1]; ++n) {
        RasterCommand command = commands[tile_commands[n]];
        if (x < command.bounds.x || x > command.bounds.z ||
            y < command.bounds.y || y > command.bounds.w) continue;
        vec2 uv;
        bool covered = triangle_uv(command.vertices[0], command.vertices[1],
                                   command.vertices[2], pixel_center, uv);
        if (!covered)
            covered = triangle_uv(command.vertices[0], command.vertices[2],
                                  command.vertices[3], pixel_center, uv);
        if (!covered || ((command.meta.w & 0x100u) != 0u && ((x ^ y) & 1) != 0))
            continue;

        uint output_value = command.meta.x;
        uint mode = command.meta.w & 0xffu;
        if (mode != 0u) {
            int iu = int(uv.x);
            int iv = int(uv.y);
            int signed_offset = ((iv >> 8) & 0xff00) + (iu >> 16);
            uint offset = uint(signed_offset) & 0x3fffffu;
            uint texture_index = command.meta.z + offset;
            uint texel = 0u;
            if (texture_index < 0x800000u) {
                ivec2 coord = ivec2(int(texture_index & 0xfffu), int(texture_index >> 12));
                texel = texelFetch(uTexture, coord, 0).r;
            }
            if ((mode == 1u || mode == 2u) && texel == 0u) continue;
            if (mode != 2u) output_value += texel;
        }

        if (command.meta.y == 0u) {
            if (!changed0) value0 = imageLoad(uDestination0, ivec2(x, y)).r;
            changed0 = true;
            value0 = output_value & 0xffffu;
        } else {
            if (!changed1) value1 = imageLoad(uDestination1, ivec2(x, y)).r;
            changed1 = true;
            value1 = output_value & 0xffffu;
        }
    }
    if (changed0) imageStore(uDestination0, ivec2(x, y), uvec4(value0, 0u, 0u, 0u));
    if (changed1) imageStore(uDestination1, ivec2(x, y), uvec4(value1, 0u, 0u, 0u));
}
)glsl";

constexpr const char* kPaletteVertexShader = R"glsl(
#version 430 core
void main() {
    vec2 p = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
)glsl";

constexpr const char* kPaletteFragmentShader = R"glsl(
#version 430 core
layout(location = 0) out vec4 fragColor;
uniform usampler2D uIndexPage;
uniform usampler2D uPalette;
uniform float uScanlineStrength;
void main() {
    ivec2 p = ivec2(gl_FragCoord.xy);
    if (p.x >= 512 || p.y >= 400) { fragColor = vec4(0.0, 0.0, 0.0, 1.0); return; }
    uint index = texelFetch(uIndexPage, p, 0).r & 0x7fffu;
    uint color = texelFetch(uPalette, ivec2(int(index & 255u), int(index >> 8)), 0).r;
    uvec3 five = uvec3((color >> 10) & 31u, (color >> 5) & 31u, color & 31u);
    uvec3 eight = (five << 3) | (five >> 2);
    vec3 rgb = vec3(eight) / 255.0;
    float row = mod(float(p.y), 2.0);
    float line_gain = mix(1.0, row < 0.5 ? 0.72 : 1.0, clamp(uScanlineStrength, 0.0, 1.0));
    fragColor = vec4(rgb * line_gain, 1.0);
}
)glsl";

bool gl_ok(std::string& error, const char* stage) {
    const GLenum code = glGetError();
    if (code == GL_NO_ERROR) return true;
    error = std::string(stage) + " failed with OpenGL error 0x";
    constexpr char digits[] = "0123456789abcdef";
    for (int shift = 28; shift >= 0; shift -= 4)
        error.push_back(digits[(code >> shift) & 0xf]);
    return false;
}

GLuint compile_program(const char* source, std::string& error) {
    const GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint length = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        glGetShaderInfoLog(shader, length, nullptr, log.data());
        glDeleteShader(shader);
        error = "V-Unit OpenGL compute shader compile failed: " + log;
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
        error = "V-Unit OpenGL compute program link failed: " + log;
        return 0;
    }
    return program;
}

GLuint compile_palette_program(std::string& error) {
    const GLuint vertex = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertex, 1, &kPaletteVertexShader, nullptr);
    glCompileShader(vertex);
    GLint compiled = GL_FALSE;
    glGetShaderiv(vertex, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint length = 0;
        glGetShaderiv(vertex, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        glGetShaderInfoLog(vertex, length, nullptr, log.data());
        glDeleteShader(vertex);
        error = "V-Unit RGB vertex shader compile failed: " + log;
        return 0;
    }
    const GLuint fragment = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragment, 1, &kPaletteFragmentShader, nullptr);
    glCompileShader(fragment);
    glGetShaderiv(fragment, GL_COMPILE_STATUS, &compiled);
    if (compiled != GL_TRUE) {
        GLint length = 0;
        glGetShaderiv(fragment, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        glGetShaderInfoLog(fragment, length, nullptr, log.data());
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        error = "V-Unit RGB fragment shader compile failed: " + log;
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
    if (linked != GL_TRUE) {
        GLint length = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
        std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
        glGetProgramInfoLog(program, length, nullptr, log.data());
        glDeleteProgram(program);
        error = "V-Unit RGB program link failed: " + log;
        return 0;
    }
    return program;
}
}

struct VUnitGpuRenderer::Impl {
    SDL_Window* window{};
    SDL_GLContext context{};
    bool owns_video_subsystem{};
    GLuint program{};
    GLuint palette_program{};
    GLuint framebuffer{};
    GLuint palette_vao{};
    GLuint framebuffer_pages[2]{};
    GLuint texture_ram{};
    GLuint palette_ram{};
    GLuint rgb_frame{};
    GLuint command_buffer{};
    GLuint tile_offset_buffer{};
    GLuint tile_command_buffer{};
    GLint tiles_x_location{-1};
    GLint rgb_index_location{-1};
    GLint rgb_palette_location{-1};
    GLint scanline_strength_location{-1};
    std::uint64_t texture_generation{};
    std::uint64_t palette_generation{};
    std::array<std::uint64_t, 2> vram_generation{};
    std::string renderer;
    std::string initialization_error;
    bool initialized{};

    ~Impl() {
        if (context) SDL_GL_MakeCurrent(window, context);
        if (program) glDeleteProgram(program);
        if (palette_program) glDeleteProgram(palette_program);
        if (framebuffer_pages[0] || framebuffer_pages[1])
            glDeleteTextures(2, framebuffer_pages);
        if (texture_ram) glDeleteTextures(1, &texture_ram);
        if (palette_ram) glDeleteTextures(1, &palette_ram);
        if (rgb_frame) glDeleteTextures(1, &rgb_frame);
        if (framebuffer) glDeleteFramebuffers(1, &framebuffer);
        if (palette_vao) glDeleteVertexArrays(1, &palette_vao);
        const GLuint buffers[] = {command_buffer, tile_offset_buffer, tile_command_buffer};
        glDeleteBuffers(3, buffers);
        if (context) SDL_GL_DestroyContext(context);
        if (window) SDL_DestroyWindow(window);
        if (owns_video_subsystem) SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }

    bool initialize(std::string& error) {
        if (initialized) return true;
        if (!(SDL_WasInit(SDL_INIT_VIDEO) & SDL_INIT_VIDEO)) {
            if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
                error = std::string("SDL video init for V-Unit GPU failed: ") + SDL_GetError();
                return false;
            }
            owns_video_subsystem = true;
        }
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        window = SDL_CreateWindow("MultiArcadeRecomp V-Unit GPU", 32, 32,
                                  SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
        if (!window) {
            error = std::string("SDL hidden OpenGL window failed: ") + SDL_GetError();
            return false;
        }
        context = SDL_GL_CreateContext(window);
        if (!context) {
            error = std::string("SDL OpenGL context failed: ") + SDL_GetError();
            return false;
        }
        if (!SDL_GL_MakeCurrent(window, context)) {
            error = std::string("SDL OpenGL context activation failed: ") + SDL_GetError();
            return false;
        }
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        if (major < 4 || (major == 4 && minor < 3)) {
            error = "V-Unit GPU renderer requires OpenGL 4.3 compute shaders";
            return false;
        }
        if (const auto* name = glGetString(GL_RENDERER))
            renderer = reinterpret_cast<const char*>(name);
        program = compile_program(kRasterShader, error);
        if (!program) return false;
        palette_program = compile_palette_program(error);
        if (!palette_program) return false;
        tiles_x_location = glGetUniformLocation(program, "uTilesX");
        rgb_index_location = glGetUniformLocation(palette_program, "uIndexPage");
        rgb_palette_location = glGetUniformLocation(palette_program, "uPalette");
        scanline_strength_location = glGetUniformLocation(palette_program, "uScanlineStrength");
        glGenFramebuffers(1, &framebuffer);
        glGenVertexArrays(1, &palette_vao);
        glGenTextures(2, framebuffer_pages);
        for (const auto page : framebuffer_pages) {
            glBindTexture(GL_TEXTURE_2D, page);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R16UI, 512, 512, 0,
                         GL_RED_INTEGER, GL_UNSIGNED_SHORT, nullptr);
        }
        glGenTextures(1, &texture_ram);
        glBindTexture(GL_TEXTURE_2D, texture_ram);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, kTextureWidth, kTextureHeight, 0,
                     GL_RED_INTEGER, GL_UNSIGNED_BYTE, nullptr);
        glGenTextures(1, &palette_ram);
        glBindTexture(GL_TEXTURE_2D, palette_ram);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 256, 128, 0,
                     GL_RED_INTEGER, GL_UNSIGNED_INT, nullptr);
        glGenTextures(1, &rgb_frame);
        glBindTexture(GL_TEXTURE_2D, rgb_frame);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 512, 512, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glGenBuffers(1, &command_buffer);
        glGenBuffers(1, &tile_offset_buffer);
        glGenBuffers(1, &tile_command_buffer);
        glUseProgram(program);
        glUniform1i(glGetUniformLocation(program, "uTexture"), 1);
        glUseProgram(palette_program);
        glUniform1i(rgb_index_location, 0);
        glUniform1i(rgb_palette_location, 1);
        initialized = gl_ok(error, "V-Unit GPU resource initialization");
        return initialized;
    }
};

VUnitGpuRenderer::VUnitGpuRenderer() : m_impl(new Impl) {}
VUnitGpuRenderer::~VUnitGpuRenderer() { delete m_impl; }

const std::string& VUnitGpuRenderer::renderer_name() const noexcept {
    return m_impl->renderer;
}

bool VUnitGpuRenderer::initialize(std::string& error) noexcept {
    try {
        if (!m_impl->initialize(error)) return false;
        if (SDL_GL_MakeCurrent(m_impl->window, m_impl->context)) return true;
        error = std::string("SDL OpenGL context activation failed: ") + SDL_GetError();
        return false;
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "unknown V-Unit OpenGL initialization failure";
        return false;
    }
}

bool VUnitGpuRenderer::render(std::span<const Video::GpuCommand> commands,
        std::span<const std::uint8_t> texture, std::span<std::uint16_t> vram,
        std::uint64_t texture_generation,
        std::array<std::uint64_t, 2> vram_generation,
        std::uint8_t readback_pages,
        std::string& error) noexcept {
    try {
        auto& gl = *m_impl;
        if (!gl.initialize(error)) return false;
        if (!SDL_GL_MakeCurrent(gl.window, gl.context)) {
            error = std::string("SDL OpenGL context activation failed: ") + SDL_GetError();
            return false;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, gl.framebuffer);
        for (unsigned page = 0; page < 2; ++page) {
            if (gl.vram_generation[page] != vram_generation[page]) {
                glBindTexture(GL_TEXTURE_2D, gl.framebuffer_pages[page]);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 512,
                    GL_RED_INTEGER, GL_UNSIGNED_SHORT, vram.data() + page * kPageWords);
                gl.vram_generation[page] = vram_generation[page];
            }
        }
        if (gl.texture_generation != texture_generation) {
            glBindTexture(GL_TEXTURE_2D, gl.texture_ram);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, kTextureWidth, kTextureHeight,
                GL_RED_INTEGER, GL_UNSIGNED_BYTE, texture.data());
            gl.texture_generation = texture_generation;
        }

        const auto readback = [&]() {
            for (unsigned page = 0; page < 2; ++page) {
                if ((readback_pages & (1u << page)) == 0) continue;
                glBindFramebuffer(GL_FRAMEBUFFER, gl.framebuffer);
                glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                       GL_TEXTURE_2D, gl.framebuffer_pages[page], 0);
                glReadBuffer(GL_COLOR_ATTACHMENT0);
                const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                if (status != GL_FRAMEBUFFER_COMPLETE) {
                    error = "V-Unit GPU framebuffer is incomplete (0x" +
                            std::to_string(status) + ")";
                    return false;
                }
                glReadPixels(0, 0, 512, 512, GL_RED_INTEGER, GL_UNSIGNED_SHORT,
                             vram.data() + page * kPageWords);
            }
            return gl_ok(error, "V-Unit GPU raster/readback");
        };
        // A display refresh with no GPU-written page needs no GPU work. In
        // particular, keep the POST/CPU-rendered screens off the GL readback path.
        if (commands.empty()) return readback();

        glUseProgram(gl.program);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gl.texture_ram);
        glBindImageTexture(0, gl.framebuffer_pages[0], 0, GL_FALSE, 0,
                           GL_READ_WRITE, GL_R16UI);
        glBindImageTexture(1, gl.framebuffer_pages[1], 0, GL_FALSE, 0,
                           GL_READ_WRITE, GL_R16UI);

        std::vector<PackedCommand> packed_commands;
        packed_commands.reserve(commands.size());
        std::vector<std::uint32_t> tile_counts(kTilesX * kTilesY);
        for (std::uint32_t command_index = 0; command_index < commands.size(); ++command_index) {
            const auto& command = commands[command_index];
            PackedCommand packed{};
            float min_x = command.vertices[0].x, max_x = min_x;
            float min_y = command.vertices[0].y, max_y = min_y;
            for (unsigned i = 0; i < 4; ++i) {
                const auto& vertex = command.vertices[i];
                packed.vertices[i * 4 + 0] = vertex.x;
                packed.vertices[i * 4 + 1] = vertex.y;
                packed.vertices[i * 4 + 2] = vertex.u;
                packed.vertices[i * 4 + 3] = vertex.v;
                min_x = std::min(min_x, vertex.x); max_x = std::max(max_x, vertex.x);
                min_y = std::min(min_y, vertex.y); max_y = std::max(max_y, vertex.y);
            }
            packed.bounds[0] =
                std::clamp(static_cast<int>(std::floor(min_x)), 0, 511);
            packed.bounds[1] = std::clamp(static_cast<int>(std::floor(min_y)), 0, 399);
            packed.bounds[2] = std::clamp(static_cast<int>(std::ceil(max_x)), 0, 511);
            packed.bounds[3] = std::clamp(static_cast<int>(std::ceil(max_y)), 0, 399);
            packed.meta[0] = command.color_base;
            packed.meta[1] = command.render_page & 1u;
            packed.meta[2] = command.texture_base;
            packed.meta[3] = command.mode | (command.dither ? 0x100u : 0u);
            packed_commands.push_back(packed);
            if (packed.bounds[0] > packed.bounds[2] || packed.bounds[1] > packed.bounds[3])
                continue;
            const unsigned first_tile_x = static_cast<unsigned>(packed.bounds[0]) / kTileSize;
            const unsigned last_tile_x = static_cast<unsigned>(packed.bounds[2]) / kTileSize;
            const unsigned first_tile_y = static_cast<unsigned>(packed.bounds[1]) / kTileSize;
            const unsigned last_tile_y = static_cast<unsigned>(packed.bounds[3]) / kTileSize;
            for (unsigned ty = first_tile_y; ty <= last_tile_y; ++ty) {
                for (unsigned tx = first_tile_x; tx <= last_tile_x; ++tx)
                    ++tile_counts[ty * kTilesX + tx];
            }
        }

        std::vector<std::uint32_t> tile_offsets(kTilesX * kTilesY + 1);
        for (std::size_t tile = 0; tile < tile_counts.size(); ++tile)
            tile_offsets[tile + 1] = tile_offsets[tile] +
                tile_counts[tile];
        std::vector<std::uint32_t> tile_commands(tile_offsets.back());
        std::vector<std::uint32_t> tile_cursors(tile_offsets.begin(), tile_offsets.end() - 1);
        for (std::uint32_t command_index = 0; command_index < packed_commands.size(); ++command_index) {
            const auto& bounds = packed_commands[command_index].bounds;
            if (bounds[0] > bounds[2] || bounds[1] > bounds[3]) continue;
            const unsigned first_tile_x = static_cast<unsigned>(bounds[0]) / kTileSize;
            const unsigned last_tile_x = static_cast<unsigned>(bounds[2]) / kTileSize;
            const unsigned first_tile_y = static_cast<unsigned>(bounds[1]) / kTileSize;
            const unsigned last_tile_y = static_cast<unsigned>(bounds[3]) / kTileSize;
            for (unsigned ty = first_tile_y; ty <= last_tile_y; ++ty) {
                for (unsigned tx = first_tile_x; tx <= last_tile_x; ++tx) {
                    const auto tile = ty * kTilesX + tx;
                    tile_commands[tile_cursors[tile]++] = command_index;
                }
            }
        }

        if (!packed_commands.empty()) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl.command_buffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                static_cast<GLsizeiptr>(packed_commands.size() * sizeof(PackedCommand)),
                packed_commands.data(), GL_STREAM_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, gl.command_buffer);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl.tile_offset_buffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                static_cast<GLsizeiptr>(tile_offsets.size() * sizeof(std::uint32_t)),
                tile_offsets.data(), GL_STREAM_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, gl.tile_offset_buffer);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, gl.tile_command_buffer);
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                static_cast<GLsizeiptr>(tile_commands.size() * sizeof(std::uint32_t)),
                tile_commands.data(), GL_STREAM_DRAW);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, gl.tile_command_buffer);
            glUniform1ui(gl.tiles_x_location, kTilesX);
            glDispatchCompute(kTilesX, kTilesY, 1);
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_UPDATE_BARRIER_BIT);
        }
        return readback();
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "unknown V-Unit GPU renderer failure";
        return false;
    }
}

bool VUnitGpuRenderer::convert_visible_rgb(unsigned page,
        std::span<const std::uint32_t> palette,
        std::uint64_t palette_generation,
        std::span<const std::uint16_t> vram,
        std::array<std::uint64_t, 2> vram_generation,
        float scanline_strength,
        std::vector<std::uint8_t>& rgb,
        std::string& error) noexcept {
    try {
        auto& gl = *m_impl;
        if (page > 1 || palette.size() < 0x8000 || vram.size() < 2 * kPageWords) {
            error = "invalid V-Unit GPU palette-conversion input";
            return false;
        }
        if (!gl.initialize(error)) return false;
        if (!SDL_GL_MakeCurrent(gl.window, gl.context)) {
            error = std::string("SDL OpenGL context activation failed: ") + SDL_GetError();
            return false;
        }
        if (gl.vram_generation[page] != vram_generation[page]) {
            glBindTexture(GL_TEXTURE_2D, gl.framebuffer_pages[page]);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 512, 512,
                GL_RED_INTEGER, GL_UNSIGNED_SHORT, vram.data() + page * kPageWords);
            gl.vram_generation[page] = vram_generation[page];
        }
        if (gl.palette_generation != palette_generation) {
            glBindTexture(GL_TEXTURE_2D, gl.palette_ram);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 128,
                GL_RED_INTEGER, GL_UNSIGNED_INT, palette.data());
            gl.palette_generation = palette_generation;
        }

        glUseProgram(gl.palette_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gl.framebuffer_pages[page]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gl.palette_ram);
        glBindFramebuffer(GL_FRAMEBUFFER, gl.framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, gl.rgb_frame, 0);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            error = "V-Unit GPU RGB framebuffer is incomplete";
            return false;
        }
        glViewport(0, 0, 512, 512);
        glUseProgram(gl.palette_program);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, gl.framebuffer_pages[page]);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, gl.palette_ram);
        glUniform1f(gl.scanline_strength_location, std::clamp(scanline_strength, 0.0f, 1.0f));
        glBindVertexArray(gl.palette_vao);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glMemoryBarrier(GL_FRAMEBUFFER_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
        rgb.resize(512u * 400u * 3u);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, 512, 400, GL_RGB, GL_UNSIGNED_BYTE, rgb.data());
        return gl_ok(error, "V-Unit GPU palette conversion/readback");
    } catch (const std::exception& e) {
        error = e.what();
        return false;
    } catch (...) {
        error = "unknown V-Unit GPU palette-conversion failure";
        return false;
    }
}

} // namespace multiarcaderecomp::vunit
