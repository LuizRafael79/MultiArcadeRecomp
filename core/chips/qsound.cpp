// license:BSD-3-Clause
// Copyright (c) 2026 MameRecomp contributors.
// Peripheral model adapted from MAME src/devices/sound/qsound.cpp and
// qsoundhle.cpp. MAME attribution: Vas Crabb (LLE), superctr and Valley Bell
// (HLE); see core/third_party/qsound-hle/qsound.c for the HLE source notice.

#include "qsound.hpp"

#include "cpus/dsp16/dsp16a.hpp"

#include <algorithm>
#include <array>
#include <deque>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <vector>

namespace mamerecomp::chips {
using dsp16::Dsp16A;
using dsp16::Dsp16ABus;

namespace {
// Direct port of MAME qsoundhle.cpp's device state and sample update path.
struct MameHle {
    static constexpr std::uint16_t boot = 0x000, init1 = 0x288, init2 = 0x61a;
    static constexpr std::uint16_t refresh1 = 0x039, refresh2 = 0x04f;
    static constexpr std::uint16_t normal1 = 0x314, normal2 = 0x6b2;
    static constexpr std::uint16_t pan_tab = 0x110, adpcm_tab = 0x9dc;
    static constexpr std::uint16_t filter_tab = 0xd53, delay_base = 0x554, delay_base2 = 0x53c;
    struct Voice { std::uint16_t bank=0x8000, phase=0, rate=0; std::int16_t addr=0, loop=0, end=0, volume=0, echo=0; };
    struct Adpcm { std::uint16_t start=0, end=0, bank=0x8000, flag=0, cur_addr=0; std::int16_t volume=0, cur_volume=0, step=0; };
    struct Fir { int taps_count=0, pos=0; std::uint16_t table_pos=0; std::array<std::int16_t,95> taps{}, delay{}; };
    struct Delay { std::int16_t delay=0, volume=0, write=0, read=0; std::array<std::int16_t,51> line{}; };
    struct Echo { std::uint16_t end=0; std::int16_t feedback=0, length=0, last=0, pos=0; std::array<std::int16_t,1024> line{}; };
    std::array<std::uint16_t,0x1000> rom{};
    std::array<Voice,16> voice{};
    std::array<Adpcm,3> adpcm{};
    std::array<std::uint16_t,19> pan{};
    std::array<std::int16_t,19> output{};
    std::array<Fir,2> filter{}, alt_filter{};
    std::array<Delay,2> wet{}, dry{};
    Echo echo{};
    std::uint16_t delay_update=0, next_state=init1, state=boot, data_latch=0;
    int counter=0, ready=0;
    std::array<std::int16_t,2> out{};
    std::array<std::uint16_t*,256> reg{};
    std::span<const std::uint8_t> samples{};

