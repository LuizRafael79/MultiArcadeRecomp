#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <span>

namespace multiarcaderecomp::naomi {

// HLE for Sega 837-13551 JVS I/O board and the Naomi 315-6149 JVS/Maple bridge.
// Board identity is verified directly from the user's 315-6215 firmware ROM.
// Protocol behavior follows the matching Flycast 2.6 Naomi JVS implementation.
class NaomiJvs13551 {
public:
    static constexpr const char* kBoardId =
        "SEGA ENTERPRISES,LTD.;I/O BD JVS;837-13551 ;Ver1.00;98/10";

    std::array<uint8_t, 0x80> eeprom{};
    std::array<std::vector<uint8_t>, 32> rx{};
    std::array<std::vector<uint8_t>, 32> repeat{};
    std::array<uint8_t, 0x10000> firmware{};
    bool firmware_loaded = false;
    bool crazy_mode = false;
    bool init_in_progress = false;
    uint8_t nodeId = 1;
    uint16_t steering = 0x8000;
    uint16_t accel = 0x0000;
    uint16_t brake = 0x0000;
    uint16_t p1Buttons = 0;
    uint16_t p2Buttons = 0;
    uint8_t systemBits = 0;
    uint16_t coinCount[2]{};
    uint8_t digitalOutputs = 0;

    // Flycast MIEImpl initializes this NVRAM from a saved/default image and
    // falls back to zero-filled storage when the set has no EEPROM dump.
    // MvC2 USA has no set-specific EEPROM image.
    NaomiJvs13551() { eeprom.fill(0); firmware.fill(0); }

    // Flycast configures the Naomi system EEPROM from the M2 ROM BootID
    // before the BIOS starts (naomi_cart_ConfigureEEPROM). The ROM-board
    // X76F100 is separate; this 128-byte image is the Maple-visible system
    // EEPROM which the BIOS checks to decide whether game settings are ready.
    bool configureSystemEeprom(std::span<const uint8_t> rom) {
        constexpr size_t boot_id_size = 0x500;
        constexpr size_t game_title_offset = 0x30;
        constexpr size_t game_id_offset = 0x134;
        constexpr size_t coin_flag_offset = 0x1e0;
        constexpr size_t game_pc_tail_offset = 0x420;
        size_t boot = 0;
        auto has_title = [&](size_t offset) {
            return rom.size() >= offset + boot_id_size &&
                   rom[offset + game_title_offset] != 0 &&
                   !(rom[offset + game_title_offset] == 0xff &&
                     rom[offset + game_title_offset + 1] == 0xff);
        };
        if (!has_title(boot)) {
            boot = 0x800000;
            if (!has_title(boot)) return false;
        }

        const auto* id = rom.data() + boot;
        if (std::memcmp(id, "NAOMI", 5) != 0 && std::memcmp(id, "Naomi2", 6) != 0)
            return false;

        const auto game_id = id + game_id_offset;
        const bool changed = std::memcmp(game_id, eeprom.data() + 3, 4) != 0;
        auto write_setting = [&](size_t offset, uint8_t value) {
            eeprom[offset] = value;
            eeprom[offset + 18] = value;
            uint32_t crc = 0xdebdeb00u;
            for (size_t i = 2; i < 18; ++i) {
                crc = (crc & 0xffffff00u) + eeprom[i];
                for (unsigned bit = 0; bit < 8; ++bit)
                    crc = (crc & 0x80000000u) ? (crc << 1) + 0x10210000u : crc << 1;
            }
            for (unsigned bit = 0; bit < 8; ++bit)
                crc = (crc & 0x80000000u) ? (crc << 1) + 0x10210000u : crc << 1;
            eeprom[0] = eeprom[18] = static_cast<uint8_t>(crc);
            eeprom[1] = eeprom[19] = static_cast<uint8_t>(crc >> 8);
        };
        const uint8_t cabinet = id[game_pc_tail_offset + 9];
        const uint8_t vertical = id[game_pc_tail_offset + 11];
        if (changed) {
            for (size_t i = 0; i < 4; ++i) write_setting(3 + i, game_id[i]);
            write_setting(7, 9);
            write_setting(8, (cabinet & 8) ? 0x30 : (cabinet & 4) ? 0x20 :
                              (cabinet & 2) ? 0x10 : 0);
            const uint8_t* coin = id + coin_flag_offset;
            if (coin[0] == 1) {
                write_setting(2, (coin[1] & 1) | 0x10);
                if (coin[2] == 1) write_setting(8, eeprom[8] | 1);
                write_setting(9, static_cast<uint8_t>(coin[3] - 1));
                write_setting(10, std::max<uint8_t>(coin[6], 1));
                write_setting(11, std::max<uint8_t>(coin[4], 1));
                write_setting(12, std::max<uint8_t>(coin[5], 1));
                write_setting(13, coin[7]);
                for (size_t i = 0; i < 4; ++i)
                    write_setting(14 + i, coin[8 + i * 2] | (coin[9 + i * 2] << 4));
            } else {
                write_setting(2, (vertical & 2) ? 0x11 : 0x10);
                write_setting(9, 0);
                write_setting(10, 1); write_setting(11, 1); write_setting(12, 1);
                write_setting(13, 0);
                write_setting(14, 0x11); write_setting(15, 0x11);
                write_setting(16, 0x11); write_setting(17, 0x11);
            }
        }
        if (vertical == 2) write_setting(2, eeprom[2] | 1);
        else if (vertical == 1) write_setting(2, eeprom[2] & ~1u);
        if (cabinet != 0 && cabinet < 0x10) {
            const unsigned players = eeprom[8] >> 4;
            if (!(cabinet & (1u << players))) {
                const uint8_t chute = eeprom[8] & 1;
                const uint8_t count = cabinet & 8 ? 0x30 : cabinet & 4 ? 0x20 :
                                      cabinet & 2 ? 0x10 : 0;
                write_setting(8, count | chute);
            }
        }
        return true;
    }

