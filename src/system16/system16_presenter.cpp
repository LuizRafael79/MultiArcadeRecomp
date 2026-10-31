#include "system16_video.hpp"
#include "system16_config.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <stdexcept>
#include <string>
#include <thread>

namespace mamerecomp {
namespace {

struct CoinOption { std::uint8_t value; const char* label; };
constexpr std::array<CoinOption, 15> coin_options{{
    {0x07, "4 moedas / 1 credito"},
    {0x08, "3 moedas / 1 credito"},
    {0x09, "2 moedas / 1 credito"},
    {0x05, "2 moedas / 1 credito (5/3, 6/4)"},
    {0x04, "2 moedas / 1 credito (4/3)"},
    {0x0f, "1 moeda / 1 credito"},
    {0x03, "1 moeda / 1 credito (5/6)"},
    {0x02, "1 moeda / 1 credito (4/5)"},
    {0x01, "1 moeda / 1 credito (2/3)"},
    {0x06, "2 moedas / 3 creditos"},
    {0x0e, "1 moeda / 2 creditos"},
    {0x0d, "1 moeda / 3 creditos"},
    {0x0c, "1 moeda / 4 creditos"},
    {0x0b, "1 moeda / 5 creditos"},
    {0x0a, "1 moeda / 6 creditos"},
}};

bool coin_combo(const char* label, std::uint8_t& value) {
    int selected = 0;
    for (std::size_t i = 0; i < coin_options.size(); ++i) {
        if (coin_options[i].value == value) { selected = static_cast<int>(i); break; }
    }
    std::array<const char*, coin_options.size()> labels{};
    for (std::size_t i = 0; i < coin_options.size(); ++i) labels[i] = coin_options[i].label;
    if (!ImGui::Combo(label, &selected, labels.data(), static_cast<int>(labels.size()))) return false;
    value = coin_options[static_cast<std::size_t>(selected)].value;
    return true;
}

bool begin_settings_table(const char* id) {
    if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg,
            ImVec2(0.0f, 0.0f), 0.0f)) return false;
    ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 0.95f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.05f);
    return true;
}

void setting_row_label(const char* label) {
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    ImGui::TableSetColumnIndex(1);
}

} // namespace

struct System16APresenter::Impl {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    std::chrono::steady_clock::time_point next_frame = std::chrono::steady_clock::now();
    System16ALiveInputs inputs;