    void load_firmware(std::span<const std::uint16_t> words) {
        rom.fill(0);
        std::copy_n(words.begin(), std::min(words.size(), rom.size()), rom.begin());
    }
    std::int16_t dsp_rom(std::uint16_t a) const { return static_cast<std::int16_t>(rom[a & 0xfff]); }
    std::int16_t sample(std::uint16_t bank, std::uint16_t address) const {
        if (samples.empty() || !(bank & 0x8000U)) return 0;
        const std::size_t a = ((std::size_t(bank & 0x7fffU) << 16) | address) % samples.size();
        return static_cast<std::int16_t>(std::uint16_t(samples[a]) << 8);
    }
    void reset() { ready=0; out={}; state=boot; counter=0; }
    void init_map() {
        reg.fill(nullptr);
        for (int i=0;i<16;i++) {
            reg[(i<<3)+0]=reinterpret_cast<std::uint16_t*>(&voice[(i+1)%16].bank);
            reg[(i<<3)+1]=reinterpret_cast<std::uint16_t*>(&voice[i].addr);
            reg[(i<<3)+2]=reinterpret_cast<std::uint16_t*>(&voice[i].rate);
            reg[(i<<3)+3]=reinterpret_cast<std::uint16_t*>(&voice[i].phase);
            reg[(i<<3)+4]=reinterpret_cast<std::uint16_t*>(&voice[i].loop);
            reg[(i<<3)+5]=reinterpret_cast<std::uint16_t*>(&voice[i].end);
            reg[(i<<3)+6]=reinterpret_cast<std::uint16_t*>(&voice[i].volume);
            reg[i+0x80]=&pan[i]; reg[i+0xba]=reinterpret_cast<std::uint16_t*>(&voice[i].echo);
        }
        for (int i=0;i<3;i++) {
            reg[(i<<2)+0xca]=&adpcm[i].start; reg[(i<<2)+0xcb]=&adpcm[i].end;
            reg[(i<<2)+0xcc]=&adpcm[i].bank; reg[(i<<2)+0xcd]=reinterpret_cast<std::uint16_t*>(&adpcm[i].volume);
            reg[i+0xd6]=&adpcm[i].flag; reg[i+0x90]=&pan[16+i];
        }
        reg[0x93]=reinterpret_cast<std::uint16_t*>(&echo.feedback); reg[0xd9]=&echo.end;
        reg[0xe2]=&delay_update; reg[0xe3]=&next_state;
        for (int i=0;i<2;i++) {
            reg[(i<<1)+0xda]=&filter[i].table_pos; reg[(i<<1)+0xdb]=&alt_filter[i].table_pos;
            reg[(i<<1)+0xde]=reinterpret_cast<std::uint16_t*>(&wet[i].delay);
            reg[(i<<1)+0xdf]=reinterpret_cast<std::uint16_t*>(&dry[i].delay);
            reg[(i<<1)+0xe4]=reinterpret_cast<std::uint16_t*>(&wet[i].volume);
            reg[(i<<1)+0xe5]=reinterpret_cast<std::uint16_t*>(&dry[i].volume);
        }
    }
    void write(std::uint8_t a, std::uint16_t data) { if (reg[a]) *reg[a]=data; ready=0; }
    std::int16_t voice_update(Voice& v, std::int32_t& echo_in) {
        const std::int16_t result=std::int16_t((std::int32_t(v.volume)*sample(v.bank,std::uint16_t(v.addr)))>>14);
        echo_in += (std::int32_t(result)*v.echo)<<2;
        std::int32_t phase=v.rate+((std::int32_t(v.addr)<<12)|(v.phase>>4));
        if ((phase>>12)>=v.end) phase-=std::int32_t(v.loop)<<12;
        phase=std::clamp(phase,-0x8000000,0x7ffffff);
        v.addr=std::int16_t(phase>>12); v.phase=std::uint16_t((phase<<4)&0xffff);
        return result;
    }
    std::int16_t adpcm_update(Adpcm& v, std::int16_t current, int nibble) {
        int step;
        if (!nibble) {
            if (v.cur_addr==v.end) v.cur_volume=0;
            if (v.flag) { current=0; v.flag=0; v.step=10; v.cur_volume=v.volume; v.cur_addr=v.start; }
            step=sample(v.bank,v.cur_addr)>>8;
        } else step=sample(v.bank,v.cur_addr++)>>4;
        step=std::int8_t(step); step >>=4;
        std::int32_t delta=((1+std::abs(step<<1))*v.step)>>1;
        if (step<=0) delta=-delta;
        delta=std::clamp(delta+current,-32768,32767);
        v.step=std::int16_t((dsp_rom(adpcm_tab+8+step)*v.step)>>6);
        v.step=std::clamp<std::int16_t>(v.step,1,2000);
        return std::int16_t((delta*v.cur_volume)>>16);
    }
    std::int32_t fir_apply(Fir& f, std::int16_t input) {
        std::int32_t result=0; int tap=0;
        for (;tap<f.taps_count-1;tap++) {
            result-=(f.taps[tap]*f.delay[f.pos++])<<2;
            if (f.pos>=f.taps_count-1) f.pos=0;
        }
        result-=(f.taps[tap]*input)<<2;
        f.delay[f.pos++]=input; if (f.pos>=f.taps_count-1) f.pos=0;
        return result;
    }
    std::int32_t delay_apply(Delay& d, std::int32_t input) {
        d.line[d.write++]=std::int16_t(input>>16); if(d.write>=51)d.write=0;
        const std::int32_t result=d.line[d.read++]*d.volume; if(d.read>=51)d.read=0; return result;
    }
    void delay_sync(Delay& d) { int p=(d.write-d.delay)%51; d.read=std::int16_t(p<0?p+51:p); }
    std::int16_t echo_apply(std::int32_t input) {
        std::int32_t old=echo.line[echo.pos]; const int last=echo.last; echo.last=std::int16_t(old); old=(old+last)>>1;
        echo.line[echo.pos++]=std::int16_t((input+((old*echo.feedback)<<2))>>16);
        if(echo.pos>=echo.length)echo.pos=0; return std::int16_t(old);
    }
    void update() {
        if(state==boot||state==init1||state==init2) { state_init(); return; }
        if(state==refresh1) { refresh_filter1(); return; }
        if(state==refresh2) { refresh_filter2(); return; }
        normal_update();
    }
    void state_init() {
        const bool mode=state==init2;
        if(counter>=2){counter=0;state=next_state;return;}
        if(counter==1){counter++;return;}
        voice={}; adpcm={}; filter={}; alt_filter={}; wet={}; dry={}; echo={}; output={};
        for(int i=0;i<19;i++)pan[i]=pan_tab+0x10;
        for(int i=0;i<16;i++)voice[i].bank=0x8000;
        for(int i=0;i<3;i++)adpcm[i].bank=0x8000;
        if(!mode){wet[0].delay=0;dry[0].delay=46;wet[1].delay=0;dry[1].delay=48;
            filter[0].table_pos=filter_tab+95;filter[1].table_pos=filter_tab+190;echo.end=delay_base+6;next_state=refresh1;}
        else {wet[0].delay=1;dry[0].delay=0;wet[1].delay=0;dry[1].delay=0;
            filter[0].table_pos=0xf73;filter[1].table_pos=0xfa4;alt_filter[0].table_pos=0xf73;alt_filter[1].table_pos=0xfa4;
            echo.end=delay_base2+6;next_state=refresh2;}
        for(int i=0;i<2;i++)wet[i].volume=dry[i].volume=0x3fff;
        delay_update=1;ready=0;counter=1;
    }
    void refresh_filter1() {
        for(auto& f:filter){f.pos=0;f.taps_count=95;for(int i=0;i<95;i++)f.taps[i]=dsp_rom(f.table_pos+i);}
        state=next_state=normal1;
    }
    void refresh_filter2() {
        for(int ch=0;ch<2;ch++){auto& f=filter[ch];f.pos=0;f.taps_count=45;
            for(int i=0;i<45;i++)f.taps[i]=dsp_rom(f.table_pos+i);
            auto& a=alt_filter[ch];a.pos=0;a.taps_count=44;
            for(int i=0;i<44;i++)a.taps[i]=dsp_rom(a.table_pos+i);}
        state=next_state=normal2;
    }
    void normal_update() {
        ready=0x80; echo.length=std::int16_t(std::clamp<int>(echo.end-(state==normal2?delay_base2:delay_base),0,1024));
        std::int32_t echo_in=0;
        for(int i=0;i<16;i++)output[i]=voice_update(voice[i],echo_in);
        const int avoice=counter%3;output[16+avoice]=adpcm_update(adpcm[avoice],output[16+avoice],counter/3);
        const std::int16_t echo_out=echo_apply(echo_in);
        for(int ch=0;ch<2;ch++){
            std::int32_t wet_in=ch?std::int32_t(echo_out)<<14:0,dry_in=ch?0:std::int32_t(echo_out)<<14;
            for(int i=0;i<19;i++){const auto p=std::uint16_t(pan[i]+ch*196);
                dry_in-=std::int32_t(output[i])*dsp_rom(p); wet_in-=std::int32_t(output[i])*dsp_rom(p+98);}
            dry_in=std::clamp(dry_in,-0x1fffffff,0x1fffffff)<<2; wet_in=std::clamp(wet_in,-0x1fffffff,0x1fffffff)<<2;
            wet_in=fir_apply(filter[ch],std::int16_t(wet_in>>16));
            if(state==normal2)dry_in=fir_apply(alt_filter[ch],std::int16_t(dry_in>>16));
            std::int32_t out=delay_apply(wet[ch],wet_in)+delay_apply(dry[ch],dry_in);
            out=(out+0x2000)&~0x3fff; this->out[ch]=std::int16_t(std::clamp(out>>14,-0x7fff,0x7fff));
            if(delay_update){delay_sync(wet[ch]);delay_sync(dry[ch]);}
        }
        delay_update=0;if(++counter>5){counter=0;state=next_state;}
    }
};
} // namespace

struct QSound::Impl {
    struct SyncEvent {
        enum class Kind : std::uint8_t { Command, Ready } kind;
        std::uint16_t address=0, data=0;
    };
    explicit Impl(QSoundConfig cfg)
        : config(cfg), dsp(make_dsp_bus()) {
        if (const char* path = std::getenv("MAMERECOMP_QSOUND_DSP_TRACE")) {
            dsp_trace.open(path, std::ios::out | std::ios::trunc);
            if (!dsp_trace)
                throw std::runtime_error("could not create QSound DSP trace " + std::string(path));
            dsp_trace << "phase,cycle,pc,opcode,auc,psw,pt,r0,r1,r2,r3,x,y,p,a0,a1\n";
        }
        mame_hle.init_map();
        if (!config.firmware.empty()) {
            mame_hle.load_firmware(config.firmware);
            use_mame_hle = true;
        }
        if (config.mode == QSoundMode::Lle) {
            if (!config.firmware.empty())
                dsp.set_firmware(config.firmware.first(std::min(config.firmware.size(),
                    Dsp16A::internal_program_words)));
            dsp.reset();
        }
    }

