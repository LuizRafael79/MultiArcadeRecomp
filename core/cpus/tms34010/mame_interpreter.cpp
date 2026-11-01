// TMS34010 interpreter adapter. Opcode handlers and field helpers are
// adapted from MAME's BSD-3-Clause implementation; see mame_source/README.md.
#include "mame_interpreter.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>

namespace mamerecomp::tms34010 {

static constexpr std::uint32_t STBIT_N = 1u << 31;
static constexpr std::uint32_t STBIT_C = 1u << 30;
static constexpr std::uint32_t STBIT_Z = 1u << 29;
static constexpr std::uint32_t STBIT_V = 1u << 28;
static constexpr std::uint32_t STBIT_P = 1u << 25;
static constexpr std::uint32_t STBIT_IE = 1u << 21;
static constexpr std::uint32_t STBIT_FE1 = 1u << 11;
static constexpr std::uint32_t STBIT_FE0 = 1u << 5;
static constexpr std::uint32_t sext(std::uint32_t value, unsigned bits) {
    const std::uint32_t sign = 1u << (bits - 1);
    return (value ^ sign) - sign;
}
static constexpr std::int64_t mul_32x32(std::int32_t a, std::int32_t b) {
    return static_cast<std::int64_t>(a) * b;
}
static constexpr std::uint64_t mulu_32x32(std::uint32_t a, std::uint32_t b) {
    return static_cast<std::uint64_t>(a) * b;
}

enum : unsigned { REG_HESYNC=0, REG_HEBLNK, REG_HSBLNK, REG_HTOTAL,
    REG_VESYNC, REG_VEBLNK, REG_VSBLNK, REG_VTOTAL, REG_DPYCTL,
    REG_DPYSTRT, REG_DPYINT, REG_CONTROL, REG_HSTDATA, REG_HSTADRL,
    REG_HSTADRH, REG_HSTCTLL, REG_HSTCTLH, REG_INTENB, REG_INTPEND,
    REG_CONVSP, REG_CONVDP, REG_PSIZE, REG_PMASK, REG_UNK23, REG_UNK24,
    REG_UNK25, REG_UNK26, REG_DPYTAP, REG_HCOUNT, REG_VCOUNT, REG_DPYADR,
    REG_REFCNT };

#define N_FLAG() (m_st & STBIT_N)
#define Z_FLAG() (m_st & STBIT_Z)
#define C_FLAG() (m_st & STBIT_C)
#define V_FLAG() (m_st & STBIT_V)
#define P_FLAG() (m_st & STBIT_P)
#define IE_FLAG() (m_st & STBIT_IE)
#define FE0_FLAG() (m_st & STBIT_FE0)
#define FE1_FLAG() (m_st & STBIT_FE1)
#define AREG(i) (m_regs[i].reg)
#define AREG_XY(i) (m_regs[i].xy)
#define AREG_X(i) (m_regs[i].xy.x)
#define AREG_Y(i) (m_regs[i].xy.y)
#define BREG(i) (m_regs[30 - (i)].reg)
#define BREG_XY(i) (m_regs[30 - (i)].xy)
#define BREG_X(i) (m_regs[30 - (i)].xy.x)
#define BREG_Y(i) (m_regs[30 - (i)].xy.y)
#define SP() AREG(15)
#define FW(i) ((m_st >> ((i) ? 6 : 0)) & 0x1f)
#define FWEX(i) ((m_st >> ((i) ? 6 : 0)) & 0x3f)
#define SRCREG(O) (((O) >> 5) & 0x0f)
#define DSTREG(O) ((O) & 0x0f)
#define SKIP_WORD() (m_pc += 16)
#define SKIP_LONG() (m_pc += 32)
#define PARAM_K(O) (((O) >> 5) & 0x1f)
#define PARAM_N(O) ((O) & 0x1f)
#define PARAM_REL8(O) (static_cast<std::int8_t>(O))
#define WFIELD0(a,b) (this->*s_wfield_functions[FW(0)])(a,b)
#define WFIELD1(a,b) (this->*s_wfield_functions[FW(1)])(a,b)
#define RFIELD0(a) (this->*s_rfield_functions[FWEX(0)])(a)
#define RFIELD1(a) (this->*s_rfield_functions[FWEX(1)])(a)
#define WPIXEL(a,b) (this->*m_pixel_write)(a,b)
#define RPIXEL(a) (this->*m_pixel_read)(a)
#define SADDR() BREG(0)
#define SADDR_X() BREG_X(0)
#define SADDR_Y() BREG_Y(0)
#define SADDR_XY() BREG_XY(0)
#define SPTCH() BREG(1)
#define DADDR() BREG(2)
#define DADDR_X() BREG_X(2)
#define DADDR_Y() BREG_Y(2)
#define DADDR_XY() BREG_XY(2)
#define DPTCH() BREG(3)
#define OFFSET() BREG(4)
#define WSTART_X() BREG_X(5)
#define WSTART_Y() BREG_Y(5)
#define WSTART_XY() BREG_XY(5)
#define WEND_X() BREG_X(6)
#define WEND_Y() BREG_Y(6)
#define WEND_XY() BREG_XY(6)
#define DYDX_X() BREG_X(7)
#define DYDX_Y() BREG_Y(7)
#define DYDX_XY() BREG_XY(7)
#define COLOR0() BREG(8)
#define COLOR1() BREG(9)
#define COUNT() BREG(10)
#define INC1_X() BREG_X(11)
#define INC1_Y() BREG_Y(11)
#define INC2_X() BREG_X(12)
#define INC2_Y() BREG_Y(12)
#define PATTRN() BREG(13)
#define TEMP() BREG(14)
#define WINDOW_CHECKING() ((IOREG(REG_CONTROL) >> 6) & 3)
#define SET_ST(value) set_st(value)
#define RESET_ST() reset_st()
#define check_interrupt() ((void)0)
#define logerror(...) ((void)0)
#define TMS34010_RDMEM_WORD(A) read_word(A)
#define TMS34010_RDMEM_DWORD(A) read_dword(A)
#define TMS34010_WRMEM_WORD(A,V) write_word(A,V)
#define TMS34010_WRMEM_DWORD(A,V) write_dword(A,V)

#include "mame_source/34010ops.h"

std::uint32_t MameTMS34010::read_word(offs_t a) {
    if (!m_bus || !m_bus->read16) { m_bus_fault = true; return 0; }
    return m_bus->read16(m_bus->opaque, a & ~15u);
}
std::uint32_t MameTMS34010::read_dword(offs_t a) {
    return read_word(a) | (read_word(a + 16) << 16);
}
void MameTMS34010::write_word(offs_t a, std::uint32_t v) {
    if (!m_bus || !m_bus->write16) { m_bus_fault = true; return; }
    a &= ~15u;
    m_bus->write16(m_bus->opaque, a, static_cast<std::uint16_t>(v));
    if ((a & 0xfffffe00u) == 0xc0000000u) {
        const unsigned index = (a - 0xc0000000u) >> 4;
        if (index < 32) m_IOregs[index] = static_cast<std::uint16_t>(v);
        if (index == REG_PSIZE) {
            const auto size = static_cast<std::uint16_t>(v);
            m_pixelshift = size == 0x02 ? 1 : size == 0x04 ? 2 : size == 0x08 ? 3 : size == 0x10 ? 4 : 0;
        }
        auto convert = [](std::uint16_t data) -> std::uint32_t {
            if (!(data & 0x1f)) return data;
            const unsigned low = (~data) & 0x1f;
            std::uint32_t result = 1u << low;
            if (data & 0x1f00) result += 1u << ((~(data >> 8)) & 0x1f);
            return result;
        };
        if (index == REG_CONVSP) m_convsp = convert(static_cast<std::uint16_t>(v));
        if (index == REG_CONVDP) m_convdp = convert(static_cast<std::uint16_t>(v));
        if (index == 22) m_convmp = convert(static_cast<std::uint16_t>(v));
    }
}
void MameTMS34010::write_dword(offs_t a, std::uint32_t v) {
    write_word(a, v); write_word(a + 16, v >> 16);
}
std::uint32_t MameTMS34010::RBYTE(offs_t a) { std::uint32_t v=0; (void)read_field(*m_bus,a,m_st,0,v); return v & 255; }
void MameTMS34010::WBYTE(offs_t a,std::uint32_t v) { (void)write_field(*m_bus,a,v,m_st,0); }
std::uint32_t MameTMS34010::RLONG(offs_t a) { std::uint32_t v=0; (void)read_long(*m_bus,a,v); return v; }
void MameTMS34010::WLONG(offs_t a,std::uint32_t v) { (void)write_long(*m_bus,a,v); }
void MameTMS34010::PUSH(std::uint32_t v) { SP() -= 32; write_dword(SP(),v); }
std::int32_t MameTMS34010::POP() { auto v=static_cast<std::int32_t>(read_dword(SP())); SP() += 32; return v; }
std::uint16_t MameTMS34010::ROPCODE() { auto v=static_cast<std::uint16_t>(read_word(m_pc)); m_pc+=16; return v; }
std::int16_t MameTMS34010::PARAM_WORD() { return static_cast<std::int16_t>(ROPCODE()); }
std::int32_t MameTMS34010::PARAM_LONG() { auto v=static_cast<std::int32_t>(read_dword(m_pc)); m_pc+=32; return v; }
std::int16_t MameTMS34010::PARAM_WORD_NO_INC() { return static_cast<std::int16_t>(read_word(m_pc)); }
std::int32_t MameTMS34010::PARAM_LONG_NO_INC() { return static_cast<std::int32_t>(read_dword(m_pc)); }
void MameTMS34010::write_pixel(offs_t a,std::uint32_t v) { if(m_bus&&m_bus->write_pixel) { if(!m_bus->write_pixel(m_bus->opaque,a,v)) m_bus_fault=true; } else write_word(a,v); }
std::uint32_t MameTMS34010::read_pixel(offs_t a) { return m_bus&&m_bus->read_pixel ? m_bus->read_pixel(m_bus->opaque,a) : read_word(a); }
void MameTMS34010::set_st(std::uint32_t v) { m_st=v; }
void MameTMS34010::reset_st() { m_st=0x10; }

#include "mame_fields.inc"
#include "mame_ops.inc"
#include "mame_opcode_table.inc"

const char* MameTMS34010::opcode_handler_name(std::uint16_t opcode) noexcept {
    const auto handler = s_opcode_table[opcode >> 4];
    // Generated from unique handler targets in MAME's 34010 opcode table.
        if (handler == &MameTMS34010::abs_a) return "abs_a";
    if (handler == &MameTMS34010::abs_b) return "abs_b";
    if (handler == &MameTMS34010::add_a) return "add_a";
    if (handler == &MameTMS34010::add_b) return "add_b";
    if (handler == &MameTMS34010::add_xy_a) return "add_xy_a";
    if (handler == &MameTMS34010::add_xy_b) return "add_xy_b";
    if (handler == &MameTMS34010::addc_a) return "addc_a";
    if (handler == &MameTMS34010::addc_b) return "addc_b";
    if (handler == &MameTMS34010::addi_l_a) return "addi_l_a";
    if (handler == &MameTMS34010::addi_l_b) return "addi_l_b";
    if (handler == &MameTMS34010::addi_w_a) return "addi_w_a";
    if (handler == &MameTMS34010::addi_w_b) return "addi_w_b";
    if (handler == &MameTMS34010::addk_a) return "addk_a";
    if (handler == &MameTMS34010::addk_b) return "addk_b";
    if (handler == &MameTMS34010::and_a) return "and_a";
    if (handler == &MameTMS34010::and_b) return "and_b";
    if (handler == &MameTMS34010::andi_a) return "andi_a";
    if (handler == &MameTMS34010::andi_b) return "andi_b";
    if (handler == &MameTMS34010::andn_a) return "andn_a";
    if (handler == &MameTMS34010::andn_b) return "andn_b";
    if (handler == &MameTMS34010::btst_k_a) return "btst_k_a";
    if (handler == &MameTMS34010::btst_k_b) return "btst_k_b";
    if (handler == &MameTMS34010::btst_r_a) return "btst_r_a";
    if (handler == &MameTMS34010::btst_r_b) return "btst_r_b";
    if (handler == &MameTMS34010::call_a) return "call_a";
    if (handler == &MameTMS34010::call_b) return "call_b";
    if (handler == &MameTMS34010::calla) return "calla";
    if (handler == &MameTMS34010::callr) return "callr";
    if (handler == &MameTMS34010::clrc) return "clrc";
    if (handler == &MameTMS34010::cmp_a) return "cmp_a";
    if (handler == &MameTMS34010::cmp_b) return "cmp_b";
    if (handler == &MameTMS34010::cmp_xy_a) return "cmp_xy_a";
    if (handler == &MameTMS34010::cmp_xy_b) return "cmp_xy_b";
    if (handler == &MameTMS34010::cmpi_l_a) return "cmpi_l_a";
    if (handler == &MameTMS34010::cmpi_l_b) return "cmpi_l_b";
    if (handler == &MameTMS34010::cmpi_w_a) return "cmpi_w_a";
    if (handler == &MameTMS34010::cmpi_w_b) return "cmpi_w_b";
    if (handler == &MameTMS34010::cpw_a) return "cpw_a";
    if (handler == &MameTMS34010::cpw_b) return "cpw_b";
    if (handler == &MameTMS34010::cvxyl_a) return "cvxyl_a";
    if (handler == &MameTMS34010::cvxyl_b) return "cvxyl_b";
    if (handler == &MameTMS34010::dint) return "dint";
    if (handler == &MameTMS34010::divs_a) return "divs_a";
    if (handler == &MameTMS34010::divs_b) return "divs_b";
    if (handler == &MameTMS34010::divu_a) return "divu_a";
    if (handler == &MameTMS34010::divu_b) return "divu_b";
    if (handler == &MameTMS34010::drav_a) return "drav_a";
    if (handler == &MameTMS34010::drav_b) return "drav_b";
    if (handler == &MameTMS34010::dsj_a) return "dsj_a";
    if (handler == &MameTMS34010::dsj_b) return "dsj_b";
    if (handler == &MameTMS34010::dsjeq_a) return "dsjeq_a";
    if (handler == &MameTMS34010::dsjeq_b) return "dsjeq_b";
    if (handler == &MameTMS34010::dsjne_a) return "dsjne_a";
    if (handler == &MameTMS34010::dsjne_b) return "dsjne_b";
    if (handler == &MameTMS34010::dsjs_a) return "dsjs_a";
    if (handler == &MameTMS34010::dsjs_b) return "dsjs_b";
    if (handler == &MameTMS34010::eint) return "eint";
    if (handler == &MameTMS34010::emu) return "emu";
    if (handler == &MameTMS34010::exgf0_a) return "exgf0_a";
    if (handler == &MameTMS34010::exgf0_b) return "exgf0_b";
    if (handler == &MameTMS34010::exgf1_a) return "exgf1_a";
    if (handler == &MameTMS34010::exgf1_b) return "exgf1_b";
    if (handler == &MameTMS34010::exgpc_a) return "exgpc_a";
    if (handler == &MameTMS34010::exgpc_b) return "exgpc_b";
    if (handler == &MameTMS34010::fill_l) return "fill_l";
    if (handler == &MameTMS34010::fill_xy) return "fill_xy";
    if (handler == &MameTMS34010::getpc_a) return "getpc_a";
    if (handler == &MameTMS34010::getpc_b) return "getpc_b";
    if (handler == &MameTMS34010::getst_a) return "getst_a";
    if (handler == &MameTMS34010::getst_b) return "getst_b";
    if (handler == &MameTMS34010::illop) return "illop";
    if (handler == &MameTMS34010::j_C_0) return "j_C_0";
    if (handler == &MameTMS34010::j_C_8) return "j_C_8";
    if (handler == &MameTMS34010::j_C_x) return "j_C_x";
    if (handler == &MameTMS34010::j_EQ_0) return "j_EQ_0";
    if (handler == &MameTMS34010::j_EQ_8) return "j_EQ_8";
    if (handler == &MameTMS34010::j_EQ_x) return "j_EQ_x";
    if (handler == &MameTMS34010::j_GE_0) return "j_GE_0";
    if (handler == &MameTMS34010::j_GE_8) return "j_GE_8";
    if (handler == &MameTMS34010::j_GE_x) return "j_GE_x";
    if (handler == &MameTMS34010::j_GT_0) return "j_GT_0";
    if (handler == &MameTMS34010::j_GT_8) return "j_GT_8";
    if (handler == &MameTMS34010::j_GT_x) return "j_GT_x";
    if (handler == &MameTMS34010::j_HI_0) return "j_HI_0";
    if (handler == &MameTMS34010::j_HI_8) return "j_HI_8";
    if (handler == &MameTMS34010::j_HI_x) return "j_HI_x";
    if (handler == &MameTMS34010::j_LE_0) return "j_LE_0";
    if (handler == &MameTMS34010::j_LE_8) return "j_LE_8";
    if (handler == &MameTMS34010::j_LE_x) return "j_LE_x";
    if (handler == &MameTMS34010::j_LS_0) return "j_LS_0";
    if (handler == &MameTMS34010::j_LS_8) return "j_LS_8";
    if (handler == &MameTMS34010::j_LS_x) return "j_LS_x";
    if (handler == &MameTMS34010::j_LT_0) return "j_LT_0";
    if (handler == &MameTMS34010::j_LT_8) return "j_LT_8";
    if (handler == &MameTMS34010::j_LT_x) return "j_LT_x";
    if (handler == &MameTMS34010::j_NC_0) return "j_NC_0";
    if (handler == &MameTMS34010::j_NC_8) return "j_NC_8";
    if (handler == &MameTMS34010::j_NC_x) return "j_NC_x";
    if (handler == &MameTMS34010::j_NE_0) return "j_NE_0";
    if (handler == &MameTMS34010::j_NE_8) return "j_NE_8";
    if (handler == &MameTMS34010::j_NE_x) return "j_NE_x";
    if (handler == &MameTMS34010::j_NN_0) return "j_NN_0";
    if (handler == &MameTMS34010::j_NN_8) return "j_NN_8";
    if (handler == &MameTMS34010::j_NN_x) return "j_NN_x";
    if (handler == &MameTMS34010::j_NV_0) return "j_NV_0";
    if (handler == &MameTMS34010::j_NV_8) return "j_NV_8";
    if (handler == &MameTMS34010::j_NV_x) return "j_NV_x";
    if (handler == &MameTMS34010::j_N_0) return "j_N_0";
    if (handler == &MameTMS34010::j_N_8) return "j_N_8";
    if (handler == &MameTMS34010::j_N_x) return "j_N_x";
    if (handler == &MameTMS34010::j_P_0) return "j_P_0";
    if (handler == &MameTMS34010::j_P_8) return "j_P_8";
    if (handler == &MameTMS34010::j_P_x) return "j_P_x";
    if (handler == &MameTMS34010::j_UC_0) return "j_UC_0";
    if (handler == &MameTMS34010::j_UC_8) return "j_UC_8";
    if (handler == &MameTMS34010::j_UC_x) return "j_UC_x";
    if (handler == &MameTMS34010::j_V_0) return "j_V_0";
    if (handler == &MameTMS34010::j_V_8) return "j_V_8";
    if (handler == &MameTMS34010::j_V_x) return "j_V_x";
    if (handler == &MameTMS34010::jump_a) return "jump_a";
    if (handler == &MameTMS34010::jump_b) return "jump_b";
    if (handler == &MameTMS34010::line) return "line";
    if (handler == &MameTMS34010::lmo_a) return "lmo_a";
    if (handler == &MameTMS34010::lmo_b) return "lmo_b";
    if (handler == &MameTMS34010::mmfm_a) return "mmfm_a";
    if (handler == &MameTMS34010::mmfm_b) return "mmfm_b";
    if (handler == &MameTMS34010::mmtm_a) return "mmtm_a";
    if (handler == &MameTMS34010::mmtm_b) return "mmtm_b";
    if (handler == &MameTMS34010::mods_a) return "mods_a";
    if (handler == &MameTMS34010::mods_b) return "mods_b";
    if (handler == &MameTMS34010::modu_a) return "modu_a";
    if (handler == &MameTMS34010::modu_b) return "modu_b";
    if (handler == &MameTMS34010::movb_aa) return "movb_aa";
    if (handler == &MameTMS34010::movb_ar_a) return "movb_ar_a";
    if (handler == &MameTMS34010::movb_ar_b) return "movb_ar_b";
    if (handler == &MameTMS34010::movb_nn_a) return "movb_nn_a";
    if (handler == &MameTMS34010::movb_nn_b) return "movb_nn_b";
    if (handler == &MameTMS34010::movb_no_no_a) return "movb_no_no_a";
    if (handler == &MameTMS34010::movb_no_no_b) return "movb_no_no_b";
    if (handler == &MameTMS34010::movb_no_r_a) return "movb_no_r_a";
    if (handler == &MameTMS34010::movb_no_r_b) return "movb_no_r_b";
    if (handler == &MameTMS34010::movb_nr_a) return "movb_nr_a";
    if (handler == &MameTMS34010::movb_nr_b) return "movb_nr_b";
    if (handler == &MameTMS34010::movb_r_no_a) return "movb_r_no_a";
    if (handler == &MameTMS34010::movb_r_no_b) return "movb_r_no_b";
    if (handler == &MameTMS34010::movb_ra_a) return "movb_ra_a";
    if (handler == &MameTMS34010::movb_ra_b) return "movb_ra_b";
    if (handler == &MameTMS34010::movb_rn_a) return "movb_rn_a";
    if (handler == &MameTMS34010::movb_rn_b) return "movb_rn_b";
    if (handler == &MameTMS34010::move0_a_ni_a) return "move0_a_ni_a";
    if (handler == &MameTMS34010::move0_a_ni_b) return "move0_a_ni_b";
    if (handler == &MameTMS34010::move0_aa) return "move0_aa";
    if (handler == &MameTMS34010::move0_ar_a) return "move0_ar_a";
    if (handler == &MameTMS34010::move0_ar_b) return "move0_ar_b";
    if (handler == &MameTMS34010::move0_dn_dn_a) return "move0_dn_dn_a";
    if (handler == &MameTMS34010::move0_dn_dn_b) return "move0_dn_dn_b";
    if (handler == &MameTMS34010::move0_dn_r_a) return "move0_dn_r_a";
    if (handler == &MameTMS34010::move0_dn_r_b) return "move0_dn_r_b";
    if (handler == &MameTMS34010::move0_ni_ni_a) return "move0_ni_ni_a";
    if (handler == &MameTMS34010::move0_ni_ni_b) return "move0_ni_ni_b";
    if (handler == &MameTMS34010::move0_ni_r_a) return "move0_ni_r_a";
    if (handler == &MameTMS34010::move0_ni_r_b) return "move0_ni_r_b";
    if (handler == &MameTMS34010::move0_nn_a) return "move0_nn_a";
    if (handler == &MameTMS34010::move0_nn_b) return "move0_nn_b";
    if (handler == &MameTMS34010::move0_no_ni_a) return "move0_no_ni_a";
    if (handler == &MameTMS34010::move0_no_ni_b) return "move0_no_ni_b";
    if (handler == &MameTMS34010::move0_no_no_a) return "move0_no_no_a";
    if (handler == &MameTMS34010::move0_no_no_b) return "move0_no_no_b";
    if (handler == &MameTMS34010::move0_no_r_a) return "move0_no_r_a";
    if (handler == &MameTMS34010::move0_no_r_b) return "move0_no_r_b";
    if (handler == &MameTMS34010::move0_nr_a) return "move0_nr_a";
    if (handler == &MameTMS34010::move0_nr_b) return "move0_nr_b";
    if (handler == &MameTMS34010::move0_r_dn_a) return "move0_r_dn_a";
    if (handler == &MameTMS34010::move0_r_dn_b) return "move0_r_dn_b";
    if (handler == &MameTMS34010::move0_r_ni_a) return "move0_r_ni_a";
    if (handler == &MameTMS34010::move0_r_ni_b) return "move0_r_ni_b";
    if (handler == &MameTMS34010::move0_r_no_a) return "move0_r_no_a";
    if (handler == &MameTMS34010::move0_r_no_b) return "move0_r_no_b";
    if (handler == &MameTMS34010::move0_ra_a) return "move0_ra_a";
    if (handler == &MameTMS34010::move0_ra_b) return "move0_ra_b";
    if (handler == &MameTMS34010::move0_rn_a) return "move0_rn_a";
    if (handler == &MameTMS34010::move0_rn_b) return "move0_rn_b";
    if (handler == &MameTMS34010::move1_a_ni_a) return "move1_a_ni_a";
    if (handler == &MameTMS34010::move1_a_ni_b) return "move1_a_ni_b";
    if (handler == &MameTMS34010::move1_aa) return "move1_aa";
    if (handler == &MameTMS34010::move1_ar_a) return "move1_ar_a";
    if (handler == &MameTMS34010::move1_ar_b) return "move1_ar_b";
    if (handler == &MameTMS34010::move1_dn_dn_a) return "move1_dn_dn_a";
    if (handler == &MameTMS34010::move1_dn_dn_b) return "move1_dn_dn_b";
    if (handler == &MameTMS34010::move1_dn_r_a) return "move1_dn_r_a";
    if (handler == &MameTMS34010::move1_dn_r_b) return "move1_dn_r_b";
    if (handler == &MameTMS34010::move1_ni_ni_a) return "move1_ni_ni_a";
    if (handler == &MameTMS34010::move1_ni_ni_b) return "move1_ni_ni_b";
    if (handler == &MameTMS34010::move1_ni_r_a) return "move1_ni_r_a";
    if (handler == &MameTMS34010::move1_ni_r_b) return "move1_ni_r_b";
    if (handler == &MameTMS34010::move1_nn_a) return "move1_nn_a";
    if (handler == &MameTMS34010::move1_nn_b) return "move1_nn_b";
    if (handler == &MameTMS34010::move1_no_ni_a) return "move1_no_ni_a";
    if (handler == &MameTMS34010::move1_no_ni_b) return "move1_no_ni_b";
    if (handler == &MameTMS34010::move1_no_no_a) return "move1_no_no_a";
    if (handler == &MameTMS34010::move1_no_no_b) return "move1_no_no_b";
    if (handler == &MameTMS34010::move1_no_r_a) return "move1_no_r_a";
    if (handler == &MameTMS34010::move1_no_r_b) return "move1_no_r_b";
    if (handler == &MameTMS34010::move1_nr_a) return "move1_nr_a";
    if (handler == &MameTMS34010::move1_nr_b) return "move1_nr_b";
    if (handler == &MameTMS34010::move1_r_dn_a) return "move1_r_dn_a";
    if (handler == &MameTMS34010::move1_r_dn_b) return "move1_r_dn_b";
    if (handler == &MameTMS34010::move1_r_ni_a) return "move1_r_ni_a";
    if (handler == &MameTMS34010::move1_r_ni_b) return "move1_r_ni_b";
    if (handler == &MameTMS34010::move1_r_no_a) return "move1_r_no_a";
    if (handler == &MameTMS34010::move1_r_no_b) return "move1_r_no_b";
    if (handler == &MameTMS34010::move1_ra_a) return "move1_ra_a";
    if (handler == &MameTMS34010::move1_ra_b) return "move1_ra_b";
    if (handler == &MameTMS34010::move1_rn_a) return "move1_rn_a";
    if (handler == &MameTMS34010::move1_rn_b) return "move1_rn_b";
    if (handler == &MameTMS34010::move_rr_a) return "move_rr_a";
    if (handler == &MameTMS34010::move_rr_ax) return "move_rr_ax";
    if (handler == &MameTMS34010::move_rr_b) return "move_rr_b";
    if (handler == &MameTMS34010::move_rr_bx) return "move_rr_bx";
    if (handler == &MameTMS34010::movi_l_a) return "movi_l_a";
    if (handler == &MameTMS34010::movi_l_b) return "movi_l_b";
    if (handler == &MameTMS34010::movi_w_a) return "movi_w_a";
    if (handler == &MameTMS34010::movi_w_b) return "movi_w_b";
    if (handler == &MameTMS34010::movk_a) return "movk_a";
    if (handler == &MameTMS34010::movk_b) return "movk_b";
    if (handler == &MameTMS34010::movx_a) return "movx_a";
    if (handler == &MameTMS34010::movx_b) return "movx_b";
    if (handler == &MameTMS34010::movy_a) return "movy_a";
    if (handler == &MameTMS34010::movy_b) return "movy_b";
    if (handler == &MameTMS34010::mpys_a) return "mpys_a";
    if (handler == &MameTMS34010::mpys_b) return "mpys_b";
    if (handler == &MameTMS34010::mpyu_a) return "mpyu_a";
    if (handler == &MameTMS34010::mpyu_b) return "mpyu_b";
    if (handler == &MameTMS34010::neg_a) return "neg_a";
    if (handler == &MameTMS34010::neg_b) return "neg_b";
    if (handler == &MameTMS34010::negb_a) return "negb_a";
    if (handler == &MameTMS34010::negb_b) return "negb_b";
    if (handler == &MameTMS34010::nop) return "nop";
    if (handler == &MameTMS34010::not_a) return "not_a";
    if (handler == &MameTMS34010::not_b) return "not_b";
    if (handler == &MameTMS34010::or_a) return "or_a";
    if (handler == &MameTMS34010::or_b) return "or_b";
    if (handler == &MameTMS34010::ori_a) return "ori_a";
    if (handler == &MameTMS34010::ori_b) return "ori_b";
    if (handler == &MameTMS34010::pixblt_b_l) return "pixblt_b_l";
    if (handler == &MameTMS34010::pixblt_b_xy) return "pixblt_b_xy";
    if (handler == &MameTMS34010::pixblt_l_l) return "pixblt_l_l";
    if (handler == &MameTMS34010::pixblt_l_xy) return "pixblt_l_xy";
    if (handler == &MameTMS34010::pixblt_xy_l) return "pixblt_xy_l";
    if (handler == &MameTMS34010::pixblt_xy_xy) return "pixblt_xy_xy";
    if (handler == &MameTMS34010::pixt_ii_a) return "pixt_ii_a";
    if (handler == &MameTMS34010::pixt_ii_b) return "pixt_ii_b";
    if (handler == &MameTMS34010::pixt_ir_a) return "pixt_ir_a";
    if (handler == &MameTMS34010::pixt_ir_b) return "pixt_ir_b";
    if (handler == &MameTMS34010::pixt_ixyixy_a) return "pixt_ixyixy_a";
    if (handler == &MameTMS34010::pixt_ixyixy_b) return "pixt_ixyixy_b";
    if (handler == &MameTMS34010::pixt_ixyr_a) return "pixt_ixyr_a";
    if (handler == &MameTMS34010::pixt_ixyr_b) return "pixt_ixyr_b";
    if (handler == &MameTMS34010::pixt_ri_a) return "pixt_ri_a";
    if (handler == &MameTMS34010::pixt_ri_b) return "pixt_ri_b";
    if (handler == &MameTMS34010::pixt_rixy_a) return "pixt_rixy_a";
    if (handler == &MameTMS34010::pixt_rixy_b) return "pixt_rixy_b";
    if (handler == &MameTMS34010::popst) return "popst";
    if (handler == &MameTMS34010::pushst) return "pushst";
    if (handler == &MameTMS34010::putst_a) return "putst_a";
    if (handler == &MameTMS34010::putst_b) return "putst_b";
    if (handler == &MameTMS34010::reti) return "reti";
    if (handler == &MameTMS34010::rets) return "rets";
    if (handler == &MameTMS34010::rev_a) return "rev_a";
    if (handler == &MameTMS34010::rev_b) return "rev_b";
    if (handler == &MameTMS34010::rl_k_a) return "rl_k_a";
    if (handler == &MameTMS34010::rl_k_b) return "rl_k_b";
    if (handler == &MameTMS34010::rl_r_a) return "rl_r_a";
    if (handler == &MameTMS34010::rl_r_b) return "rl_r_b";
    if (handler == &MameTMS34010::setc) return "setc";
    if (handler == &MameTMS34010::setf0) return "setf0";
    if (handler == &MameTMS34010::setf1) return "setf1";
    if (handler == &MameTMS34010::sext0_a) return "sext0_a";
    if (handler == &MameTMS34010::sext0_b) return "sext0_b";
    if (handler == &MameTMS34010::sext1_a) return "sext1_a";
    if (handler == &MameTMS34010::sext1_b) return "sext1_b";
    if (handler == &MameTMS34010::sla_k_a) return "sla_k_a";
    if (handler == &MameTMS34010::sla_k_b) return "sla_k_b";
    if (handler == &MameTMS34010::sla_r_a) return "sla_r_a";
    if (handler == &MameTMS34010::sla_r_b) return "sla_r_b";
    if (handler == &MameTMS34010::sll_k_a) return "sll_k_a";
    if (handler == &MameTMS34010::sll_k_b) return "sll_k_b";
    if (handler == &MameTMS34010::sll_r_a) return "sll_r_a";
    if (handler == &MameTMS34010::sll_r_b) return "sll_r_b";
    if (handler == &MameTMS34010::sra_k_a) return "sra_k_a";
    if (handler == &MameTMS34010::sra_k_b) return "sra_k_b";
    if (handler == &MameTMS34010::sra_r_a) return "sra_r_a";
    if (handler == &MameTMS34010::sra_r_b) return "sra_r_b";
    if (handler == &MameTMS34010::srl_k_a) return "srl_k_a";
    if (handler == &MameTMS34010::srl_k_b) return "srl_k_b";
    if (handler == &MameTMS34010::srl_r_a) return "srl_r_a";
    if (handler == &MameTMS34010::srl_r_b) return "srl_r_b";
    if (handler == &MameTMS34010::sub_a) return "sub_a";
    if (handler == &MameTMS34010::sub_b) return "sub_b";
    if (handler == &MameTMS34010::sub_xy_a) return "sub_xy_a";
    if (handler == &MameTMS34010::sub_xy_b) return "sub_xy_b";
    if (handler == &MameTMS34010::subb_a) return "subb_a";
    if (handler == &MameTMS34010::subb_b) return "subb_b";
    if (handler == &MameTMS34010::subi_l_a) return "subi_l_a";
    if (handler == &MameTMS34010::subi_l_b) return "subi_l_b";
    if (handler == &MameTMS34010::subi_w_a) return "subi_w_a";
    if (handler == &MameTMS34010::subi_w_b) return "subi_w_b";
    if (handler == &MameTMS34010::subk_a) return "subk_a";
    if (handler == &MameTMS34010::subk_b) return "subk_b";
    if (handler == &MameTMS34010::trap) return "trap";
    if (handler == &MameTMS34010::unimpl) return "unimpl";
    if (handler == &MameTMS34010::xor_a) return "xor_a";
    if (handler == &MameTMS34010::xor_b) return "xor_b";
    if (handler == &MameTMS34010::xori_a) return "xori_a";
    if (handler == &MameTMS34010::xori_b) return "xori_b";
    if (handler == &MameTMS34010::zext0_a) return "zext0_a";
    if (handler == &MameTMS34010::zext0_b) return "zext0_b";
    if (handler == &MameTMS34010::zext1_a) return "zext1_a";
    if (handler == &MameTMS34010::zext1_b) return "zext1_b";
    return "unknown";
}

void MameTMS34010::line(std::uint16_t) { m_icount -= 1; }
void MameTMS34010::pixblt_b_l(std::uint16_t) { m_icount -= 1; }
void MameTMS34010::pixblt_b_xy(std::uint16_t) { m_icount -= 1; }
void MameTMS34010::pixblt_l_l(std::uint16_t) { m_icount -= 1; }
void MameTMS34010::pixblt_l_xy(std::uint16_t) { m_icount -= 1; }
void MameTMS34010::pixblt_xy_l(std::uint16_t) { m_icount -= 1; }
void MameTMS34010::pixblt_xy_xy(std::uint16_t) { m_icount -= 1; }
void MameTMS34010::fill_l(std::uint16_t) { m_icount -= 1; }
void MameTMS34010::fill_xy(std::uint16_t) { m_icount -= 1; }

BlockExit MameTMS34010::step(State& state, Bus& bus, std::uint64_t stop_cycle) {
    if (!bus.read16) return BlockExit::BusFault;
    m_state=&state; m_bus=&bus; m_bus_fault=false;
    for(unsigned i=0;i<15;i++) { m_regs[i].reg=state.a[i]; m_regs[30-i].reg=state.b[i]; }
    m_regs[15].reg=state.sp; m_pc=state.pc; m_ppc=state.pc;
    m_st=state.st; m_icount=1;
    for (unsigned i=0; i<32; ++i)
        m_IOregs[i] = static_cast<std::uint16_t>(bus.read16(bus.opaque, 0xc0000000u + i*16u));
    m_pixelshift = m_IOregs[REG_PSIZE] == 0x02 ? 1 : m_IOregs[REG_PSIZE] == 0x04 ? 2 : m_IOregs[REG_PSIZE] == 0x08 ? 3 : m_IOregs[REG_PSIZE] == 0x10 ? 4 : 0;
    auto convert = [](std::uint16_t data) -> std::uint32_t {
        if (!(data & 0x1f)) return data;
        std::uint32_t value = 1u << ((~data) & 0x1f);
        if (data & 0x1f00) value += 1u << ((~(data >> 8)) & 0x1f);
        return value;
    };
    m_convsp=convert(m_IOregs[REG_CONVSP]); m_convdp=convert(m_IOregs[REG_CONVDP]);
    m_convmp=convert(m_IOregs[22]);
    m_pixel_write=&MameTMS34010::write_pixel;
    m_pixel_read=&MameTMS34010::read_pixel;
    std::uint16_t op=ROPCODE();
    auto handler=s_opcode_table[op>>4];
    if (handler == &MameTMS34010::line ||
        handler == &MameTMS34010::pixblt_b_l || handler == &MameTMS34010::pixblt_b_xy ||
        handler == &MameTMS34010::pixblt_l_l || handler == &MameTMS34010::pixblt_l_xy ||
        handler == &MameTMS34010::pixblt_xy_l || handler == &MameTMS34010::pixblt_xy_xy ||
        handler == &MameTMS34010::fill_l || handler == &MameTMS34010::fill_xy) {
        state.pc=m_ppc;
        return BlockExit::NeedsInterpreter;
    }
    (this->*handler)(op);
    const int elapsed=std::max(1,1-m_icount);
    state.pc=m_pc; state.st=m_st; state.sp=m_regs[15].reg;
    for(unsigned i=0;i<15;i++) { state.a[i]=m_regs[i].reg; state.b[i]=m_regs[30-i].reg; }
    state.cycles += static_cast<unsigned>(elapsed);
    if(m_bus_fault) return BlockExit::BusFault;
    if(state.cycles>=stop_cycle) return BlockExit::Budget;
    return BlockExit::Dispatch;
}

BlockExit execute_mame_interpreter(State& state, Bus& bus, std::uint64_t stop_cycle) {
    static MameTMS34010 cpu;
    return cpu.step(state,bus,stop_cycle);
}

} // namespace mamerecomp::tms34010