    explicit Impl(const std::string& title) {
        if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
            throw std::runtime_error(std::string("SDL video init failed: ") + SDL_GetError());
        window = SDL_CreateWindow(title.c_str(), 960, 672, SDL_WINDOW_RESIZABLE);
        if (!window) { SDL_QuitSubSystem(SDL_INIT_VIDEO); throw std::runtime_error(std::string("SDL window creation failed: ") + SDL_GetError()); }
        renderer = SDL_CreateRenderer(window, nullptr);
        if (!renderer) { SDL_DestroyWindow(window); SDL_QuitSubSystem(SDL_INIT_VIDEO); throw std::runtime_error(std::string("SDL renderer creation failed: ") + SDL_GetError()); }
        SDL_SetRenderVSync(renderer, 1);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
            System16AFrame::width, System16AFrame::height);
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

System16APresenter::System16APresenter(std::string title) : impl_(std::make_unique<Impl>(title)) {}
System16APresenter::~System16APresenter() = default;

bool System16APresenter::show_configuration_menu(System16GameSettings& settings,
    const std::filesystem::path& settings_path, const std::string& game_title) {
    if (!SDL_SetWindowFullscreen(impl_->window, settings.fullscreen))
        throw std::runtime_error(std::string("SDL fullscreen configuration failed: ") + SDL_GetError());
    if (!settings.fullscreen) {
        SDL_SetWindowSize(impl_->window, 320 * settings.window_scale, 224 * settings.window_scale);
        SDL_SetWindowPosition(impl_->window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    }
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
                ImGui::TextDisabled("JOGABILIDADE E CONFIGURACAO ARCADE");
                ImGui::Spacing();
                changed |= ImGui::Checkbox("Free Play (Coin A + Coin B)", &settings.free_play);
                if (begin_settings_table("##game-settings")) {
                    setting_row_label("Coin A");
                    ImGui::BeginDisabled(settings.free_play);
                    changed |= coin_combo("##coin-a", settings.coin_a);
                    ImGui::EndDisabled();
                    setting_row_label("Coin B");
                    ImGui::BeginDisabled(settings.free_play);
                    changed |= coin_combo("##coin-b", settings.coin_b);
                    ImGui::EndDisabled();

                    setting_row_label("Vidas");
                    int lives_index = settings.lives == 2 ? 0 : settings.lives == 5 ? 2 : 1;
                    constexpr const char* lives_items[] = {"2", "3", "5"};
                    if (ImGui::Combo("##lives", &lives_index, lives_items, 3)) {
                        constexpr std::uint8_t lives_values[] = {2, 3, 5};
                        settings.lives = lives_values[lives_index]; changed = true;
                    }
                    setting_row_label("Dificuldade");
                    int difficulty_index = settings.difficulty == 0x20 ? 0 : settings.difficulty == 0x30 ? 1 : settings.difficulty == 0x10 ? 2 : 3;
                    constexpr const char* difficulty_items[] = {"Facil", "Normal", "Dificil", "Muito dificil"};
                    if (ImGui::Combo("##difficulty", &difficulty_index, difficulty_items, 4)) {
                        constexpr std::uint8_t difficulty_values[] = {0x20, 0x30, 0x10, 0x00};
                        settings.difficulty = difficulty_values[difficulty_index]; changed = true;
                    }
                    setting_row_label("Velocidade dos tiros inimigos");
                    changed |= ImGui::Checkbox("Lentos##bullet-speed", &settings.slow_enemy_bullets);
                    setting_row_label("Idioma");
                    changed |= ImGui::Checkbox("Ingles##language", &settings.english);
                    setting_row_label("Gabinete");
                    changed |= ImGui::Checkbox("Cocktail##cabinet", &settings.cocktail);
                    setting_row_label("Sons de demonstracao");
                    changed |= ImGui::Checkbox("Ativados##demo-sounds", &settings.demo_sounds);
                    ImGui::EndTable();
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Video")) {
                ImGui::Spacing();
                ImGui::TextDisabled("VIDEO E APRESENTACAO");
                ImGui::Spacing();
                if (system16a_opengl_backend_available()) {
                    int backend = settings.video_backend == System16AVideoBackend::OpenGL ? 1 : 0;
                    constexpr const char* backend_items[] = {"Software (CPU)", "OpenGL (compute)"};
                    if (ImGui::Combo("Renderer", &backend, backend_items, 2)) {
                        settings.video_backend = backend == 1 ? System16AVideoBackend::OpenGL : System16AVideoBackend::Software;
                        changed = true;
                    }
                    ImGui::TextWrapped("OpenGL acelera a composicao dos tilemaps; sprites e paleta final ainda sao processados pela CPU.");
                } else {
                    settings.video_backend = System16AVideoBackend::Software;
                    ImGui::Text("Compositor: Software (CPU)");
                    ImGui::TextDisabled("O backend OpenGL nao foi compilado neste sistema.");
                }
                changed |= ImGui::Checkbox("Tela cheia", &settings.fullscreen);
                if (ImGui::IsItemEdited()) SDL_SetWindowFullscreen(impl_->window, settings.fullscreen);
                ImGui::BeginDisabled(settings.fullscreen);
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
                ImGui::TextDisabled("TECLADO");
                ImGui::Spacing();
                ImGui::BulletText("Mover: W A S D");
                ImGui::BulletText("Ataque: K / L");
                ImGui::BulletText("Ninjutsu: J");
                ImGui::BulletText("Ficha: 5    Iniciar: 1");
                ImGui::TextDisabled("Remapeamento dos controles fica para uma proxima etapa.");
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
            draw->AddText(ImGui::GetFont(), 31.0f, ImVec2(logo_pos.x, logo_pos.y - 2.0f), IM_COL32(222, 232, 244, 255), "SHINOBI");
            draw->AddText(ImGui::GetFont(), 12.0f, ImVec2(logo_pos.x + 174.0f, logo_pos.y + 13.0f), IM_COL32(39, 205, 224, 255), "SEGA");
            draw->AddRectFilled(ImVec2(logo_pos.x, logo_pos.y + 38.0f), ImVec2(logo_pos.x + 210.0f, logo_pos.y + 41.0f), IM_COL32(185, 18, 47, 255));
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
            save_status = save_system16_settings(settings_path, settings, &error) ? "Configuracao salva" : error;
        }
        ImGui::Render();
        SDL_SetRenderDrawColor(impl_->renderer, 6, 10, 17, 255);
        SDL_RenderClear(impl_->renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), impl_->renderer);
        SDL_RenderPresent(impl_->renderer);
    }