    static uint32_t firmwareHash(const uint8_t* data, size_t length) {
        constexpr uint32_t p1=0x9E3779B1u, p2=0x85EBCA77u, p3=0xC2B2AE3Du,
                           p4=0x27D4EB2Fu, p5=0x165667B1u;
        auto rot=[](uint32_t x,unsigned n){ return (x<<n)|(x>>(32-n)); };
        auto read32=[](const uint8_t* p){ return uint32_t(p[0])|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24); };
        const uint8_t* p=data; const uint8_t* const end=data+length; uint32_t h;
        if (length>=16) {
            uint32_t v1=p1+p2, v2=p2, v3=0, v4=0u-p1; const uint8_t* limit=end-16;
            do { v1=rot(v1+read32(p)*p2,13)*p1; p+=4; v2=rot(v2+read32(p)*p2,13)*p1; p+=4; v3=rot(v3+read32(p)*p2,13)*p1; p+=4; v4=rot(v4+read32(p)*p2,13)*p1; p+=4; } while(p<=limit);
            h=rot(v1,1)+rot(v2,7)+rot(v3,12)+rot(v4,18);
        } else h=p5;
        h+=static_cast<uint32_t>(length);
        while(p+4<=end) { h=rot(h+read32(p)*p3,17)*p4; p+=4; }
        while(p<end) { h=rot(h+uint32_t(*p++)*p5,11)*p1; }
        h^=h>>15; h*=p2; h^=h>>13; h*=p3; h^=h>>16; return h;
    }

    static void append(std::vector<uint8_t>& o, uint8_t v) { o.push_back(v); }
    static void append32le(std::vector<uint8_t>& o, uint32_t v) {
        append(o, v); append(o, v >> 8); append(o, v >> 16); append(o, v >> 24);
    }