    Dsp16ABus make_dsp_bus() {
        Dsp16ABus bus;
        bus.program_read = [this](std::uint16_t offset) {
            ++sample_read_count;
            // MAME's dsp_sample_r always reads the current bank/offset. Bit 15
            // selects the address half through the PIO bank latch; it is not
            // a valid/invalid flag. The first read after reset is real too.
            const std::uint32_t address = (std::uint32_t(rom_bank) << 16) | rom_offset;
            const std::uint8_t byte = sample_rom.empty() ? 0 : sample_rom[address % sample_rom.size()];
            if (byte) ++nonzero_sample_read_count;
            rom_bank = (rom_bank & 0x8000U) | (offset & 0x7fffU);
            return static_cast<std::uint16_t>(std::uint16_t(byte) << 8);
        };
        bus.pio_read = [this](std::uint16_t /*selector*/) {
            ++pio_read_count;
            const bool address_pending = command_pending;
            const std::uint16_t value = address_pending ? command_address : command_data;
            if (std::getenv("MAMERECOMP_QSOUND_TRACE") && pio_read_count <= 512)
                std::cerr << "QSound DSP PIO read #" << pio_read_count
                          << " PC=0x" << std::hex << dsp.pc()
                          << (address_pending ? " addr=" : " data=") << value
                          << " cmd-data=0x" << command_data << std::dec << '\n';
            if (command_pending) {
                command_pending = false;
                dsp.set_irq(false);
                sync_events.push_back({SyncEvent::Kind::Ready});
            }
            return value;
        };
        bus.pio_write = [this](std::uint16_t selector, std::uint16_t value) {
            ++pio_write_count;
            if (std::getenv("MAMERECOMP_QSOUND_TRACE") && pio_write_count <= 512)
                std::cerr << "QSound DSP PIO write #" << pio_write_count
                          << " PC=0x" << std::hex << dsp.pc()
                          << " PSEL=" << selector << " value=0x" << value << std::dec << '\n';
            // PDX0 selects the sample byte address and ROM bank half. PDX1 is
            // reserved for the DSP's alternate (ADPCM) sample interface.
            rom_bank = (rom_bank & 0x7fffU) | std::uint16_t((selector & 1U) << 15);
            rom_offset = value;
        };
        bus.serial_clock_out = [this](bool level) { on_ock(level); };
        bus.synchronize = [this] { drain_sync_events(); };
        if (std::getenv("MAMERECOMP_QSOUND_DSP_TRACE")) {
            bus.instruction_trace = [this](dsp16::Dsp16ATracePhase phase,
                                           std::uint16_t pc,
                                           std::uint16_t opcode,
                                           const dsp16::Dsp16AState& state) {
                if (!dsp_trace.is_open()) return;
                switch (pc) {
                case 0x0599: case 0x059a: case 0x059b: case 0x059c:
                case 0x05d2: case 0x05d3: case 0x05d4: case 0x05d7:
                    break;
                default: return;
                }
                dsp_trace << (phase == dsp16::Dsp16ATracePhase::Before ? 'B' : 'A')
                          << ',' << dsp.cycles() << ',' << std::hex << pc << ',' << opcode
                          << ',' << unsigned(state.auc) << ',' << state.psw << ',' << state.pt
                          << ',' << state.r[0] << ',' << state.r[1] << ',' << state.r[2]
                          << ',' << state.r[3] << ',' << static_cast<std::uint16_t>(state.x)
                          << ',' << static_cast<std::uint32_t>(state.y)
                          << ',' << static_cast<std::uint32_t>(state.p)
                          << ',' << static_cast<std::uint64_t>(state.a[0])
                          << ',' << static_cast<std::uint64_t>(state.a[1]) << std::dec << '\n';
            };
        }
        return bus;
    }

