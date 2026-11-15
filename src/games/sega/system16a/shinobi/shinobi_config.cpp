#include "games/sega/system16a/shinobi/shinobi_config.hpp"

#include <imgui.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mamerecomp::games::sega::system16a::shinobi {
namespace {

struct Options {
    std::filesystem::path path;
    bool free_play = false;
    std::uint8_t coin_a = 0x0f;
    std::uint8_t coin_b = 0x0f;
    std::uint8_t lives = 3;
    std::uint8_t difficulty = 0x30;
    bool slow_enemy_bullets = true;
    bool english = true;
    bool cocktail = false;
    bool demo_sounds = true;

    std::uint8_t dsw1() const {
        return free_play ? 0x00 : static_cast<std::uint8_t>((coin_a & 0x0fU) | ((coin_b & 0x0fU) << 4));
    }
    std::uint8_t dsw2() const {
        std::uint8_t value = cocktail ? 0x01 : 0x00;
        value |= demo_sounds ? 0x00 : 0x02;
        value |= lives == 2 ? 0x08 : lives == 5 ? 0x04 : 0x0c;
        value |= difficulty & 0x30U;
        value |= slow_enemy_bullets ? 0x40 : 0x00;
        value |= english ? 0x00 : 0x80;
        return value;
    }
};

struct CoinOption { std::uint8_t value; const char* label; const char* id; };
constexpr std::array<CoinOption, 15> coin_options{{
    {0x07, "4 moedas / 1 credito", "4c1c"}, {0x08, "3 moedas / 1 credito", "3c1c"},
    {0x09, "2 moedas / 1 credito", "2c1c"}, {0x05, "2 moedas / 1 credito (5/3, 6/4)", "2c1c_5_3_6_4"},
    {0x04, "2 moedas / 1 credito (4/3)", "2c1c_4_3"}, {0x0f, "1 moeda / 1 credito", "1c1c"},
    {0x03, "1 moeda / 1 credito (5/6)", "1c1c_5_6"}, {0x02, "1 moeda / 1 credito (4/5)", "1c1c_4_5"},
    {0x01, "1 moeda / 1 credito (2/3)", "1c1c_2_3"}, {0x06, "2 moedas / 3 creditos", "2c3c"},
    {0x0e, "1 moeda / 2 creditos", "1c2c"}, {0x0d, "1 moeda / 3 creditos", "1c3c"},
    {0x0c, "1 moeda / 4 creditos", "1c4c"}, {0x0b, "1 moeda / 5 creditos", "1c5c"},
    {0x0a, "1 moeda / 6 creditos", "1c6c"},
}};

bool parse_bool(std::string_view value, bool fallback) {
    if (value == "1" || value == "true" || value == "on") return true;
    if (value == "0" || value == "false" || value == "off") return false;
    return fallback;
}

void load_options(const std::filesystem::path& path, Options& options) {
    std::ifstream input(path);
    std::string line;
    while (std::getline(input, line)) {
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const auto key = std::string_view(line).substr(0, equals);
        const auto value = std::string_view(line).substr(equals + 1);
        if (key == "free_play") options.free_play = parse_bool(value, options.free_play);
        else if (key == "coin_a" || key == "coin_b") {
            for (const auto& coin : coin_options) if (value == coin.id) {
                (key == "coin_a" ? options.coin_a : options.coin_b) = coin.value;
            }
        } else if (key == "lives") {
            if (value == "2") options.lives = 2;
            else if (value == "3") options.lives = 3;
            else if (value == "5") options.lives = 5;
        } else if (key == "difficulty") {
            if (value == "easy") options.difficulty = 0x20;
            else if (value == "normal") options.difficulty = 0x30;
            else if (value == "hard") options.difficulty = 0x10;
            else if (value == "very_hard") options.difficulty = 0x00;
        } else if (key == "slow_enemy_bullets") options.slow_enemy_bullets = parse_bool(value, options.slow_enemy_bullets);
        else if (key == "english") options.english = parse_bool(value, options.english);
        else if (key == "cocktail") options.cocktail = parse_bool(value, options.cocktail);
        else if (key == "demo_sounds") options.demo_sounds = parse_bool(value, options.demo_sounds);
    }
}

bool save_options(const Options& options) {
    std::error_code ec;
    std::filesystem::create_directories(options.path.parent_path(), ec);
    if (ec) return false;
    std::ofstream output(options.path, std::ios::trunc);
    if (!output) return false;
    const auto coin_id = [](std::uint8_t raw) -> const char* {
        for (const auto& coin : coin_options) if (coin.value == raw) return coin.id;
        return "1c1c";
    };
    const char* difficulty = options.difficulty == 0x20 ? "easy" : options.difficulty == 0x30 ? "normal"
        : options.difficulty == 0x10 ? "hard" : "very_hard";
    output << "# Shinobi game options; values are semantic menu choices.\n"
           << "free_play=" << (options.free_play ? 1 : 0) << '\n'
           << "coin_a=" << coin_id(options.coin_a & 0x0fU) << '\n'
           << "coin_b=" << coin_id(options.coin_b & 0x0fU) << '\n'
           << "lives=" << static_cast<unsigned>(options.lives) << '\n'
           << "difficulty=" << difficulty << '\n'
           << "slow_enemy_bullets=" << (options.slow_enemy_bullets ? 1 : 0) << '\n'
           << "english=" << (options.english ? 1 : 0) << '\n'
           << "cocktail=" << (options.cocktail ? 1 : 0) << '\n'
           << "demo_sounds=" << (options.demo_sounds ? 1 : 0) << '\n';
    return static_cast<bool>(output);
}

bool draw_game_tab(void* opaque) {
    auto& options = *static_cast<Options*>(opaque);
    bool changed = false;
    ImGui::TextDisabled("JOGABILIDADE E CONFIGURACAO ARCADE");
    ImGui::Spacing();
    changed |= ImGui::Checkbox("Free Play (Coin A + Coin B)", &options.free_play);
    if (ImGui::BeginTable("##shinobi-game-settings", 2,
            ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg, ImVec2(0.0f, 0.0f), 0.0f)) {
        ImGui::TableSetupColumn("Setting", ImGuiTableColumnFlags_WidthStretch, 0.95f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.05f);
        const auto row = [](const char* label) {
            ImGui::TableNextRow(); ImGui::TableSetColumnIndex(0); ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label); ImGui::TableSetColumnIndex(1);
        };
        const auto coin_combo = [&](const char* id, std::uint8_t& value) {
            int selected = 0;
            for (std::size_t i = 0; i < coin_options.size(); ++i) if (coin_options[i].value == value) selected = static_cast<int>(i);
            std::array<const char*, coin_options.size()> labels{};
            for (std::size_t i = 0; i < coin_options.size(); ++i) labels[i] = coin_options[i].label;
            if (!ImGui::Combo(id, &selected, labels.data(), static_cast<int>(labels.size()))) return false;
            value = coin_options[static_cast<std::size_t>(selected)].value;
            return true;
        };
        row("Coin A"); ImGui::BeginDisabled(options.free_play); changed |= coin_combo("##coin-a", options.coin_a); ImGui::EndDisabled();
        row("Coin B"); ImGui::BeginDisabled(options.free_play); changed |= coin_combo("##coin-b", options.coin_b); ImGui::EndDisabled();
        row("Vidas"); int lives_index = options.lives == 2 ? 0 : options.lives == 5 ? 2 : 1;
        constexpr const char* lives_items[] = {"2", "3", "5"};
        if (ImGui::Combo("##lives", &lives_index, lives_items, 3)) {
            constexpr std::uint8_t values[] = {2, 3, 5}; options.lives = values[lives_index]; changed = true;
        }
        row("Dificuldade"); int difficulty_index = options.difficulty == 0x20 ? 0 : options.difficulty == 0x30 ? 1 : options.difficulty == 0x10 ? 2 : 3;
        constexpr const char* difficulty_items[] = {"Facil", "Normal", "Dificil", "Muito dificil"};
        if (ImGui::Combo("##difficulty", &difficulty_index, difficulty_items, 4)) {
            constexpr std::uint8_t values[] = {0x20, 0x30, 0x10, 0x00}; options.difficulty = values[difficulty_index]; changed = true;
        }
        row("Velocidade dos tiros inimigos"); changed |= ImGui::Checkbox("Lentos##bullet-speed", &options.slow_enemy_bullets);
        row("Idioma"); changed |= ImGui::Checkbox("Ingles##language", &options.english);
        row("Gabinete"); changed |= ImGui::Checkbox("Cocktail##cabinet", &options.cocktail);
        row("Sons de demonstracao"); changed |= ImGui::Checkbox("Ativados##demo-sounds", &options.demo_sounds);
        ImGui::EndTable();
    }
    if (changed) (void)save_options(options);
    return changed;
}