    std::vector<uint8_t> jvsMessage(const uint8_t* in, size_t n) {
        if (!in || n == 0) return {};
        const uint8_t c = in[0];
        if (c == 0xF0) return {}; // reset: no reply
        if (c == 0xF1 && (n < 2 || in[1] != nodeId)) return {};

        std::vector<uint8_t> o{0xE0, 0x00, 0x00};
        auto status = [&]{ append(o, 1); };
        switch (c) {
        case 0xF1:
            status(); status(); append(o, 5); break;
        case 0x10:
            status(); status();
            for (const char* p = kBoardId; *p; ++p) append(o, (uint8_t)*p);
            append(o, 0); break;
        case 0x11: status(); status(); append(o, 0x11); break; // command format 1.1
        case 0x12: status(); status(); append(o, 0x20); break; // JVS 2.0
        case 0x13: status(); status(); append(o, 0x10); break; // comm 1.0
        case 0x14: // features: exact 837-13551 profile
            status(); status();
            append(o,1); append(o,2); append(o,13); append(o,0); // digital: 2p, 13 bits
            append(o,2); append(o,2); append(o,0);  append(o,0); // 2 coin slots
            append(o,3); append(o,8); append(o,0x10); append(o,0); // 8x 16-bit analog
            append(o,0x12); append(o,6); append(o,0); append(o,0); // 6 outputs
            append(o,0); break;
        case 0x15: // master board ID (837-13551 reports an empty ID)
            status(); status(); break;
        case 0x70: // Sega's board initialization handshake
            // Flycast's board sets this while the game probes the I/O board.
            // The command is four bytes after the opcode; malformed packets
            // still receive the documented non-ready response safely.
            init_in_progress = true;
            status(); status();
            if (n >= 5 && in[2] == 3) {
                append(o, 0x10);
                for (unsigned i = 0; i < 16; ++i) append(o, 0x7f);
                if (in[4] == 0x10 || in[4] == 0x11)
                    init_in_progress = false;
            } else {
                append(o, 2); append(o, 3); append(o, 1);
            }
            break;
        default:
            if (c >= 0x20 && c <= 0x38) {
                status();
                size_t i = 0;
                while (i < n) {
                    uint8_t q = in[i];
                    if (q == 0x20 && i + 2 < n) {
                        status();
                        append(o, systemBits);
                        unsigned players = in[i+1], bytes = in[i+2];
                        for (unsigned p=0; p<players; ++p) {
                            uint16_t b = p == 0 ? p1Buttons : p2Buttons;
                            append(o, (uint8_t)(b >> 8));
                            if (bytes == 2) append(o, (uint8_t)b);
                        }
                        i += 3;
                    } else if (q == 0x21 && i + 1 < n) {
                        status(); unsigned slots=in[i+1];
                        for (unsigned s=0;s<slots;++s) {
                            uint16_t cc = s < 2 ? coinCount[s] : 0;
                            append(o,(cc>>8)&0x3F); append(o,cc);
                        }
                        i += 2;
                    } else if (q == 0x22 && i + 1 < n) {
                        status(); unsigned chans=in[i+1];
                        for (unsigned a=0;a<chans;++a) {
                            uint16_t v = a==0?steering:(a==1?accel:(a==2?brake:0x8000));
                            v = std::min<uint16_t>(0xff7f, v);
                            if (v & 0x80) v = (uint16_t)(v + 0x100);
                            append(o,v>>8); append(o,v);
                        }
                        i += 2;
                    } else if (q == 0x30 && i + 3 < n) {
                        unsigned slot=in[i+1]; uint16_t dec=(in[i+2]<<8)|in[i+3];
                        if (slot>=1 && slot<=2) coinCount[slot-1] = coinCount[slot-1] > dec ? coinCount[slot-1]-dec : 0;
                        status(); i += 4;
                    } else if (q == 0x32 && i + 1 < n) {
                        unsigned cnt=in[i+1];
                        if (cnt && i+2<n) digitalOutputs=in[i+2];
                        status(); i += std::min<size_t>(n-i, cnt+2);
                    } else if (q == 0x33 && i + 1 < n) {
                        // JVS analog output; the 837-13551 accepts it but
                        // exposes no modeled analog-output hardware.
                        const auto count = static_cast<size_t>(in[i + 1]);
                        status();
                        i += std::min(n - i, count + 2u);
                    } else {
                        append(o,2); break;
                    }
                }
            } else {
                append(o,2);
            }
            break;
        }
        o[2] = (uint8_t)(o.size() - 2);
        uint8_t crc=0; for (size_t i=1;i<o.size();++i) crc=(uint8_t)(crc+o[i]);
        append(o,crc);
        return o;
    }