    if (start_game) {
        std::string error;
        if (!save_system16_settings(settings_path, settings, &error)) save_status = error;
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    return start_game;
}

bool System16APresenter::present(const System16AFrame& frame, std::span<const std::uint8_t> palette_ram) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_QUIT) return false;
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) return false;
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    auto press = [&](std::uint8_t& port, SDL_Scancode key, unsigned bit) {
        if (keys[key]) port = static_cast<std::uint8_t>(port & ~(1U << bit));
    };
    impl_->inputs = {};
    press(impl_->inputs.service, SDL_SCANCODE_5, 0); // Coin 1
    press(impl_->inputs.service, SDL_SCANCODE_6, 1); // Coin 2
    press(impl_->inputs.service, SDL_SCANCODE_F2, 2); // Service mode
    press(impl_->inputs.service, SDL_SCANCODE_1, 4); // Start 1
    press(impl_->inputs.service, SDL_SCANCODE_2, 5); // Start 2
    press(impl_->inputs.p1, SDL_SCANCODE_K, 1); // Button 1
    press(impl_->inputs.p1, SDL_SCANCODE_L, 2); // Button 2
    press(impl_->inputs.p1, SDL_SCANCODE_J, 0); // Button 3 / ninjutsu
    press(impl_->inputs.p1, SDL_SCANCODE_S, 4); // Down
    press(impl_->inputs.p1, SDL_SCANCODE_W, 5); // Up
    press(impl_->inputs.p1, SDL_SCANCODE_D, 6); // Right
    press(impl_->inputs.p1, SDL_SCANCODE_A, 7); // Left
    press(impl_->inputs.p2, SDL_SCANCODE_KP_3, 0); // Player 2 button 3 / ninjutsu
    const auto rgb = system16a_frame_rgb(frame, palette_ram);
    if (!SDL_UpdateTexture(impl_->texture, nullptr, rgb.data(), System16AFrame::width * 3))
        throw std::runtime_error(std::string("SDL frame upload failed: ") + SDL_GetError());
    SDL_SetRenderDrawColor(impl_->renderer, 0, 0, 0, 255);
    SDL_RenderClear(impl_->renderer);
    int output_w = 0, output_h = 0;
    SDL_GetCurrentRenderOutputSize(impl_->renderer, &output_w, &output_h);
    SDL_FRect destination{};
    const float scale = std::min(static_cast<float>(output_w) / System16AFrame::width, static_cast<float>(output_h) / System16AFrame::height);
    destination.w = System16AFrame::width * scale;
    destination.h = System16AFrame::height * scale;
    destination.x = (output_w - destination.w) * 0.5f;
    destination.y = (output_h - destination.h) * 0.5f;
    SDL_RenderTexture(impl_->renderer, impl_->texture, nullptr, &destination);
    SDL_RenderPresent(impl_->renderer);
    constexpr auto frame_duration = std::chrono::nanoseconds(1'000'000'000 / 60);
    impl_->next_frame += frame_duration;
    const auto now = std::chrono::steady_clock::now();
    if (impl_->next_frame > now) std::this_thread::sleep_until(impl_->next_frame);
    else impl_->next_frame = now;
    return true;
}

System16ALiveInputs System16APresenter::live_inputs() const { return impl_->inputs; }
bool system16a_presenter_available() { return true; }
} // namespace mamerecomp
