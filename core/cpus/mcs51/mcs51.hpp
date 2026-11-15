#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace multiarcaderecomp::mcs51 {
using u8 = std::uint8_t;
using s8 = std::int8_t;
using u16 = std::uint16_t;
using s16 = std::int16_t;
using u32 = std::uint32_t;

class Core {
public:
    struct Bus {
        std::function<u8(u16)> read_external;
        std::function<void(u16, u8)> write_external;
        std::function<void(unsigned)> interrupt_ack;
        u16 external_data_mask = 0xffff;
        std::array<std::function<u8()>, 4> read_port{};
        std::array<std::function<void(u8)>, 4> write_port{};
    };

    explicit Core(std::span<const u8> program);
    Core(std::span<const u8> program, Bus bus);
    void reset();
    void set_irq0(bool asserted);
    void set_irq1(bool asserted);
    u32 step(); // MAME-compatible classic 8051 machine cycles; one machine cycle is 12 oscillator clocks.
    u16 pc() const noexcept { return m_pc; }
    u8 interrupt_enable() const noexcept { return m_ie; }
    u8 timer_control() const noexcept { return m_tcon; }
    std::uint64_t clock_count() const noexcept { return m_clock_count; }

private:
    static constexpr int PSW_CY = 7, PSW_AC = 6, PSW_OV = 2;
    static constexpr int TCON_TF1 = 7, TCON_TR1 = 6, TCON_TF0 = 5, TCON_TR0 = 4;
    static constexpr int TCON_IE1 = 3, TCON_IT1 = 2, TCON_IE0 = 1, TCON_IT0 = 0;
    static constexpr int IE_A = 7, IE_T1 = 3, IE_X1 = 2, IE_T0 = 1, IE_X0 = 0;

    std::array<u8, 0x1000> m_rom{};
    std::array<u8, 0x80> m_internal_ram{};
    Bus m_bus;
    u16 m_pc = 0, m_ppc = 0, m_dptr = 0;
    u8 m_acc = 0, m_psw = 0, m_b = 0, m_sp = 7, m_pcon = 0;
    u8 m_tcon = 0, m_tmod = 0, m_scon = 0, m_sbuf = 0, m_ie = 0, m_ip = 0;
    u8 m_p0 = 0xff, m_p1 = 0xff, m_p2 = 0xff, m_p3 = 0xff;
    u8 m_tl0 = 0, m_tl1 = 0, m_th0 = 0, m_th1 = 0;
    u8 m_rwm = 0, m_last_op = 0, m_last_bit = 0, m_irq_delayed = 0;
    bool m_irq0_line = false;
    bool m_irq1_line = false;
    int m_inst_cycles = 0, m_cur_irq_prio = -1;
    u8 m_irq_active = 0;
    std::array<u8, 8> m_irq_prio{};
    u32 m_last_line_state = 0;
    std::uint64_t m_clock_count = 0;
    u8 m_t0_cnt = 0, m_t1_cnt = 0;

