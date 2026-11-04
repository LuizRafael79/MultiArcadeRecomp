#include "sound.h"
#include "adsp.h"
#include "adsprec.h"
#include "ki/games/game.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <vector>

extern "C" {
extern const struct rec_dsp_image_s rec_dsp_images_[];
extern const uint32_t rec_dsp_nimages_;
}

namespace {
constexpr std::size_t kRomWords = 0x800000;
constexpr std::size_t kRingSize = 65536;
struct Dcs {
    std::vector<std::uint16_t> rom = std::vector<std::uint16_t>(kRomWords, 0xffff);
    std::array<std::uint32_t, 0x800> ext_pm{};
    std::array<std::int16_t, kRingSize> ring{};
    std::array<std::uint16_t, 16384> queued{};
    std::uint64_t wr{}, rd{}, cycles{}, commands{}, reads{}, generated{}, nonzero{}, consumed{};
    std::size_t queued_count{};
    std::uint16_t bank{}, input{}, last_command{}, peak{};
    std::int16_t last_sample{};
    double rate{31250.0};
    bool input_full{}, held{}, interpreter_only{};

    static Dcs& self();
    static std::uint32_t pm_read(std::uint16_t a) {
        auto& d=self(); return a>=0x800&&a<0x2000?d.ext_pm[a&0x7ff]:0;
    }
    static void pm_write(std::uint16_t a, std::uint32_t v) {
        auto& d=self(); if(a>=0x800&&a<0x2000){d.ext_pm[a&0x7ff]=v&0xffffff;adsp_pm_changed(a);}
    }
    static std::uint16_t dm_read(std::uint16_t a) {
        auto& d=self();
        if(a<0x2000)return static_cast<std::uint16_t>(d.ext_pm[a&0x7ff]>>8);
        if(a<0x3000)return d.rom[(std::size_t(d.bank)*0x1000+a-0x2000)%kRomWords];
        if(a>=0x3400&&a<0x3800){++d.reads;d.input_full=false;adsp_set_irq(ADSP_IRQ2,false);return d.input;}
        return 0xffff;
    }
    static void dm_write(std::uint16_t a, std::uint16_t v) {
        auto& d=self();
        if(a<0x2000){auto& w=d.ext_pm[a&0x7ff];w=(std::uint32_t(v)<<8)|(w&0xff);adsp_pm_changed(a);}
        else if(a>=0x3000&&a<0x3400)d.bank=v&0x7ff;
    }
    static void sample_out(std::int16_t s) {
        auto& d=self();d.ring[d.wr++%kRingSize]=s;if(d.wr-d.rd>kRingSize)d.rd=d.wr-kRingSize;
        ++d.generated;d.nonzero+=s!=0;auto m=std::uint16_t(s<0?-std::int32_t(s):s);d.peak=std::max(d.peak,m);d.last_sample=s;
    }
    static void sample_rate(double hz){self().rate=hz;}
    static void reboot(){auto&d=self();adsp_reset();d.boot();}
    bool load(const char *dir) {
        for(unsigned n=0;n<8;n++){
            if(!game.sound_roms[n])return false;
            auto path=std::filesystem::path(dir)/game.sound_roms[n];std::ifstream f(path,std::ios::binary);
            if(!f){std::fprintf(stderr,"DCS ROM missing: %s\n",path.c_str());return false;}
            std::vector<std::uint8_t>b(0x80000);f.read(reinterpret_cast<char*>(b.data()),b.size());
            if(f.gcount()!=static_cast<std::streamsize>(b.size()))return false;
            for(std::size_t i=0;i<b.size();i++)rom[n*0x100000+i]=0xff00|b[i];
        }return true;
    }
    void boot(){auto base=(std::size_t(bank)*0x1000)%kRomWords;auto*pm=adsp_internal_pm();unsigned n=std::min<unsigned>(((rom[base+3]&0xff)+1)*8,0x400);for(unsigned i=0;i<n;i++){auto a=base+i*4;pm[i]=((rom[a]&255)<<16)|((rom[a+1]&255)<<8)|(rom[a+2]&255);}adsp_code_reload();}
    void board_reset(){bank=0;input_full=false;adsp_reset();adsp_set_irq(ADSP_IRQ0,false);adsp_set_irq(ADSP_IRQ1,false);adsp_set_irq(ADSP_IRQ2,false);boot();}
    void catch_up(){for(std::size_t i=0;i<queued_count;i++)if(!held){adsp_run(queued[i]);cycles+=queued[i];}queued_count=0;}
    void run(int n){if(held||n<=0)return;if(n>0xffff||queued_count==queued.size())catch_up();if(n>0xffff){adsp_run(n);cycles+=n;}else queued[queued_count++]=static_cast<std::uint16_t>(n);}
};
Dcs d;
Dcs& Dcs::self(){return d;}
const adsp_hooks_t hooks{Dcs::pm_read,Dcs::pm_write,Dcs::dm_read,Dcs::dm_write,Dcs::sample_out,Dcs::sample_rate,Dcs::reboot};
}

