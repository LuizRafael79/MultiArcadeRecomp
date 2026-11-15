#include "runtime/sega/presentation/sega_presenter.hpp"
#include "boards/sega/common/video_output.hpp"
#include "sega_config.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace mamerecomp {
namespace {

bool presenter_opengl_available() {
#ifdef MAMERECOMP_HAS_OPENGL_VIDEO
    return true;
#else
    return false;
#endif
}

} // namespace

struct SegaPresenter::Impl {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    SegaFrameRgbConverter rgb_converter;
    bool renderer_vsync_enabled = false;
    bool frame_limit_enabled = true;
    bool profile_enabled = false;
    SegaPresenterTiming timing;
    std::uint32_t refresh_num = 60, refresh_den = 1;
    std::uint64_t frame_remainder = 0;
    std::chrono::steady_clock::time_point next_frame = std::chrono::steady_clock::now();
    SDL_DisplayMode exclusive_display_mode{};
    SegaDisplayMode display_mode = SegaDisplayMode::Windowed;
    std::uint8_t window_scale = 3;
    SegaLiveInputs inputs;
    std::vector<mamerecomp::sega::GameInputBinding> input_bindings;

    explicit Impl(const std::string& title,
        std::vector<mamerecomp::sega::GameInputBinding> bindings, bool limit_frames, bool profile,
        std::uint32_t rate_num, std::uint32_t rate_den)
        : frame_limit_enabled(limit_frames), profile_enabled(profile), refresh_num(rate_num), refresh_den(rate_den),
          input_bindings(std::move(bindings)) {
        if (!refresh_num || !refresh_den) throw std::invalid_argument("invalid Sega refresh rate");
        // SDL's Wayland libdecor GTK plugin can crash while negotiating window
        // decorations on some desktop stacks. Use xdg-shell/server decorations
        // when available, with SDL's undecorated Wayland fallback otherwise.
        SDL_SetHint(SDL_HINT_VIDEO_WAYLAND_ALLOW_LIBDECOR, "0");
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            throw std::runtime_error(std::string("SDL video init failed: ") + SDL_GetError());
        const char* video_driver = SDL_GetCurrentVideoDriver();
        SDL_Log("MameRecomp SDL video driver: %s", video_driver ? video_driver : "unknown");
        window = SDL_CreateWindow(title.c_str(), 960, 672, SDL_WINDOW_RESIZABLE);
        if (!window) { SDL_QuitSubSystem(SDL_INIT_VIDEO); throw std::runtime_error(std::string("SDL window creation failed: ") + SDL_GetError()); }
        // Let SDL select the best available renderer. Forcing "software" here
        // makes every presented frame pass through its CPU rasterizer even
        // when the board's tilemap backend is using OpenGL.
        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer) renderer = SDL_CreateRenderer(window, "software");
        if (!renderer) { SDL_DestroyWindow(window); SDL_QuitSubSystem(SDL_INIT_VIDEO); throw std::runtime_error(std::string("SDL renderer creation failed: ") + SDL_GetError()); }
        SDL_Log("MameRecomp System16 SDL video renderer: %s",
            SDL_GetRendererName(renderer) ? SDL_GetRendererName(renderer) : "unknown");
        int actual_vsync = 0;
        // Wayland's compositor presents on its own cadence. Combining an EGL
        // swap interval with the board-rate limiter can miss compositor frames,
        // particularly for System18's 57.23 Hz on a 60 Hz display.
        const bool wayland = video_driver && std::string_view(video_driver) == "wayland";
        const bool vsync_configured = SDL_SetRenderVSync(renderer, frame_limit_enabled && !wayland ? 1 : 0);
        renderer_vsync_enabled = SDL_GetRenderVSync(renderer, &actual_vsync) && actual_vsync != 0;
        SDL_Log("MameRecomp System16 SDL renderer VSync: %s", renderer_vsync_enabled ? "on" : "off");
        if (!vsync_configured)
            SDL_Log("MameRecomp System16 SDL renderer could not change VSync: %s", SDL_GetError());
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
            SegaVideoFrame::width, SegaVideoFrame::height);
        if (!texture) { SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_QuitSubSystem(SDL_INIT_VIDEO); throw std::runtime_error(std::string("SDL texture creation failed: ") + SDL_GetError()); }
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    }
    ~Impl() {
        if (texture) SDL_DestroyTexture(texture);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
};