const mamerecomp::sega::GameInputBinding input_bindings[] = {
    {mamerecomp::sega::GameInputPort::Service, 0, "5", "Ficha 1"},
    {mamerecomp::sega::GameInputPort::Service, 1, "6", "Ficha 2"},
    {mamerecomp::sega::GameInputPort::Service, 2, "F2", "Servico"},
    {mamerecomp::sega::GameInputPort::Service, 4, "1", "Iniciar 1"},
    {mamerecomp::sega::GameInputPort::Service, 5, "2", "Iniciar 2"},
    {mamerecomp::sega::GameInputPort::Player1, 1, "K", "Botao 1"},
    {mamerecomp::sega::GameInputPort::Player1, 2, "L", "Botao 2"},
    {mamerecomp::sega::GameInputPort::Player1, 0, "J", "Botao 3"},
    {mamerecomp::sega::GameInputPort::Player1, 4, "S", "Baixo"},
    {mamerecomp::sega::GameInputPort::Player1, 5, "W", "Cima"},
    {mamerecomp::sega::GameInputPort::Player1, 6, "D", "Direita"},
    {mamerecomp::sega::GameInputPort::Player1, 7, "A", "Esquerda"},
    {mamerecomp::sega::GameInputPort::Player2, 0, "Keypad 3", "P2 Botao 3"},
};

