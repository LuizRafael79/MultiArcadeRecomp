#include "runtime/sega/game_audio_api.hpp"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <iterator>

namespace {
using multiarcaderecomp::sega::GameAudioApi;
using multiarcaderecomp::sega::GameAudioHostApi;

enum class Fade : std::uint8_t { None, In, Out };
struct State {
    int current_track = -1;
    bool stage7_seen = false;
    bool stage9_waiting_for_c0 = false;
    bool native_audio_passthrough = false;
    bool chardecide_started = false;
    Fade fade = Fade::None;
    int fade_last = -1;
    int fade_start = 0x3f;
    int fade_floor = 0x20;
    float gain = 1.0f;
};
State state;

constexpr const char* track_names[] = {
    "stage1.mp3", "characterselect.mp3", "bossfight.mp3", "thief.mp3",
    "stage3.mp3", "stage5.mp3", "stage7.mp3", "stage9.mp3", "deathadder.mp3",
    "ending-map.mp3", "chardecide.mp3", "gameover.mp3"};

bool start_track(const GameAudioHostApi& host, int track, bool restart = true) {
    if (!host.play_track || track < 0 || track >= static_cast<int>(std::size(track_names))) return false;
    if (!restart && state.current_track == track) return true;
    if (!host.play_track(host.context, track_names[track])) return false;
    state.current_track = track;
    state.fade = Fade::None;
    state.fade_last = -1;
    state.gain = 1.0f;
    if (host.set_track_gain) host.set_track_gain(host.context, state.gain);
    return true;
}

bool on_sound_command(std::uint8_t command, const GameAudioHostApi* host_ptr) {
    if (!host_ptr) return false;
    const auto& host = *host_ptr;
    const bool filter_character_decision_voices = command == 0x9f;

    if (state.native_audio_passthrough && command == 0xc1) {
        state.native_audio_passthrough = false;
        std::cout << "Arranged BGM restored at command 0xc1\n";
        return false;
    }
    if (command == 0x9f) {
        if (!state.chardecide_started && host.play_one_shot
            && host.play_one_shot(host.context, track_names[10])) {
            state.chardecide_started = true;
            std::cout << "Arranged chardecide one-shot started over current BGM\n";
        } else {
            std::cout << "Arranged chardecide one-shot already used or unavailable; current BGM continues\n";
        }
        return filter_character_decision_voices;
    }
    if (state.native_audio_passthrough) return false;
    if (state.current_track == 1 && command == 0x3f) return false;

    const bool continuing_fade_in = state.fade == Fade::In && state.fade_last >= 0x3e;
    if (command == 0x3f && !continuing_fade_in) {
        state.fade = Fade::Out;
        state.fade_last = 0x3f;
        state.fade_start = 0x3f;
        state.fade_floor = 0x20;
        state.gain = 1.0f;
        std::cout << "Arranged BGM fade-out started at command 0x3f\n";
        return false;
    }
    if (command == 0x3e && state.current_track >= 0
        && (state.fade == Fade::None || (state.fade == Fade::In && state.fade_last == 0x24))) {
        state.fade = Fade::Out;
        state.fade_last = 0x3e;
        state.fade_start = 0x3e;
        state.fade_floor = 0x26;
        state.gain = 1.0f;
        std::cout << "Arranged BGM reverse fade-out started at command 0x3e\n";
        return false;
    }
    if (state.fade == Fade::Out && command >= state.fade_floor && command < state.fade_start
        && command < state.fade_last) {
        state.fade_last = command;
        state.gain = static_cast<float>(command - state.fade_floor)
            / static_cast<float>(state.fade_start - state.fade_floor);
        if (host.set_track_gain) host.set_track_gain(host.context, state.gain);
        std::cout << "Arranged BGM fade step 0x" << std::hex << static_cast<unsigned>(command)
                  << std::dec << " gain=" << state.gain << '\n';
        if (command == state.fade_floor) {
            const bool deathadder_track = state.current_track == 8;
            if (host.stop_track) host.stop_track(host.context);
            state.current_track = -1;
            state.fade = Fade::None;
            state.gain = 1.0f;
            if (deathadder_track && state.fade_floor == 0x20) {
                state.native_audio_passthrough = true;
                std::cout << "Death Adder arranged track faded out; native audio passthrough enabled\n";
            }
        }
        return false;
    }
    if (state.fade == Fade::None && state.current_track >= 0 && command == 0x26) {
        state.fade = Fade::In;
        state.fade_last = 0x24;
        state.gain = 0.0f;
        if (host.set_track_gain) host.set_track_gain(host.context, state.gain);
    }
    if (state.fade == Fade::In && command >= 0x26 && command <= 0x40
        && (command & 1U) == 0 && command > state.fade_last) {
        state.fade_last = command;
        state.gain = static_cast<float>(command - 0x26) / (0x40 - 0x26);
        if (host.set_track_gain) host.set_track_gain(host.context, state.gain);
        std::cout << "Arranged BGM fade-in step 0x" << std::hex << static_cast<unsigned>(command)
                  << std::dec << " gain=" << state.gain << '\n';
        if (command == 0x40) state.fade = Fade::None;
        return false;
    }

    int track = -1;
    if (command == 0xc0) {
        if (state.stage9_waiting_for_c0) {
            track = 7;
            state.stage9_waiting_for_c0 = false;
        } else track = 2;
    } else if (command == 0xc1) track = 0;
    else if (command == 0xc2) track = 8;
    else if (command == 0xc3) track = 4;
    else if (command == 0xc4) track = 11;
    else if (command == 0xc5 || command == 0xcc) track = 1;
    else if (command == 0xc6) {
        track = 3;
        if (state.stage7_seen) state.stage9_waiting_for_c0 = true;
    } else if (command == 0xc7 || command == 0xc9) track = 9;
    else if (command == 0xc8) track = 5;
    else if (command == 0xcb) {
        track = 6;
        state.stage7_seen = true;
        state.stage9_waiting_for_c0 = false;
    }
    std::cout << "Arranged sound latch 0x" << std::hex << static_cast<unsigned>(command) << std::dec;
    if (track >= 0) std::cout << " -> " << track_names[track];
    else std::cout << " (no arranged track mapping)";
    if (filter_character_decision_voices) std::cout << " (native chardecide YM slots filtered)";
    std::cout << '\n';
    if (track >= 0 && track != state.current_track) (void)start_track(host, track);
    return false;
}

const GameAudioApi api{1, on_sound_command};
}

extern "C" const multiarcaderecomp::sega::GameAudioApi* multiarcaderecomp_game_audio_api() {
    return &api;
}
