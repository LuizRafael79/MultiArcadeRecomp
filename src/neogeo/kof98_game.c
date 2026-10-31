/* KOF98 MVS / PROGSF1 prototype profile.
 * ROM wiring and the protection behavior follow MAME's neogeo.cpp and
 * devices/bus/neogeo/prot_kof98.cpp. The transform is game-specific; the
 * bus, BIOS, LSPC, sound and renderer remain the shared Neo Geo runtime.
 */
#include "kof98_game.h"

#include <stdlib.h>
#include <string.h>

static unsigned s_protection_state;

void mamerecomp_kof98_decrypt(uint8_t *program, size_t size) {
    if (!program || size < 0x600000) return;
    uint8_t *dst = (uint8_t *)malloc(0x200000);
    if (!dst) return;
    memcpy(dst, program, 0x200000);
    static const uint32_t sec[] = {0x000000,0x100000,0x000004,0x100004,
        0x10000a,0x00000a,0x10000e,0x00000e};
    static const uint32_t pos[] = {0x000,0x004,0x00a,0x00e};
    for (uint32_t i=0x800; i<0x100000; i+=0x200) {
        for (uint32_t j=0; j<0x100; j+=0x10) {
            for (uint32_t k=0; k<16; k+=2) {
                memcpy(program+i+j+k, dst+i+j+sec[k/2]+0x100, 2);
                memcpy(program+i+j+k+0x100, dst+i+j+sec[k/2], 2);
            }
            if (i>=0x080000 && i<0x0c0000) {
                for (uint32_t k=0;k<4;k++) {
                    memcpy(program+i+j+pos[k], dst+i+j+pos[k], 2);
                    memcpy(program+i+j+pos[k]+0x100, dst+i+j+pos[k]+0x100, 2);
                }
            } else if (i>=0x0c0000) {
                for (uint32_t k=0;k<4;k++) {
                    memcpy(program+i+j+pos[k], dst+i+j+pos[k]+0x100, 2);
                    memcpy(program+i+j+pos[k]+0x100, dst+i+j+pos[k], 2);
                }
            }
        }
        memcpy(program+i, dst+i, 2);
        memcpy(program+i+2, dst+0x100000+i, 2);
        memcpy(program+i+0x100, dst+i+0x100, 2);
        memcpy(program+i+0x102, dst+0x100100+i, 2);
    }
    memmove(program+0x100000, program+0x200000, 0x400000);
    free(dst);
}

void mamerecomp_kof98_prot_reset(void) { s_protection_state=0; }

uint16_t mamerecomp_kof98_prot_read(void *context, uint32_t offset) {
    const uint8_t *program=(const uint8_t *)context;
    if (s_protection_state==1) return offset ? 0x00fd : 0x00c2;
    if (s_protection_state==2) return offset ? 0x4f2d : 0x4e45;
    if (!program || offset>1) return 0xffff;
    const size_t a=0x100+offset*2;
    return (uint16_t)((program[a]<<8)|program[a+1]);
}

void mamerecomp_kof98_prot_write(void *context, uint32_t address, uint16_t data) {
    (void)context; (void)address;
    if (data==0x0090) s_protection_state=1;
    else if (data==0x00f0) s_protection_state=2;
}

const ng_game_t mamerecomp_kof98_game = {
    .name="kof98", .title="The King of Fighters '98 (MVS)",
    .prom_size=0x600000,
    .prom={
        {"242-p1.p1",0,0x000000,0x200000,1},
        {"242-p2.sp2",0,0x200000,0x400000,1},
    },
    .crom_size=0x4000000,
    .crom_pairs={
        {"242-c1.c1","242-c2.c2"},{"242-c3.c3","242-c4.c4"},
        {"242-c5.c5","242-c6.c6"},{"242-c7.c7","242-c8.c8"},
    },
    .srom="242-s1.s1", .m1="242-m1.m1",
    .vrom={"242-v1.v1","242-v2.v2","242-v3.v3","242-v4.v4"},
    .prom_transform=mamerecomp_kof98_decrypt,
    .protection_read=mamerecomp_kof98_prot_read,
    .protection_write=mamerecomp_kof98_prot_write,
};