    uint8_t senseLine(uint8_t node) const { return node == 1 ? 0x8E : 0x8F; }

    void queueJvs(uint8_t node, uint8_t channel, const uint8_t* cmd, size_t len, bool useRepeat, bool repeatFirst=false) {
        if (channel >= rx.size()) return;
        if (node == 0xFF) {
            // JVS broadcast: fan out to every attached board. This model has one 837-13551 at node 1.
            queueJvs(1, channel, cmd, len, useRepeat, repeatFirst);
            return;
        }
        if (node < 1 || node > 32) return;
        std::vector<uint8_t> req;
        if (useRepeat && !repeat[node-1].empty() && repeatFirst) req.insert(req.end(), repeat[node-1].begin(), repeat[node-1].end());
        if (cmd && len) req.insert(req.end(), cmd, cmd+len);
        if (useRepeat && !repeat[node-1].empty() && !repeatFirst) req.insert(req.end(), repeat[node-1].begin(), repeat[node-1].end());
        auto body=jvsMessage(req.data(),req.size());
        if (!body.empty()) {
            auto& r=rx[channel];
            // Flycast appends each board reply to the channel buffer. The
            // Maple bridge's 0x27/0x35 commands can carry multiple JVS
            // messages in one transaction.
            // The CT/HOTD bridge firmware uses a different receive record:
            // it omits the node and status bytes and keeps only length+data.
            // Flycast MIEImpl::send_jvs_message() selects this layout after
            // firmwareLoaded() identifies one of those firmware hashes.
            if (crazy_mode) {
                r.push_back(static_cast<uint8_t>(body.size()));
            } else {
                r.push_back(node); r.push_back(0);
                r.push_back(static_cast<uint8_t>(body.size()));
            }
            r.insert(r.end(),body.begin(),body.end());
        }
    }

    std::vector<uint8_t> mapleCommand(uint8_t cmd, const uint8_t* p, size_t n) {
        std::vector<uint8_t> o;
        auto hdr=[&](uint8_t rc,uint8_t words){ append(o,rc); append(o,0); append(o,0x20); append(o,words); };
        switch(cmd) {
        case 0x01: hdr(0x05,0); break; // DeviceRequest
        case 0x02: hdr(0x06,0); break;
        case 0x03: case 0x04: hdr(0x07,0); break;
        case 0x80: { // 315-6149 bridge firmware upload
            if (!p || n < 4) { hdr(0xFD,0); break; }
            if (p[1] == 0xFF) {
                const uint32_t hash=firmwareHash(firmware.data(),firmware.size());
                firmware_loaded=true;
                crazy_mode=hash==0xa7c50459u || hash==0xae841e36u || hash==0xa6784e26u;
                for (auto& request:repeat) request.clear();
                hdr(0x07,0); break;
            }
            const size_t transfer=p[0]==0xff ? 0x1cu : 0x18u;
            const size_t address=(size_t(p[2])<<8)|p[3];
            if (n < 4+transfer || address+transfer > firmware.size()) { hdr(0xFD,0); break; }
            std::memcpy(firmware.data()+address,p+4,transfer);
            uint8_t sum=0; for (size_t i=0;i<0x1c;++i) sum=static_cast<uint8_t>(sum+p[i]);
            hdr(0x80,1); append(o,sum); append(o,0); append(o,0); append(o,0);
            hdr(0x07,0);
            break;
        }
        case 0x84: // 315-6149 self test
            hdr(0x85,1); append(o,0); append(o,0); append(o,0); append(o,0); break;
        case 0x82: { // 315-6149 bridge ID, exactly as Flycast/Naomi bridge reports
            static const char a[]="315-6149    COPYRIGHT SEGA E";
            static const char b[]="NTERPRISES CO,LTD.  1998    ";
            hdr(0x83,7); o.insert(o.end(),a,a+28);
            // Flycast BaseMIE::RawDma emits only 20 bytes in the second
            // reply (5 words); the source ID string's remaining bytes are
            // padding and do not belong to this packet.
            hdr(0x83,5); o.insert(o.end(),b,b+20);
            break;
        }
        case 0x86:
            if (!p || !n) { hdr(0x87,0); break; }
            handle86(p,n,o); break;
        default: hdr(0xFD,0); break;
        }
        return o;
    }

