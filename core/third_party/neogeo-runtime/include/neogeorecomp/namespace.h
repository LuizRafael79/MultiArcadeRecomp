/*
 * The Neo Geo runtime and MultiArcadeRecomp's generic 68000 runtime can be loaded
 * into the same process. Keep the Neo Geo CPU/bus ABI distinct so ELF symbol
 * lookup cannot redirect Neo Geo AoT accesses into another board's bus.
 */
#ifndef NEOGEORECOMP_NAMESPACE_H
#define NEOGEORECOMP_NAMESPACE_H

#define ng_cpu multiarcaderecomp_neogeo_cpu
#define ng_cycles multiarcaderecomp_neogeo_cycles
#define ng_next_event multiarcaderecomp_neogeo_next_event
#define ng_icount multiarcaderecomp_neogeo_icount
#define ng_debug_instruction_hook multiarcaderecomp_neogeo_debug_instruction_hook

#define ng_rmap multiarcaderecomp_neogeo_rmap
#define ng_wmap multiarcaderecomp_neogeo_wmap
#define ng_protection_active multiarcaderecomp_neogeo_protection_active
#define ng_r8_slow multiarcaderecomp_neogeo_r8_slow
#define ng_r16_slow multiarcaderecomp_neogeo_r16_slow
#define ng_w8_slow multiarcaderecomp_neogeo_w8_slow
#define ng_w16_slow multiarcaderecomp_neogeo_w16_slow

#define ng_bad_dispatch multiarcaderecomp_neogeo_bad_dispatch
#define ng_set_sr multiarcaderecomp_neogeo_set_sr
#define ng_exception multiarcaderecomp_neogeo_exception

#endif
