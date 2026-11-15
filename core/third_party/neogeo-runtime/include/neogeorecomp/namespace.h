/*
 * The Neo Geo runtime and MameRecomp's generic 68000 runtime can be loaded
 * into the same process. Keep the Neo Geo CPU/bus ABI distinct so ELF symbol
 * lookup cannot redirect Neo Geo AoT accesses into another board's bus.
 */
#ifndef NEOGEORECOMP_NAMESPACE_H
#define NEOGEORECOMP_NAMESPACE_H

#define ng_cpu mamerecomp_neogeo_cpu
#define ng_cycles mamerecomp_neogeo_cycles
#define ng_next_event mamerecomp_neogeo_next_event
#define ng_icount mamerecomp_neogeo_icount
#define ng_debug_instruction_hook mamerecomp_neogeo_debug_instruction_hook

#define ng_rmap mamerecomp_neogeo_rmap
#define ng_wmap mamerecomp_neogeo_wmap
#define ng_protection_active mamerecomp_neogeo_protection_active
#define ng_r8_slow mamerecomp_neogeo_r8_slow
#define ng_r16_slow mamerecomp_neogeo_r16_slow
#define ng_w8_slow mamerecomp_neogeo_w8_slow
#define ng_w16_slow mamerecomp_neogeo_w16_slow

#define ng_bad_dispatch mamerecomp_neogeo_bad_dispatch
#define ng_set_sr mamerecomp_neogeo_set_sr
#define ng_exception mamerecomp_neogeo_exception

#endif