    std::vector<uint32_t> mapleDmaEndpoint(unsigned bus,
                                            std::span<const uint32_t> request) {
        if (bus != 0 || request.empty()) return {0xffffffffu};
        const uint32_t frame = request[0];
        const auto command = static_cast<uint8_t>(frame & 0xffu);
        const std::size_t payload_words = (frame >> 24) & 0xffu;
        if (payload_words + 1u > request.size())
            return {0x002000fdu}; // protocol error response, padded to one word

        std::vector<uint8_t> payload;
        payload.reserve(payload_words * 4u);
        for (std::size_t i = 0; i < payload_words; ++i) {
            const uint32_t word = request[i + 1];
            payload.push_back(static_cast<uint8_t>(word));
            payload.push_back(static_cast<uint8_t>(word >> 8));
            payload.push_back(static_cast<uint8_t>(word >> 16));
            payload.push_back(static_cast<uint8_t>(word >> 24));
        }
        auto response = mapleCommand(command, payload.data(), payload.size());
        while ((response.size() & 3u) != 0u) response.push_back(0);
        std::vector<uint32_t> words;
        words.reserve(response.size() / 4u);
        for (std::size_t i = 0; i < response.size(); i += 4u) {
            words.push_back(static_cast<uint32_t>(response[i]) |
                (static_cast<uint32_t>(response[i + 1]) << 8) |
                (static_cast<uint32_t>(response[i + 2]) << 16) |
                (static_cast<uint32_t>(response[i + 3]) << 24));
        }
        return words;
    }

private:
    void handle86(const uint8_t* p,size_t n,std::vector<uint8_t>& o) {
        uint8_t sub=p[0];
        // MIEImpl::handle_86_subcommand swaps these two commands for the
        // firmware families identified by firmwareLoaded().
        if (crazy_mode) {
            if (sub == 0x13) sub = 0x17;
            else if (sub == 0x17) sub = 0x13;
        }
        auto hdr=[&](uint8_t rc,uint8_t words){ append(o,rc); append(o,0); append(o,0x20); append(o,words); };
        auto receive=[&](uint8_t channel) {
            auto& r=rx[channel & 31u];
            constexpr size_t header_len = 23; // 5 dwords plus 3-byte JVS header
            const uint8_t words = r.empty() ? 5u :
                static_cast<uint8_t>((r.size() + header_len - 1u) / 4u + 1u);
            hdr(0x87, words);
            append(o, r.empty() ? 0x32 : 0x16);
            append(o, 0xff); append(o, 0xff); append(o, 0xff);
            append32le(o, 0xffffff00u); append32le(o, 0); append32le(o, 0);
            if (r.empty()) {
                append32le(o, 0);
            } else {
                append(o, 0); append(o, channel & 31u);
                append(o, crazy_mode ? 0x8e : senseLine(r[0]));
                o.insert(o.end(), r.begin(), r.end());
                while ((o.size() & 3u) != 0) append(o, 0);
                r.clear();
            }
        };
        auto transmit_reply=[&](uint8_t channel, uint8_t node, uint8_t code,
                                bool flycast_fixed_sense=false) {
            hdr(0x87, 1); append(o, code); append(o, channel);
            append(o, flycast_fixed_sense ? 0x8e :
                       senseLine(node == 0xff ? 1 : node));
            append(o, 0);
        };
        auto transmit=[&](uint8_t channel, uint8_t node, const uint8_t* data,
                           size_t length, bool use_repeat, bool repeat_first=false) {
            if (length != 0 && data)
                queueJvs(node, channel, data, length, use_repeat, repeat_first);
        };
        if (sub==0x01) {
            hdr(0x87,1); append(o,2); append(o,0); append(o,0); append(o,0);
        } else if (sub==0x03) {
            size_t addr=n>1?p[1]%eeprom.size():0;
            hdr(0x87,0x20); o.insert(o.end(),eeprom.begin()+addr,eeprom.end());
        } else if (sub==0x0B && n>=4) {
            size_t addr=p[1]%eeprom.size(), sz=std::min<size_t>(p[2],eeprom.size()-addr);
            if (4+sz<=n) std::memcpy(eeprom.data()+addr,p+4,sz);
            hdr(0x87,1); o.insert(o.end(),eeprom.begin(),eeprom.begin()+4);
        } else if (sub==0x31) {
            hdr(0x87,5);
            const uint8_t dip[]={0x32,0xff,0xff,0xff,0x00,0xff,0xf9,0xff};
            o.insert(o.end(),std::begin(dip),std::end(dip)); append32le(o,0); append32le(o,0); append32le(o,0);
        } else if (sub==0x13 && n>=3) {
            uint8_t node=p[1], len=p[2]; if(node>=1&&node<=32&&3+len<=n) repeat[node-1].assign(p+3,p+3+len);
            hdr(0x87,1); append(o,p[0]+1); append(o,0); append(o,len+1); append(o,0);
        } else if (sub==0x15 && n>=2) {
            receive(p[1]);
        } else if ((sub==0x17||sub==0x19||sub==0x21) && n>=8) {
            const uint8_t channel=p[5]&31, node=p[6], length=p[7];
            rx[channel].clear();
            if (8u + length <= n)
                transmit(channel,node,p+8,length,sub!=0x17,sub==0x19);
            transmit_reply(channel,node,0x18,sub==0x17);
        } else if (sub==0x27 && n>=7) {
            const uint8_t channel=p[5]&31, count=p[6];
            rx[channel].clear();
            size_t offset=7;
            uint8_t last_node=0;
            for (unsigned i=0;i<count && offset+2<=n;++i) {
                last_node=p[offset];
                const uint8_t length=p[offset+1];
                offset+=2;
                if (offset+length>n) break;
                transmit(channel,last_node,p+offset,length,true);
                offset+=length;
            }
            transmit_reply(channel,last_node,0x26);
        } else if (sub==0x35 && n>=7) {
            receive(p[1]);
            const uint8_t channel=p[5]&31, count=p[6];
            rx[channel].clear();
            size_t offset=7;
            uint8_t last_node=0xff;
            for (unsigned i=0;i<count && offset+2<=n;++i) {
                last_node=p[offset];
                const uint8_t length=p[offset+1];
                offset+=2;
                if (offset+length>n) break;
                transmit(channel,last_node,p+offset,length,true);
                offset+=length;
            }
            transmit_reply(channel,last_node,0x26);
        } else if (sub==0x33 && n>=8) {
            receive(p[1]);
            const uint8_t channel=p[5]&31, node=p[6], length=p[7];
            rx[channel].clear();
            if (8u + length <= n) transmit(channel,node,p+8,length,true);
            transmit_reply(channel,node,0x18);
        } else {
            hdr(0xFD,0);
        }
    }
};

inline NaomiJvs13551& g_naomiJvs13551() { static NaomiJvs13551 b; return b; }

} // namespace multiarcaderecomp::naomi