SegaPresenter::SegaPresenter(std::string title,
    std::vector<mamerecomp::sega::GameInputBinding> input_bindings, bool frame_limit_enabled, bool profile_enabled,
    std::uint32_t refresh_num, std::uint32_t refresh_den)
    : impl_(std::make_unique<Impl>(title, std::move(input_bindings), frame_limit_enabled, profile_enabled,
        refresh_num, refresh_den)) {}
SegaPresenter::~SegaPresenter() = default;

bool SegaPresenter::configure_display(SegaDisplayMode mode, std::uint8_t window_scale) {
    if (mode == SegaDisplayMode::FullscreenExclusive) {
        if (!SDL_SetWindowFullscreen(impl_->window, false)
            || !SDL_SetWindowFullscreenMode(impl_->window, nullptr)) return false;
        const SDL_DisplayID display = SDL_GetDisplayForWindow(impl_->window);
        if (!display) return false;
        const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display);
        if (!desktop || !SDL_GetClosestFullscreenDisplayMode(
                display, desktop->w, desktop->h,
                static_cast<float>(impl_->refresh_num) / static_cast<float>(impl_->refresh_den), true,
                &impl_->exclusive_display_mode)) return false;
        if (!SDL_SetWindowFullscreenMode(impl_->window, &impl_->exclusive_display_mode)
            || !SDL_SetWindowFullscreen(impl_->window, true)) {
            SDL_SetWindowFullscreen(impl_->window, false);
            SDL_SetWindowFullscreenMode(impl_->window, nullptr);
            return false;
        }
        impl_->display_mode = mode;
        impl_->window_scale = window_scale;
        return true;
    }
    if (!SDL_SetWindowFullscreen(impl_->window, false)
        || !SDL_SetWindowFullscreenMode(impl_->window, nullptr)) return false;
    if (mode == SegaDisplayMode::FullscreenBorderless
        && !SDL_SetWindowFullscreen(impl_->window, true)) return false;
    if (mode == SegaDisplayMode::Windowed) {
        SDL_SetWindowSize(impl_->window, 320 * window_scale, 224 * window_scale);
        SDL_SetWindowPosition(impl_->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
    impl_->display_mode = mode;
    impl_->window_scale = window_scale;
    return true;
}

bool SegaPresenter::show_configuration_menu(SegaSharedSettings& settings,
    const std::filesystem::path& settings_path, const std::string& game_title,
    const mamerecomp::sega::GameSettingsApi* game_api, void* game_state) {
    if (!configure_display(settings.display_mode, settings.window_scale))
        throw std::runtime_error(std::string("SDL fullscreen configuration failed: ") + SDL_GetError());
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    if (!ImGui_ImplSDL3_InitForSDLRenderer(impl_->window, impl_->renderer)) {
        ImGui::DestroyContext();
        throw std::runtime_error("Dear ImGui SDL3 platform initialization failed");
    }
    if (!ImGui_ImplSDLRenderer3_Init(impl_->renderer)) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        throw std::runtime_error("Dear ImGui SDL renderer initialization failed");
    }

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 0.0f;
    style.ChildRounding = 5.0f;
    style.FrameRounding = 4.0f;
    style.PopupRounding = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.WindowPadding = ImVec2(26.0f, 22.0f);
    style.FramePadding = ImVec2(12.0f, 8.0f);
    style.ItemSpacing = ImVec2(12.0f, 12.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.025f, 0.040f, 0.065f, 1.0f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.035f, 0.055f, 0.085f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.075f, 0.115f, 0.165f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.100f, 0.205f, 0.270f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.115f, 0.260f, 0.330f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.080f, 0.185f, 0.245f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.100f, 0.255f, 0.330f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.130f, 0.310f, 0.390f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.690f, 0.055f, 0.115f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.850f, 0.075f, 0.145f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.960f, 0.095f, 0.160f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.180f, 0.820f, 0.920f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.180f, 0.720f, 0.830f, 1.0f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.055f, 0.095f, 0.140f, 1.0f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.100f, 0.235f, 0.300f, 1.0f);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.100f, 0.235f, 0.300f, 1.0f);

    bool running = true;
    bool start_game = false;
    std::string save_status = "Salvo automaticamente ao alterar";
    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) { running = false; start_game = false; }
            if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) { running = false; start_game = false; }
        }
        if (!running) break;

        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        int width = 0, height = 0;
        SDL_GetWindowSize(impl_->window, &width, &height);
        ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
        ImGui::SetNextWindowSize(ImVec2(static_cast<float>(width), static_cast<float>(height)));
        ImGui::Begin("##MameRecompSetup", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize
            | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGui::TextColored(ImVec4(0.20f, 0.82f, 0.91f, 1.0f), "MAMERECOMP  /  SYSTEM 16A");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", game_title.c_str());
        ImGui::Separator();

        bool changed = false;
        if (ImGui::BeginTabBar("##settings-tabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
            if (ImGui::BeginTabItem("Jogo")) {
                ImGui::Spacing();
                if (game_api && game_api->draw_game_tab && game_state)
                    changed |= game_api->draw_game_tab(game_state);
                else
                    ImGui::TextDisabled("Este jogo nao fornece opcoes adicionais.");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Video")) {
                ImGui::Spacing();
                ImGui::TextDisabled("VIDEO E APRESENTACAO");
                ImGui::Spacing();
                if (presenter_opengl_available()) {
                    int backend = settings.video_backend == SegaVideoBackend::OpenGL ? 1 : 0;
                    constexpr const char* backend_items[] = {"Software (CPU)", "OpenGL (compute)"};
                    if (ImGui::Combo("Renderer", &backend, backend_items, 2)) {
                        settings.video_backend = backend == 1 ? SegaVideoBackend::OpenGL : SegaVideoBackend::Software;
                        changed = true;
                    }
                    ImGui::TextWrapped("OpenGL acelera a composicao dos tilemaps; sprites e paleta final ainda sao processados pela CPU.");
                } else {
                    settings.video_backend = SegaVideoBackend::Software;
                    ImGui::Text("Compositor: Software (CPU)");
                    ImGui::TextDisabled("O backend OpenGL nao foi compilado neste sistema.");
                }
                int display_mode = static_cast<int>(settings.display_mode);
                constexpr const char* display_items[] = {
                    "Janela", "Tela cheia exclusiva", "Tela cheia sem borda"};
                if (ImGui::Combo("Modo de tela", &display_mode, display_items, 3)) {
                    settings.display_mode = static_cast<SegaDisplayMode>(display_mode);
                    if (!configure_display(settings.display_mode, settings.window_scale))
                        throw std::runtime_error(std::string("SDL display mode change failed: ") + SDL_GetError());
                    changed = true;
                }
                const bool fullscreen = settings.display_mode != SegaDisplayMode::Windowed;
                ImGui::BeginDisabled(fullscreen);
                int scale = std::clamp<int>(settings.window_scale, 2, 5) - 2;
                constexpr const char* scale_items[] = {"2x  (640 x 448)", "3x  (960 x 672)", "4x  (1280 x 896)", "5x  (1600 x 1120)"};
                if (ImGui::Combo("Tamanho da janela", &scale, scale_items, 4)) {
                    settings.window_scale = static_cast<std::uint8_t>(scale + 2);
                    SDL_SetWindowSize(impl_->window, 320 * settings.window_scale, 224 * settings.window_scale);
                    SDL_SetWindowPosition(impl_->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
                    changed = true;
                }
                ImGui::EndDisabled();
                ImGui::TextDisabled("VSync: ligado (sincronizado a 60 Hz)");
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Audio")) {
                ImGui::Spacing();
                ImGui::TextDisabled("SOM E SAIDA ESTEREO");
                ImGui::Spacing();
                changed |= ImGui::Checkbox("Ativar audio do jogo", &settings.audio_enabled);
                ImGui::BeginDisabled(!settings.audio_enabled);
                int pan = settings.audio_stereo_spread ? 1 : 0;
                constexpr const char* pan_items[] = {"Pan do jogo", "Stereo espacial"};
                if (ImGui::Combo("Panoramica", &pan, pan_items, 2)) {
                    settings.audio_stereo_spread = pan == 1; changed = true;
                }
                int volume = settings.audio_volume;
                if (ImGui::SliderInt("Volume", &volume, 0, 100, "%d%%")) {
                    settings.audio_volume = static_cast<std::uint8_t>(volume); changed = true;
                }
                ImGui::TextWrapped("Stereo espacial alterna os canais FM entre esquerda e direita. Pan do jogo segue os registradores do chip.");
                ImGui::EndDisabled();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Controles")) {
                ImGui::Spacing();
                if (game_api && game_api->input_bindings) {
                    std::size_t count = 0;
                    const auto* bindings = game_api->input_bindings(&count);
                    ImGui::TextDisabled("TECLADO");
                    for (std::size_t i = 0; bindings && i < count; ++i)
                        ImGui::BulletText("%s: %s", bindings[i].control_name, bindings[i].key_name);
                } else ImGui::TextDisabled("Este jogo nao informa os controles nesta tela.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::SetCursorPosY(std::max(ImGui::GetCursorPosY(), ImGui::GetWindowHeight() - 102.0f));
        ImGui::Separator();
        if (ImGui::BeginTable("##footer", 2, ImGuiTableFlags_SizingStretchProp)) {
            ImGui::TableSetupColumn("Brand", ImGuiTableColumnFlags_WidthStretch, 1.5f);
            ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const ImVec2 logo_pos = ImGui::GetCursorScreenPos();
            ImDrawList* draw = ImGui::GetWindowDrawList();
            draw->AddText(ImGui::GetFont(), 24.0f, ImVec2(logo_pos.x, logo_pos.y + 5.0f), IM_COL32(222, 232, 244, 255), game_title.c_str());
            draw->AddRectFilled(ImVec2(logo_pos.x, logo_pos.y + 38.0f), ImVec2(logo_pos.x + 220.0f, logo_pos.y + 41.0f), IM_COL32(185, 18, 47, 255));
            ImGui::Dummy(ImVec2(220.0f, 46.0f));
            ImGui::TextDisabled("%s", save_status.c_str());
            ImGui::TableSetColumnIndex(1);
            const float button_width = std::min(260.0f, ImGui::GetContentRegionAvail().x);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - button_width);
            if (ImGui::Button("INICIAR JOGO", ImVec2(button_width, 46.0f))) { start_game = true; running = false; }
            ImGui::EndTable();
        }
        ImGui::End();

        if (changed) {
            std::string error;
            save_status = save_sega_settings(settings_path, settings, &error) ? "Configuracao salva" : error;
        }
        ImGui::Render();
        SDL_SetRenderDrawColor(impl_->renderer, 6, 10, 17, 255);
        SDL_RenderClear(impl_->renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), impl_->renderer);
        SDL_RenderPresent(impl_->renderer);
    }

    if (start_game) {
        std::string error;
        if (!save_sega_settings(settings_path, settings, &error)) save_status = error;
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    return start_game;
}

bool SegaPresenter::present(const SegaVideoFrame& frame, std::span<const std::uint8_t> palette_ram) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) return false;
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) return false;
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_RETURN
            && (event.key.mod & SDL_KMOD_ALT) != 0 && !event.key.repeat) {
            const SegaDisplayMode target = impl_->display_mode == SegaDisplayMode::Windowed
                ? SegaDisplayMode::FullscreenExclusive : SegaDisplayMode::Windowed;
            if (!configure_display(target, impl_->window_scale)) {
                if (target == SegaDisplayMode::FullscreenExclusive
                    && configure_display(SegaDisplayMode::FullscreenBorderless, impl_->window_scale)) {
                    SDL_Log("Exclusive fullscreen unavailable; using borderless fullscreen");
                } else {
                    SDL_Log("Fullscreen toggle failed: %s", SDL_GetError());
                }
            }
        }
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    impl_->inputs = {};
    const auto fallback = std::array<mamerecomp::sega::GameInputBinding, 13>{
        {{mamerecomp::sega::GameInputPort::Service, 0, "5", "Ficha 1"}, {mamerecomp::sega::GameInputPort::Service, 1, "6", "Ficha 2"},
         {mamerecomp::sega::GameInputPort::Service, 2, "F2", "Servico"}, {mamerecomp::sega::GameInputPort::Service, 4, "1", "Iniciar 1"},
         {mamerecomp::sega::GameInputPort::Service, 5, "2", "Iniciar 2"}, {mamerecomp::sega::GameInputPort::Player1, 1, "K", "Botao 1"},
         {mamerecomp::sega::GameInputPort::Player1, 2, "L", "Botao 2"}, {mamerecomp::sega::GameInputPort::Player1, 0, "J", "Botao 3"},
         {mamerecomp::sega::GameInputPort::Player1, 4, "S", "Baixo"}, {mamerecomp::sega::GameInputPort::Player1, 5, "W", "Cima"},
         {mamerecomp::sega::GameInputPort::Player1, 6, "D", "Direita"}, {mamerecomp::sega::GameInputPort::Player1, 7, "A", "Esquerda"},
         {mamerecomp::sega::GameInputPort::Player2, 0, "KP_3", "P2 Botao 3"}}};
    const auto apply = [&](const auto& binding) {
        const SDL_Scancode key = SDL_GetScancodeFromName(binding.key_name);
        if (key == SDL_SCANCODE_UNKNOWN || !keys[key]) return;
        auto* port = binding.port == mamerecomp::sega::GameInputPort::Service ? &impl_->inputs.service
            : binding.port == mamerecomp::sega::GameInputPort::Player1 ? &impl_->inputs.p1 : &impl_->inputs.p2;
        *port = static_cast<std::uint8_t>(*port & ~(1U << binding.bit));
    };
    if (impl_->input_bindings.empty()) for (const auto& binding : fallback) apply(binding);
    else for (const auto& binding : impl_->input_bindings) apply(binding);
    using Clock = std::chrono::steady_clock;
    auto phase_start = impl_->profile_enabled ? Clock::now() : Clock::time_point{};
    const auto record_phase = [&](std::uint64_t& total, std::uint64_t& maximum) {
        if (!impl_->profile_enabled) return;
        const auto now = Clock::now();
        const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now - phase_start).count());
        total += elapsed;
        maximum = std::max(maximum, elapsed);
        phase_start = now;
    };
    const auto rgb = impl_->rgb_converter.convert(frame, palette_ram);
    record_phase(impl_->timing.convert_ns, impl_->timing.convert_max_ns);
    if (!SDL_UpdateTexture(impl_->texture, nullptr, rgb.data(), SegaVideoFrame::width * 3))
        throw std::runtime_error(std::string("SDL frame upload failed: ") + SDL_GetError());
    record_phase(impl_->timing.upload_ns, impl_->timing.upload_max_ns);
    SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255);
    SDL_RenderClear(impl_->renderer);
    int output_w = 0, output_h = 0;
    SDL_GetCurrentRenderOutputSize(impl_->renderer, &output_w, &output_h);
    SDL_FRect destination{};
    const float scale = std::min(static_cast<float>(output_w) / SegaVideoFrame::width, static_cast<float>(output_h) / SegaVideoFrame::height);
    destination.w = SegaVideoFrame::width * scale;
    destination.h = SegaVideoFrame::height * scale;
    destination.x = (output_w - destination.w) * 0.5f;
    destination.y = (output_h - destination.h) * 0.5f;
    SDL_RenderTexture(impl_->renderer, impl_->texture, nullptr, &destination);
    // Host-side proof of concept: this label is drawn after the emulated frame,
    // so it never changes guest video memory or captured frame output.
    const float label_x = destination.x + 8.0f * scale;
    const float label_y = destination.y + 8.0f * scale;
    SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255);
    SDL_RenderDebugText(impl_->renderer, label_x + 1.0f, label_y + 1.0f, "Native Edition");
    SDL_SetRenderDrawColor(impl_->renderer, 120, 230, 255, 255);
    SDL_RenderDebugText(impl_->renderer, label_x, label_y, "Native Edition");
    if (impl_->profile_enabled) phase_start = Clock::now();
    SDL_RenderPresent(impl_->renderer);
    record_phase(impl_->timing.vsync_ns, impl_->timing.vsync_max_ns);
    if (impl_->frame_limit_enabled) {
        const std::uint64_t period_numerator = 1'000'000'000ULL * impl_->refresh_den + impl_->frame_remainder;
        impl_->next_frame += std::chrono::nanoseconds(period_numerator / impl_->refresh_num);
        impl_->frame_remainder = period_numerator % impl_->refresh_num;
        const auto now = Clock::now();
        if (impl_->next_frame > now) std::this_thread::sleep_until(impl_->next_frame);
        else impl_->next_frame = now;
    }
    record_phase(impl_->timing.limiter_ns, impl_->timing.limiter_max_ns);
    if (impl_->profile_enabled) ++impl_->timing.frames;
    return true;
}

SegaLiveInputs SegaPresenter::live_inputs() const { return impl_->inputs; }
bool SegaPresenter::has_input_focus() const {
    return (SDL_GetWindowFlags(impl_->window) & SDL_WINDOW_INPUT_FOCUS) != 0;
}
SegaPresenterTiming SegaPresenter::timing() const { return impl_->timing; }

} // namespace mamerecomp