const mamerecomp::sega::GameInputBinding* get_input_bindings(std::size_t* count) {
    if (count) *count = std::size(input_bindings);
    return input_bindings;
}

void* create_options(const char* path, const char* legacy_path) {
    auto* options = new Options;
    options->path = path ? path : "shinobi.game.ini";
    if (std::filesystem::exists(options->path)) {
        load_options(options->path, *options);
    } else if (legacy_path && std::filesystem::exists(legacy_path)) {
        load_options(legacy_path, *options); // migrate settings from the old combined INI
        (void)save_options(*options);
    }
    return options;
}

void destroy_options(void* state) { delete static_cast<Options*>(state); }
std::uint8_t options_dsw1(const void* state) { return static_cast<const Options*>(state)->dsw1(); }
std::uint8_t options_dsw2(const void* state) { return static_cast<const Options*>(state)->dsw2(); }

const mamerecomp::sega::GameSettingsApi api{1, create_options, destroy_options, draw_game_tab,
    get_input_bindings, options_dsw1, options_dsw2};

} // namespace

void select_lives(std::string_view profile_id, std::string_view option, LivesSetting& setting) {
    if (profile_id != "shinobi") throw std::runtime_error("--lives is currently defined only for the Shinobi game module");
    if (option == "2") setting.dip_bits = 0x08;
    else if (option == "3") setting.dip_bits = 0x0c;
    else if (option == "5" || option == "infinite") setting.dip_bits = 0x04;
    else throw std::runtime_error("--lives must be 2, 3, 5, or infinite");
    setting.infinite = option == "infinite";
}
void select_coin_mode(std::string_view option, bool lives_option_set, LivesSetting& setting, bool& freeplay) {
    if (option == "freeplay") freeplay = true;
    else if (option == "coin") { freeplay = false; if (!lives_option_set) setting.dip_bits = 0x0c; setting.infinite = false; }
    else throw std::runtime_error("--coin-mode must be freeplay or coin");
}
void apply_coin_mode_to_dsw(bool freeplay, std::optional<std::uint8_t>& dsw1_override) {
    if (freeplay && !dsw1_override) dsw1_override = 0x00;
}
std::uint8_t lives_count(const LivesSetting& setting) { return setting.dip_bits == 0x08 ? 2 : setting.dip_bits == 0x04 ? 5 : 3; }
const char* lives_mode_name(std::uint8_t dsw2, bool infinite) {
    const auto lives = dsw2 & 0x0cU;
    if (infinite && lives == 0x04U) return "infinite lives (5-lives DIP)";
    if (lives == 0x08U) return "2 lives";
    if (lives == 0x04U) return "5 lives";
    if (lives == 0x0cU) return "3 lives";
    return "Free Play";
}

const mamerecomp::sega::GameSettingsApi* game_settings_api() { return &api; }

} // namespace mamerecomp::games::sega::system16a::shinobi

extern "C" __attribute__((visibility("default")))
const mamerecomp::sega::GameSettingsApi* mamerecomp_game_settings_api() {
    return mamerecomp::games::sega::system16a::shinobi::game_settings_api();
}