    void drain_sync_events() {
        while (!sync_events.empty()) {
            const SyncEvent event = sync_events.front();
            sync_events.pop_front();
            if (event.kind == SyncEvent::Kind::Command) {
                command_address = event.address;
                command_data = event.data;
                command_pending = true;
                dsp_ready = false;
                dsp.set_irq(true);
            } else {
                dsp_ready = true;
            }
        }
    }

    void on_ock(bool level) {
        if (level == ock || !level) {
            ock = level;
            return;
        }
        ock = level;
        const bool old = dsp.output_load();
        if (ready_edge && shift_remaining == 0 && !dsp.output_enable()) {
            channel = dsp.selected_output_channel() ? 1 : 0;
            shift_remaining = 16;
            shift_register = 0;
        }
        if (shift_remaining) {
            shift_register = std::uint16_t((shift_register << 1) | (dsp.data_output() ? 1 : 0));
            if (--shift_remaining == 0) {
                ++serial_word_count;
                const auto magnitude = static_cast<std::uint16_t>(shift_register & 0x8000U
                    ? 0U - shift_register : shift_register);
                if (shift_register) ++nonzero_serial_word_count;
                serial_word_peak_value = std::max(serial_word_peak_value, magnitude);
                if (channel == 0) ++serial_left_word_count;
                else ++serial_right_word_count;
                // MAME updates the sound stream on the left-channel word
                // before replacing that sample, preserving the stereo pair
                // assembled during the preceding serial frame.
                if (channel == 0) {
                    pcm_frames.push_back({samples[0], samples[1]});
                    ++pcm_frame_count;
                }
                samples[channel] = static_cast<std::int16_t>(shift_register);
            }
        }
        ready_edge = old_load && !old;
        old_load = old;
    }