extern "C" bool sound_init(const char*dir){if(!dir||!d.load(dir))return false;sound_power_on();return true;}
extern "C" void sound_power_on(){d.held=false;d.bank=0;d.input_full=false;d.ext_pm.fill(0);d.wr=d.rd=d.cycles=d.commands=d.reads=d.generated=d.nonzero=d.consumed=0;d.queued_count=0;d.peak=0;d.last_sample=0;d.last_command=0;d.rate=31250;adsp_init(&hooks);adsp_use_code(d.interpreter_only?nullptr:rec_dsp_images_,d.interpreter_only?0:rec_dsp_nimages_);d.board_reset();}
extern "C" void sound_reset_w(bool released){d.catch_up();if(!released){d.held=true;d.board_reset();}else d.held=false;}
extern "C" void sound_data_w(std::uint16_t v){d.catch_up();d.input=v;d.last_command=v;d.input_full=true;++d.commands;adsp_set_irq(ADSP_IRQ2,true);}
extern "C" bool sound_input_empty(){d.catch_up();return !d.input_full;}
extern "C" void sound_run(int n){d.run(n);}
extern "C" void sound_set_interpreter_only(bool v){d.interpreter_only=v;adsp_use_code(v?nullptr:rec_dsp_images_,v?0:rec_dsp_nimages_);}
extern "C" int sound_fetch(std::int16_t*out,int max){if(!out||max<=0)return 0;d.catch_up();int n=int(std::min<std::uint64_t>(max,d.wr-d.rd));for(int i=0;i<n;i++)out[i]=d.ring[d.rd++%kRingSize];d.consumed+=n;return n;}
extern "C" void sound_report(){std::uint64_t nat{},interp{};adsp_counters(&nat,&interp);std::fprintf(stderr,"ADSP-2105 instructions: native/recompiled=%llu interpreted=%llu total=%llu\n%s DCS commands=%llu cycles=%llu pc=0x%04x SPORT1 rate=%.1fHz generated=%llu nonzero=%llu peak=%u last=%d pending=%llu host-reads=%llu last-command=0x%04x input-full=%u queued=%zu\n",(unsigned long long)nat,(unsigned long long)interp,(unsigned long long)(nat+interp),game.id?game.id:"KI",(unsigned long long)d.commands,(unsigned long long)d.cycles,adsp_pc(),d.rate,(unsigned long long)d.generated,(unsigned long long)d.nonzero,d.peak,d.last_sample,(unsigned long long)(d.wr-d.rd),(unsigned long long)d.reads,d.last_command,d.input_full,d.queued_count);}
extern "C" void sound_report_file(const char*path){if(!path||!*path)return;FILE*f=std::fopen(path,"a");if(!f)return;std::uint64_t nat{},interp{};adsp_counters(&nat,&interp);std::fprintf(f,"ADSP-2105 instructions: native/recompiled=%llu interpreted=%llu total=%llu\n%s DCS commands=%llu cycles=%llu pc=0x%04x SPORT1 rate=%.1fHz generated=%llu nonzero=%llu peak=%u last=%d pending=%llu consumed=%llu host-reads=%llu last-command=0x%04x input-full=%u queued=%zu\n",(unsigned long long)nat,(unsigned long long)interp,(unsigned long long)(nat+interp),game.id?game.id:"KI",(unsigned long long)d.commands,(unsigned long long)d.cycles,adsp_pc(),d.rate,(unsigned long long)d.generated,(unsigned long long)d.nonzero,d.peak,d.last_sample,(unsigned long long)(d.wr-d.rd),(unsigned long long)d.consumed,(unsigned long long)d.reads,d.last_command,d.input_full,d.queued_count);std::fclose(f);}