    u8 read_program(u32 address) const noexcept;
    u8 read_idata(u8 address) const noexcept;
    void write_idata(u8 address, u8 value) noexcept;
    u8 read_sfr(u8 address);
    void write_sfr(u8 address, u8 value);
    u8 read_xdata(u32 address);
    void write_xdata(u32 address, u8 value);
    u8 read_direct(u8 address);
    void write_direct(u8 address, u8 value);
    u32 external_ram_iaddr(u32 address, u32 mask) const noexcept;
    void acc_w(u8 value);
    void update_timer_t0(u32 cycles);
    void update_timer_t1(u32 cycles);
    void check_irqs();
    void clear_current_irq();
    u8 pending_irq_flags(u8 mask) const;
    void burn_cycles(u32 clocks);
    void update_irq_priority();
    void push_pc();
    void pop_pc();
    void set_cy(bool state);
    void set_ac(bool state);
    void set_ov(bool state);
    void set_tf0(bool state);
    void set_tf1(bool state);
    void set_ie0(bool state);
    void set_ie1(bool state);
    void set_it0(bool state);
    void set_it1(bool state);
    u8 port_read(unsigned port, u8 latch);
    void port_write(unsigned port, u8 value);
    static const u8 mcs51_cycles[256];
    static const u8 parity_value[256];
    u8 bit_address_r(u8);
    void bit_address_w(u8, u8);
    void set_reg(u8, u8);
    u8 r_reg(u8);
    void do_add_flags(u8, u8, u8);
    void do_sub_flags(u8, u8, u8);
    void acall(u8);
    void add_a_byte(u8);
    void add_a_mem(u8);
    void add_a_ir(u8);
    void add_a_r(u8);
    void addc_a_byte(u8);
    void addc_a_mem(u8);
    void addc_a_ir(u8);
    void addc_a_r(u8);
    void ajmp(u8);
    void anl_mem_a(u8);
    void anl_mem_byte(u8);
    void anl_a_byte(u8);
    void anl_a_mem(u8);
    void anl_a_ir(u8);
    void anl_a_r(u8);
    void anl_c_bitaddr(u8);
    void anl_c_nbitaddr(u8);
    void cjne_a_byte(u8);
    void cjne_a_mem(u8);
    void cjne_ir_byte(u8);
    void cjne_r_byte(u8);
    void clr_bitaddr(u8);
    void clr_c(u8);
    void clr_a(u8);
    void cpl_bitaddr(u8);
    void cpl_c(u8);
    void cpl_a(u8);
    void da_a(u8);
    void dec_a(u8);
    void dec_mem(u8);
    void dec_ir(u8);
    void dec_r(u8);
    void div_ab(u8);
    void djnz_mem(u8);
    void djnz_r(u8);
    void inc_a(u8);
    void inc_mem(u8);
    void inc_ir(u8);
    void inc_r(u8);
    void inc_dptr(u8);
    void jb(u8);
    void jbc(u8);
    void jc(u8);
    void jmp_iadptr(u8);
    void jnb(u8);
    void jnc(u8);
    void jnz(u8);
    void jz(u8);
    void lcall(u8);
    void ljmp(u8);
    void mov_a_byte(u8);
    void mov_a_mem(u8);
    void mov_a_ir(u8);
    void mov_a_r(u8);
    void mov_mem_byte(u8);
    void mov_mem_mem(u8);
    void mov_ir_byte(u8);
    void mov_r_byte(u8);
    void mov_mem_ir(u8);
    void mov_mem_r(u8);
    void mov_dptr_byte(u8);
    void mov_bitaddr_c(u8);
    void mov_ir_mem(u8);
    void mov_r_mem(u8);
    void mov_mem_a(u8);
    void mov_ir_a(u8);
    void mov_r_a(u8);
    void movc_a_iapc(u8);
    void mov_c_bitaddr(u8);
    void movc_a_iadptr(u8);
    void movx_a_idptr(u8);
    void movx_a_ir(u8);
    void movx_idptr_a(u8);
    void movx_ir_a(u8);
    void mul_ab(u8);
    void nop(u8);
    void orl_mem_a(u8);
    void orl_mem_byte(u8);
    void orl_a_byte(u8);
    void orl_a_mem(u8);
    void orl_a_ir(u8);
    void orl_a_r(u8);
    void orl_c_bitaddr(u8);
    void orl_c_nbitaddr(u8);
    void pop(u8);
    void push(u8);
    void ret(u8);
    void reti(u8);
    void rl_a(u8);
    void rlc_a(u8);
    void rr_a(u8);
    void rrc_a(u8);
    void setb_c(u8);
    void setb_bitaddr(u8);
    void sjmp(u8);
    void subb_a_byte(u8);
    void subb_a_mem(u8);
    void subb_a_ir(u8);
    void subb_a_r(u8);
    void swap_a(u8);
    void xch_a_mem(u8);
    void xch_a_ir(u8);
    void xch_a_r(u8);
    void xchd_a_ir(u8);
    void xrl_mem_a(u8);
    void xrl_mem_byte(u8);
    void xrl_a_byte(u8);
    void xrl_a_mem(u8);
    void xrl_a_ir(u8);
    void xrl_a_r(u8);
    void illegal(u8);
    void execute_op(u8);
};
} // namespace multiarcaderecomp::mcs51