    void set_rom(std::span<const std::uint8_t> bytes) {
        sample_rom.assign(bytes.begin(), bytes.end());
        if (sample_rom.empty()) {
            mame_hle.samples = {};
            return;
        }
        // MAME's ROM interface masks to the configured region width. Pad to a
        // power of two so HLE's mask and LLE's bus use that same behavior.
        std::size_t size = 1;
        while (size < sample_rom.size()) size <<= 1;
        sample_rom.resize(size, 0xff);
        mame_hle.samples = sample_rom;
    }

    void advance_hle(std::uint64_t clocks) {
        if (!use_mame_hle || config.mode != QSoundMode::Hle) return;
        hle_clock_remainder += clocks;
        constexpr std::uint64_t clocks_per_sample = 2 * 1248;
        while (hle_clock_remainder >= clocks_per_sample) {
            hle_clock_remainder -= clocks_per_sample;
            mame_hle.update();
            if (pcm_frames.size() >= 16384) pcm_frames.pop_front();
            pcm_frames.push_back({mame_hle.out[0], mame_hle.out[1]});
            ++pcm_frame_count;
        }
    }

    QSoundConfig config;
    MameHle mame_hle{};
    bool use_mame_hle = false;
    std::uint64_t hle_clock_remainder = 0;
    std::vector<std::uint8_t> sample_rom;
    std::uint16_t rom_bank = 0, rom_offset = 0;
    std::uint16_t command_address = 0, command_data = 0, new_data = 0;
    bool command_pending = false, dsp_ready = true;
    std::deque<SyncEvent> sync_events;
    std::ofstream dsp_trace;
    Dsp16A dsp;
    std::int16_t samples[2]{};
    std::uint16_t shift_register = 0;
    std::uint8_t shift_remaining = 0, channel = 0;
    bool ock = true, old_load = true, ready_edge = false;
    std::deque<std::array<std::int16_t, 2>> pcm_frames;
    std::array<std::int16_t, 2> hle_frame{};
    std::uint64_t sample_read_count=0, pio_read_count=0, pio_write_count=0;
    std::uint64_t serial_word_count=0, serial_left_word_count=0;
    std::uint64_t serial_right_word_count=0, pcm_frame_count=0;
    std::uint64_t nonzero_sample_read_count=0, nonzero_serial_word_count=0;
    std::uint16_t serial_word_peak_value=0;
};

QSound::QSound(QSoundConfig config) : impl_(std::make_unique<Impl>(config)) {}
QSound::~QSound() = default;
QSound::QSound(QSound&&) noexcept = default;
QSound& QSound::operator=(QSound&&) noexcept = default;

void QSound::set_sample_rom(std::span<const std::uint8_t> bytes) {
    impl_->set_rom(bytes);
}

void QSound::set_firmware(std::span<const std::uint16_t> words) {
    impl_->mame_hle.load_firmware(words);
    impl_->use_mame_hle = !words.empty();
    if (impl_->config.mode == QSoundMode::Lle && !words.empty())
        impl_->dsp.set_firmware(words.first(std::min(words.size(), Dsp16A::internal_program_words)));
}

void QSound::reset() {
    if (impl_->config.mode == QSoundMode::Hle) {
        impl_->mame_hle.reset();
        impl_->hle_clock_remainder = 0;
        impl_->pcm_frames.clear();
    } else {
        impl_->rom_bank = 0;
        impl_->rom_offset = 0;
        impl_->command_pending = false;
        impl_->dsp_ready = true;
        impl_->sync_events.clear();
        impl_->samples[0] = impl_->samples[1] = 0;
        impl_->shift_register = 0;
        impl_->shift_remaining = 0;
        impl_->ock = impl_->old_load = true;
        impl_->ready_edge = false;
        impl_->pcm_frames.clear();
        impl_->dsp.reset();
    }
}

void QSound::write(std::uint8_t offset, std::uint8_t data) {
    if (impl_->config.mode == QSoundMode::Hle) {
        switch(offset) {
        case 0: impl_->new_data=std::uint16_t((impl_->new_data&0x00ffU)|(std::uint16_t(data)<<8)); break;
        case 1: impl_->new_data=std::uint16_t((impl_->new_data&0xff00U)|data); break;
        case 2: impl_->mame_hle.write(data,impl_->new_data); break;
        default: break;
        }
        return;
    }
    switch (offset) {
    case 0: impl_->new_data = std::uint16_t((impl_->new_data & 0x00ffU) | (std::uint16_t(data) << 8)); break;
    case 1: impl_->new_data = std::uint16_t((impl_->new_data & 0xff00U) | data); break;
    case 2:
        impl_->command_data = impl_->new_data;
        impl_->sync_events.push_back({Impl::SyncEvent::Kind::Command, data, impl_->command_data});
        impl_->dsp_ready = false;
        break;
    default: break;
    }
}

std::uint8_t QSound::read_status() {
    if (impl_->config.mode == QSoundMode::Hle)
        return std::uint8_t(impl_->mame_hle.ready);
    return impl_->dsp_ready ? 0x80 : 0;
}

void QSound::run_clocks(std::uint64_t clocks) {
    if (impl_->config.mode == QSoundMode::Lle) {
        impl_->drain_sync_events();
        impl_->dsp.run_clocks(clocks);
    } else
        impl_->advance_hle(clocks);
}

void QSound::render(std::span<std::int16_t> stereo) {
    const std::size_t frames = stereo.size() / 2;
    for (std::size_t i = 0; i < frames; ++i) {
        if (!impl_->pcm_frames.empty()) {
            const auto frame = impl_->pcm_frames.front();
            impl_->pcm_frames.pop_front();
            stereo[i * 2] = frame[0];
            stereo[i * 2 + 1] = frame[1];
        } else {
            // MAME's sound_stream keeps the last DAC levels between serial
            // word updates. The LLE may produce fewer edge snapshots than
            // the host requests at the nominal sample rate, so hold the live
            // left/right DAC values rather than inserting artificial silence.
            stereo[i * 2] = impl_->samples[0];
            stereo[i * 2 + 1] = impl_->samples[1];
        }
    }
    if (stereo.size() & 1U) stereo.back() = 0;
}

std::uint32_t QSound::sample_rate() const {
    return impl_->config.clock_hz / 2 / 1248;
}

QSoundMode QSound::mode() const { return impl_->config.mode; }
std::uint64_t QSound::dsp_cycles() const { return impl_->dsp.cycles(); }
std::uint64_t QSound::dsp_instructions() const { return impl_->dsp.instructions(); }
std::uint16_t QSound::dsp_pc() const { return impl_->dsp.pc(); }
std::uint64_t QSound::sample_reads() const { return impl_->sample_read_count; }
std::uint64_t QSound::pio_reads() const { return impl_->pio_read_count; }
std::uint64_t QSound::pio_writes() const { return impl_->pio_write_count; }
std::uint64_t QSound::serial_words() const { return impl_->serial_word_count; }
std::uint64_t QSound::serial_left_words() const { return impl_->serial_left_word_count; }
std::uint64_t QSound::serial_right_words() const { return impl_->serial_right_word_count; }
std::uint64_t QSound::pcm_frames_produced() const { return impl_->pcm_frame_count; }
std::uint64_t QSound::nonzero_sample_reads() const { return impl_->nonzero_sample_read_count; }
std::uint64_t QSound::nonzero_serial_words() const { return impl_->nonzero_serial_word_count; }
std::uint16_t QSound::serial_word_peak() const { return impl_->serial_word_peak_value; }
std::uint64_t QSound::dsp_ock_rising_edges() const { return impl_->dsp.ock_rising_edges(); }
std::uint64_t QSound::dsp_do_transitions() const { return impl_->dsp.do_transitions(); }
std::uint64_t QSound::dsp_ose_transitions() const { return impl_->dsp.ose_transitions(); }
std::uint64_t QSound::dsp_old_transitions() const { return impl_->dsp.old_transitions(); }
std::uint64_t QSound::dsp_serial_word_loads() const { return impl_->dsp.serial_word_loads(); }
std::uint64_t QSound::dsp_sdx_writes() const { return impl_->dsp.sdx_writes(); }
std::uint64_t QSound::dsp_nonzero_sdx_writes() const { return impl_->dsp.nonzero_sdx_writes(); }
std::uint64_t QSound::dsp_nonzero_serial_loads() const { return impl_->dsp.nonzero_serial_word_loads(); }
std::uint16_t QSound::dsp_last_sdx_value() const { return impl_->dsp.last_sdx_value(); }
std::uint16_t QSound::dsp_last_serial_word_loaded() const { return impl_->dsp.last_serial_word_loaded(); }
std::uint16_t QSound::dsp_last_sdx_pc() const { return impl_->dsp.last_sdx_pc(); }
std::uint16_t QSound::dsp_last_sdx_opcode() const { return impl_->dsp.last_sdx_opcode(); }
std::int64_t QSound::dsp_last_sdx_a0() const { return impl_->dsp.last_sdx_a0(); }
std::int64_t QSound::dsp_last_sdx_a1() const { return impl_->dsp.last_sdx_a1(); }
std::int16_t QSound::dsp_last_sdx_x() const { return impl_->dsp.last_sdx_x(); }
std::int32_t QSound::dsp_last_sdx_y() const { return impl_->dsp.last_sdx_y(); }
std::int32_t QSound::dsp_last_sdx_p() const { return impl_->dsp.last_sdx_p(); }

} // namespace mamerecomp::chips
