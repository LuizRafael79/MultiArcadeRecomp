#include "m68k.hpp"

#include <algorithm>
#include <deque>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace mamerecomp {
namespace {

std::uint16_t read_be16(std::span<const std::uint8_t> memory, std::uint32_t address) {
    if (address + 1 >= memory.size()) {
        throw std::out_of_range("instruction extension exceeds the main CPU ROM");
    }
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(memory[address]) << 8)
        | static_cast<std::uint16_t>(memory[address + 1]));
}

std::uint32_t read_be32(std::span<const std::uint8_t> memory, std::uint32_t address) {
    return (static_cast<std::uint32_t>(read_be16(memory, address)) << 16)
        | read_be16(memory, address + 2);
}

M68kInstruction unsupported(std::uint32_t pc, std::uint16_t opcode, std::string reason) {
    M68kInstruction result;
    result.op = M68kOp::Unsupported;
    result.address = pc;
    result.next_pc = pc;
    result.opcode = opcode;
    result.reason = std::move(reason);
    return result;
}

std::string condition_name(std::uint8_t condition) {
    static constexpr const char* names[] = {
        "BRA", "BSR", "BHI", "BLS", "BCC", "BCS", "BNE", "BEQ",
        "BVC", "BVS", "BPL", "BMI", "BGE", "BLT", "BGT", "BLE",
    };
    return names[condition & 0x0f];
}

std::string hex_value(std::uint32_t value, int width) {
    std::ostringstream stream;
    stream << "0x" << std::hex << std::setfill('0') << std::setw(width) << value;
    return stream.str();
}

void enqueue_if_in_range(std::deque<std::uint32_t>& pending, std::uint32_t address, std::size_t size) {
    if (address < size && (address & 1U) == 0) {
        pending.push_back(address);
    }
}

} // namespace

M68kInstruction decode_68000(std::span<const std::uint8_t> memory, std::uint32_t pc) {
    if ((pc & 1U) != 0 || pc + 1 >= memory.size()) {
        return unsupported(pc, 0, "unaligned or out-of-range instruction address");
    }

    const std::uint16_t opcode = read_be16(memory, pc);
    M68kInstruction result;
    result.address = pc;
    result.opcode = opcode;
    result.next_pc = pc + 2;

    if ((opcode & 0xf000U) == 0x6000U) {
        result.op = M68kOp::Branch;
        result.condition = static_cast<std::uint8_t>((opcode >> 8) & 0x0fU);
        const std::uint8_t byte_displacement = static_cast<std::uint8_t>(opcode & 0xffU);
        std::int32_t displacement = 0;
        if (byte_displacement == 0) {
            displacement = static_cast<std::int16_t>(read_be16(memory, pc + 2));
            result.next_pc = pc + 4;
        } else {
            displacement = static_cast<std::int8_t>(byte_displacement);
        }
        // 68000 branch displacement is relative to the extension/next-word address (PC + 2).
        result.target = static_cast<std::uint32_t>(static_cast<std::int64_t>(pc + 2) + displacement);
        if (result.condition == 1) {
            result.op = M68kOp::CallRelativeBranch;
        }
        return result;
    }

    if (opcode == 0x13fcU) { // MOVE.B #imm,abs.l
        if (pc + 7 >= memory.size()) {
            return unsupported(pc, opcode, "truncated MOVE.B immediate/absolute-long operands");
        }
        result.op = M68kOp::MoveByteImmediateAbsoluteLong;
        result.immediate = read_be16(memory, pc + 2) & 0xffU;
        result.absolute_address = read_be32(memory, pc + 4);
        result.next_pc = pc + 8;
        return result;
    }

    if (opcode == 0x11fcU) { // MOVE.B #imm,abs.w
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.B immediate/absolute-word operands");
        result.op = M68kOp::MoveByteImmediateAbsoluteWord;
        result.immediate = read_be16(memory, pc + 2) & 0xffU;
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 4))));
        result.next_pc = pc + 6;
        return result;
    }

    if (opcode == 0x31fcU) { // MOVE.W #imm,abs.w
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.W immediate/absolute-word operands");
        result.op = M68kOp::MoveWordImmediateAbsoluteWord;
        result.immediate = read_be16(memory, pc + 2);
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 4))));
        result.next_pc = pc + 6;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x3178U) { // MOVE.W #imm,d16(An)
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.W immediate/address-displacement operands");
        result.op = M68kOp::MoveWordImmediateAddressDisplacement;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.immediate = read_be16(memory, pc + 2);
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 4))));
        result.next_pc = pc + 6;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x3140U) { // MOVE.W Dn,d16(An)
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.W data-register/address-displacement operands");
        result.op = M68kOp::MoveWordDataAddressDisplacement;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x2148U) { // MOVE.L An,d16(Am)
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.L address-register/displacement operands");
        result.op = M68kOp::MoveLongAddressAddressDisplacement;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x1178U) { // MOVE.B #imm,d16(An)
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.B immediate/address-displacement operands");
        result.op = M68kOp::MoveByteImmediateAddressDisplacement;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.immediate = read_be16(memory, pc + 2) & 0xffU;
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 4))));
        result.next_pc = pc + 6;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x31c0U) { // MOVE.W Dn,abs.w
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.W data-register/absolute-word operands");
        result.op = M68kOp::MoveWordDataAbsoluteWord;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x21c0U) { // MOVE.L Dn,abs.w
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.L data-register/absolute-word operands");
        result.op = M68kOp::MoveLongDataAbsoluteWord;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x48a0U || (opcode & 0xfff8U) == 0x48e0U) { // MOVEM.W/L regs,-(An)
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVEM predecrement register mask");
        result.op = M68kOp::MovemPredecrement;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.operand_size = (opcode & 0x0040U) ? 1 : 0;
        result.register_mask = read_be16(memory, pc + 2);
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x4c98U || (opcode & 0xfff8U) == 0x4cd8U) { // MOVEM.W/L (An)+,regs
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVEM postincrement register mask");
        result.op = M68kOp::MovemPostincrement;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.operand_size = (opcode & 0x0040U) ? 1 : 0;
        result.register_mask = read_be16(memory, pc + 2);
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x5038U) { // ADDQ.B #n,abs.w
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated ADDQ.B absolute-word address");
        result.op = M68kOp::AddQuickByteAbsoluteWord;
        result.immediate = (opcode >> 9) & 7U;
        if (result.immediate == 0) result.immediate = 8;
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x5030U) { // ADDQ.B #n,(d8,An,Xn)
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated ADDQ.B indexed destination");
        const std::uint16_t index = read_be16(memory, pc + 2);
        if ((index & 0x0700U) != 0) return unsupported(pc, opcode, "68000 brief index extension has reserved bits set");
        result.op = M68kOp::AddQuickByteIndexed;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = (opcode >> 9) & 7U;
        if (result.immediate == 0) result.immediate = 8;
        result.index_reg = static_cast<std::uint8_t>((index >> 12) & 7U);
        result.index_is_address = (index & 0x8000U) != 0;
        result.operand_size = static_cast<std::uint8_t>((index >> 11) & 1U);
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(index & 0xffU)));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x5140U) { // SUBQ.W #n,Dn
        result.op = M68kOp::SubQuickWordToData;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = (opcode >> 9) & 7U;
        if (result.immediate == 0) result.immediate = 8;
        return result;
    }

    if (opcode == 0x0238U) { // ANDI.B #imm,abs.w
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated ANDI.B immediate/absolute-word operands");
        result.op = M68kOp::AndByteImmediateAbsoluteWord;
        result.immediate = read_be16(memory, pc + 2) & 0xffU;
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 4))));
        result.next_pc = pc + 6;
        return result;
    }

    if (opcode == 0x0c38U) { // CMPI.B #imm,abs.w
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated CMPI.B immediate/absolute-word operands");
        result.op = M68kOp::CompareByteImmediateAbsoluteWord;
        result.immediate = read_be16(memory, pc + 2) & 0xffU;
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 4))));
        result.next_pc = pc + 6;
        return result;
    }

    if (opcode == 0x21fcU) { // MOVE.L #imm,abs.w
        if (pc + 7 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.L immediate/absolute-word operands");
        result.op = M68kOp::MoveLongImmediateAbsoluteWord;
        result.immediate = read_be32(memory, pc + 2);
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 6))));
        result.next_pc = pc + 8;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x11c0U) { // MOVE.B Dn,abs.w
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.B data-register/absolute-word operands");
        result.op = M68kOp::MoveByteDataToAbsoluteWord;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x11f0U) { // MOVE.B (d8,An,Xn),abs.w
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.B indexed/absolute-word operands");
        const std::uint16_t index = read_be16(memory, pc + 2);
        if ((index & 0x0700U) != 0) return unsupported(pc, opcode, "68000 brief index extension has reserved bits set");
        result.op = M68kOp::MoveByteIndexedToAbsoluteWord;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.source_reg = static_cast<std::uint8_t>((index >> 12) & 7U);
        result.condition = static_cast<std::uint8_t>((index >> 15) & 1U); // 0=D, 1=A index
        result.operand_size = static_cast<std::uint8_t>((index >> 11) & 1U); // 0=word, 1=long
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(index & 0xffU)));
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 4))));
        result.next_pc = pc + 6;
        return result;
    }

    if (opcode == 0x1039U) { // MOVE.B abs.l,D0
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.B absolute-long source");
        result.op = M68kOp::MoveByteAbsoluteLongToData;
        result.reg = 0;
        result.absolute_address = read_be32(memory, pc + 2);
        result.next_pc = pc + 6;
        return result;
    }

    if (opcode == 0x4eb9U) { // JSR abs.l
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated JSR absolute-long target");
        result.op = M68kOp::CallAbsoluteLong;
        result.target = read_be32(memory, pc + 2);
        result.next_pc = pc + 6;
        return result;
    }

    if (opcode == 0x4ef9U) { // JMP abs.l
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated JMP absolute-long target");
        result.op = M68kOp::JumpAbsoluteLong;
        result.target = read_be32(memory, pc + 2);
        result.next_pc = pc + 6;
        return result;
    }

    if (opcode == 0x4e75U) { // RTS
        result.op = M68kOp::ReturnSubroutine;
        return result;
    }

    if ((opcode & 0xf1ffU) == 0x41f8U) { // LEA abs.w,An
        if (pc + 3 >= memory.size()) {
            return unsupported(pc, opcode, "truncated LEA absolute-word operand");
        }
        result.op = M68kOp::LeaAbsoluteWord;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1ffU) == 0x41f9U) { // LEA abs.l,An
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated LEA absolute-long operand");
        result.op = M68kOp::LeaAbsoluteLong;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.absolute_address = read_be32(memory, pc + 2);
        result.next_pc = pc + 6;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x41e8U) { // LEA d16(An),An
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated LEA address-register displacement");
        result.op = M68kOp::LeaAddressDisplacement;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x41d0U) { // LEA (An),An
        result.op = M68kOp::LeaAddressIndirect;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x41f0U) { // LEA (d8,An,Xn),An
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated LEA indexed operand");
        const std::uint16_t index = read_be16(memory, pc + 2);
        if ((index & 0x0700U) != 0) return unsupported(pc, opcode, "68000 brief index extension has reserved bits set");
        result.op = M68kOp::LeaIndexed;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.base_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.index_reg = static_cast<std::uint8_t>((index >> 12) & 7U);
        result.index_is_address = (index & 0x8000U) != 0;
        result.operand_size = static_cast<std::uint8_t>((index >> 11) & 1U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(index & 0xffU)));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1ffU) == 0x41faU) { // LEA d16(PC),An
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated LEA PC-relative displacement");
        result.op = M68kOp::LeaPcDisplacement;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        const auto displacement = static_cast<std::int16_t>(read_be16(memory, pc + 2));
        result.target = static_cast<std::uint32_t>(static_cast<std::int64_t>(pc + 2) + displacement);
        result.next_pc = pc + 4;
        return result;
    }

    if (opcode == 0x46fcU) { // MOVE.W #imm,SR
        if (pc + 3 >= memory.size()) {
            return unsupported(pc, opcode, "truncated MOVE immediate to SR operand");
        }
        result.op = M68kOp::MoveImmediateToStatus;
        result.immediate = read_be16(memory, pc + 2);
        result.next_pc = pc + 4;
        return result;
    }

    if (opcode == 0x44fcU) { // MOVE.W #imm,CCR
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE immediate to CCR operand");
        result.op = M68kOp::MoveImmediateToCondition;
        result.immediate = read_be16(memory, pc + 2);
        result.next_pc = pc + 4;
        return result;
    }

    if (opcode == 0x4a39U) { // TST.W abs.l
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated TST.W absolute-long operand");
        result.op = M68kOp::TestWordAbsoluteLong;
        result.absolute_address = read_be32(memory, pc + 2);
        result.next_pc = pc + 6;
        return result;
    }

    if (opcode == 0x4a38U) { // TST.B abs.w
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated TST.B absolute-word address");
        result.op = M68kOp::TestByteAbsoluteWord;
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if (opcode == 0x4238U) { // CLR.B abs.w
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated CLR.B absolute-word address");
        result.op = M68kOp::ClearByteAbsoluteWord;
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x4268U) { // CLR.W d16(An)
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated CLR.W address-displacement operand");
        result.op = M68kOp::ClearWordAddressDisplacement;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x4228U) { // CLR.B d16(An)
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated CLR.B address-displacement operand");
        result.op = M68kOp::ClearByteAddressDisplacement;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x1028U) { // MOVE.B d16(An),Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.B address-displacement source");
        result.op = M68kOp::MoveByteAddressDisplacementToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x1170U) { // MOVE.B (d8,An,Xn),d16(An)
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.B indexed/address-displacement operands");
        const std::uint16_t index = read_be16(memory, pc + 2);
        if ((index & 0x0700U) != 0) return unsupported(pc, opcode, "68000 brief index extension has reserved bits set");
        result.op = M68kOp::MoveByteIndexedToAddressDisplacement;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.base_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.index_reg = static_cast<std::uint8_t>((index >> 12) & 7U);
        result.index_is_address = (index & 0x8000U) != 0;
        result.operand_size = static_cast<std::uint8_t>((index >> 11) & 1U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(index & 0xffU)));
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 4))));
        result.next_pc = pc + 6;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x1030U) { // MOVE.B (d8,An,Xn),Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.B indexed source");
        const std::uint16_t index = read_be16(memory, pc + 2);
        if ((index & 0x0700U) != 0) return unsupported(pc, opcode, "68000 brief index extension has reserved bits set");
        result.op = M68kOp::MoveByteIndexedToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.base_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.index_reg = static_cast<std::uint8_t>((index >> 12) & 7U);
        result.index_is_address = (index & 0x8000U) != 0;
        result.operand_size = static_cast<std::uint8_t>((index >> 11) & 1U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(index & 0xffU)));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x3030U) { // MOVE.W (d8,An,Xn),Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.W indexed source");
        const std::uint16_t index = read_be16(memory, pc + 2);
        if ((index & 0x0700U) != 0) return unsupported(pc, opcode, "68000 brief index extension has reserved bits set");
        result.op = M68kOp::MoveWordIndexedToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.base_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.index_reg = static_cast<std::uint8_t>((index >> 12) & 7U);
        result.index_is_address = (index & 0x8000U) != 0;
        result.operand_size = static_cast<std::uint8_t>((index >> 11) & 1U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(index & 0xffU)));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x0c40U) { // CMPI.W #imm,Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated CMPI.W immediate operand");
        result.op = M68kOp::CompareWordImmediateToData;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = read_be16(memory, pc + 2);
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1ffU) == 0xc0fcU) { // AND.W #imm,Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated AND.W immediate source");
        result.op = M68kOp::AndWordImmediateSourceToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.immediate = read_be16(memory, pc + 2);
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x0000U) { // ORI.B #imm,Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated ORI.B immediate/data-register operands");
        result.op = M68kOp::OrByteImmediateToData;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = read_be16(memory, pc + 2) & 0xffU;
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1ffU) == 0x1038U) { // MOVE.B abs.w,Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.B absolute-word source");
        result.op = M68kOp::MoveByteAbsoluteWordToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1ffU) == 0xb038U) { // CMP.B abs.w,Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated CMP.B absolute-word source");
        result.op = M68kOp::CompareByteAbsoluteWordToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0xb068U) { // CMP.W d16(An),Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated CMP.W address-displacement source");
        result.op = M68kOp::CompareWordAddressDisplacementToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0xb050U) { // CMP.W (An),Dn
        result.op = M68kOp::CompareWordAddressIndirectToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x1140U) { // MOVE.B Dn,d16(An)
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.B data-register/address-displacement operands");
        result.op = M68kOp::MoveByteDataToAddressDisplacement;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x1180U) { // MOVE.B Dn,(d8,An,Xn)
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.B indexed destination");
        const std::uint16_t index = read_be16(memory, pc + 2);
        if ((index & 0x0700U) != 0) return unsupported(pc, opcode, "68000 brief index extension has reserved bits set");
        result.op = M68kOp::MoveByteDataToIndexed;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.index_reg = static_cast<std::uint8_t>((index >> 12) & 7U);
        result.index_is_address = (index & 0x8000U) != 0;
        result.operand_size = static_cast<std::uint8_t>((index >> 11) & 1U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(index & 0xffU)));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1ffU) == 0x11bcU) { // MOVE.B #imm,(d8,An,Xn)
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.B immediate/indexed destination");
        const std::uint16_t index = read_be16(memory, pc + 4);
        if ((index & 0x0700U) != 0) return unsupported(pc, opcode, "68000 brief index extension has reserved bits set");
        result.op = M68kOp::MoveByteImmediateToIndexed;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.immediate = read_be16(memory, pc + 2) & 0xffU;
        result.index_reg = static_cast<std::uint8_t>((index >> 12) & 7U);
        result.index_is_address = (index & 0x8000U) != 0;
        result.operand_size = static_cast<std::uint8_t>((index >> 11) & 1U);
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(index & 0xffU)));
        result.next_pc = pc + 6;
        return result;
    }

    if (opcode == 0x08b8U) { // BSET #imm,abs.w
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated BSET immediate/absolute-word operands");
        result.op = M68kOp::SetBitImmediateAbsoluteWord;
        result.immediate = read_be16(memory, pc + 2) & 7U;
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 4))));
        result.next_pc = pc + 6;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x0800U || (opcode & 0xfff8U) == 0x08c0U) { // BTST/BSET #imm,Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated immediate bit-operation operand");
        result.op = ((opcode & 0xfff8U) == 0x0800U) ? M68kOp::TestBitImmediateData : M68kOp::SetBitImmediateData;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = read_be16(memory, pc + 2) & 31U;
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x4a00U
        || (opcode & 0xfff8U) == 0x4a40U
        || (opcode & 0xfff8U) == 0x4a80U) { // TST.B/W/L Dn
        result.op = M68kOp::TestDataRegister;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.operand_size = static_cast<std::uint8_t>((opcode >> 6) & 3U);
        return result;
    }

    if ((opcode & 0xf1ffU) == 0x303cU) { // MOVE.W #imm,Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.W immediate operand");
        result.op = M68kOp::MoveWordImmediateToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.immediate = read_be16(memory, pc + 2);
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x3018U) { // MOVE.W (An)+,Dn
        result.op = M68kOp::MoveWordAddressPostIncrementToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x3028U) { // MOVE.W d16(An),Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.W address-displacement source");
        result.op = M68kOp::MoveWordAddressDisplacementToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x1018U) { // MOVE.B (An)+,Dn
        result.op = M68kOp::MoveByteAddressPostIncrementToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x3000U) { // MOVE.W Dn,Dn
        result.op = M68kOp::MoveWordDataToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x1000U) { // MOVE.B Dn,Dn
        result.op = M68kOp::MoveByteDataToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0xb140U) { // EOR.W Dn,Dn
        result.op = M68kOp::EorWordDataToData;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.source_reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x8040U) { // OR.W Dn,Dn
        result.op = M68kOp::OrWordDataToData;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.source_reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        return result;
    }

    if ((opcode & 0xfff8U) == 0x0a40U) { // EORI.W #imm,Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated EORI.W immediate operand");
        result.op = M68kOp::EorWordImmediateToData;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = read_be16(memory, pc + 2);
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x0200U) { // ANDI.B #imm,Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated ANDI.B immediate operand");
        result.op = M68kOp::AndByteImmediateToData;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = read_be16(memory, pc + 2) & 0xffU;
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x0240U) { // ANDI.W #imm,Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated ANDI.W immediate operand");
        result.op = M68kOp::AndWordImmediateToData;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = read_be16(memory, pc + 2);
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x0c00U) { // CMPI.B #imm,Dn
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated CMPI.B immediate operand");
        result.op = M68kOp::CompareByteImmediateToData;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = read_be16(memory, pc + 2) & 0xffU;
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0xd040U) { // ADD.W Dn,Dn
        result.op = M68kOp::AddWordDataToData;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.source_reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0xb040U) { // CMP.W Dn,Dn
        result.op = M68kOp::CompareWordDataToData;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.source_reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x9040U) { // SUB.W Dn,Dn
        result.op = M68kOp::SubWordDataToData;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.source_reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x20c0U) { // MOVE.L Dn,(An)+
        result.op = M68kOp::MoveLongDataPostIncrement;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.source_reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x20d8U) { // MOVE.L (An)+,(Am)+
        result.op = M68kOp::MoveLongAddressPostIncrementToAddressPostIncrement;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x30d8U) { // MOVE.W (An)+,(Am)+
        result.op = M68kOp::MoveWordAddressPostIncrementToAddressPostIncrement;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1ffU) == 0x21f8U) { // MOVE.L abs.w,abs.w
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.L absolute-word to absolute-word operands");
        result.op = M68kOp::MoveLongAbsoluteWordToAbsoluteWord;
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 4))));
        result.next_pc = pc + 6;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x2090U) { // MOVE.L (An),(Am)
        result.op = M68kOp::MoveLongAddressIndirectToAddressIndirect;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1ffU) == 0x20fcU) { // MOVE.L #imm,(An)+
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.L immediate/postincrement destination");
        result.op = M68kOp::MoveLongImmediateToAddressPostIncrement;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.immediate = read_be32(memory, pc + 2);
        result.next_pc = pc + 6;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x2178U) { // MOVE.L #imm,d16(An)
        if (pc + 7 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.L immediate/address-displacement operands");
        result.op = M68kOp::MoveLongImmediateAddressDisplacement;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.immediate = read_be32(memory, pc + 2);
        result.absolute_address = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 6))));
        result.next_pc = pc + 8;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x2080U) { // MOVE.L Dn,(An)
        result.op = M68kOp::MoveLongDataToAddressIndirect;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x2140U) { // MOVE.L Dn,d16(An)
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.L data-register/address-displacement operands");
        result.op = M68kOp::MoveLongDataAddressDisplacement;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x30b8U) { // MOVE.W #imm,(An)
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.W immediate/address-indirect operands");
        result.op = M68kOp::MoveWordImmediateAddressIndirect;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.immediate = read_be16(memory, pc + 2);
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1ffU) == 0x203cU) { // MOVE.L #imm,Dn
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.L immediate/data-register operands");
        result.op = M68kOp::MoveLongImmediateToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.immediate = read_be32(memory, pc + 2);
        result.next_pc = pc + 6;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x30c0U) { // MOVE.W Dn,(An)+
        result.op = M68kOp::MoveWordDataPostIncrement;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.source_reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x3080U) { // MOVE.W Dn,(An)
        result.op = M68kOp::MoveWordDataToAddressIndirect;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x3180U) { // MOVE.W Dn,(d8,An,Xn)
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVE.W indexed destination");
        const std::uint16_t index = read_be16(memory, pc + 2);
        if ((index & 0x0700U) != 0) return unsupported(pc, opcode, "68000 brief index extension has reserved bits set");
        result.op = M68kOp::MoveWordDataToIndexed;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.index_reg = static_cast<std::uint8_t>((index >> 12) & 7U);
        result.index_is_address = (index & 0x8000U) != 0;
        result.operand_size = static_cast<std::uint8_t>((index >> 11) & 1U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(index & 0xffU)));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0xe048U) { // LSR.W #n,Dn
        result.op = M68kOp::LogicalShiftRightWordImmediate;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = (opcode >> 9) & 7U;
        if (result.immediate == 0) result.immediate = 8;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0xe148U) { // LSL.W #n,Dn
        result.op = M68kOp::LogicalShiftLeftWordImmediate;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = (opcode >> 9) & 7U;
        if (result.immediate == 0) result.immediate = 8;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0xe058U) { // ROR.W #n,Dn
        result.op = M68kOp::RotateRightWordImmediate;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.immediate = (opcode >> 9) & 7U;
        if (result.immediate == 0) result.immediate = 8;
        return result;
    }

    if ((opcode & 0xfff8U) == 0x4600U || (opcode & 0xfff8U) == 0x4640U
        || (opcode & 0xfff8U) == 0x4680U) { // NOT.B/W/L Dn
        result.op = M68kOp::NotDataRegister;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.operand_size = static_cast<std::uint8_t>((opcode >> 6) & 3U);
        return result;
    }

    if ((opcode & 0xfff8U) == 0x4298U) { // CLR.L (An)+
        result.op = M68kOp::ClearLongAddressPostIncrement;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x8098U) { // OR.L Dn,(An)+
        result.op = M68kOp::OrLongDataAddressPostIncrement;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.source_reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        return result;
    }

    if ((opcode & 0xfff8U) == 0x51c8U) { // DBF Dn,disp.w
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated DBF displacement");
        result.op = M68kOp::DecrementBranchWord;
        result.reg = static_cast<std::uint8_t>(opcode & 7U);
        result.target = static_cast<std::uint32_t>(static_cast<std::int64_t>(pc + 2)
            + static_cast<std::int16_t>(read_be16(memory, pc + 2)));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf100U) == 0x7000U) { // MOVEQ #imm,Dn
        result.op = M68kOp::MoveQuick;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(opcode & 0xffU)));
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x2000U) { // MOVE.L Dn,Dn
        result.op = M68kOp::MoveLongDataToData;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x2040U) { // MOVEA.L Dn,An
        result.op = M68kOp::MoveLongDataToAddress;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x2058U) { // MOVEA.L (An)+,Am
        result.op = M68kOp::MoveLongPostIncrementToAddress;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x2070U) { // MOVEA.L (d8,An,Xn),Am
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVEA.L indexed source");
        const std::uint16_t index = read_be16(memory, pc + 2);
        if ((index & 0x0700U) != 0) return unsupported(pc, opcode, "68000 brief index extension has reserved bits set");
        result.op = M68kOp::MoveLongIndexedToAddress;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.base_reg = static_cast<std::uint8_t>(opcode & 7U);
        result.index_reg = static_cast<std::uint8_t>((index >> 12) & 7U);
        result.index_is_address = (index & 0x8000U) != 0;
        result.operand_size = static_cast<std::uint8_t>((index >> 11) & 1U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int8_t>(index & 0xffU)));
        result.next_pc = pc + 4;
        return result;
    }

    if ((opcode & 0xf1f8U) == 0x2048U) { // MOVEA.L An,An
        result.op = M68kOp::MoveLongAddressToAddress;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.source_reg = static_cast<std::uint8_t>(opcode & 7U);
        return result;
    }

    if ((opcode & 0xf1ffU) == 0x207cU) { // MOVEA.L #imm,An
        if (pc + 5 >= memory.size()) return unsupported(pc, opcode, "truncated MOVEA.L immediate operand");
        result.op = M68kOp::MoveLongImmediateToAddress;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.immediate = read_be32(memory, pc + 2);
        result.next_pc = pc + 6;
        return result;
    }

    if ((opcode & 0xf1ffU) == 0x307cU) { // MOVEA.W #imm,An
        if (pc + 3 >= memory.size()) return unsupported(pc, opcode, "truncated MOVEA.W immediate operand");
        result.op = M68kOp::MoveWordImmediateToAddress;
        result.reg = static_cast<std::uint8_t>((opcode >> 9) & 7U);
        result.immediate = static_cast<std::uint32_t>(static_cast<std::int32_t>(static_cast<std::int16_t>(read_be16(memory, pc + 2))));
        result.next_pc = pc + 4;
        return result;
    }

    if (opcode == 0x4e71U) {
        result.op = M68kOp::Nop;
        return result;
    }

    if (opcode == 0x4e72U) { // STOP #imm
        if (pc + 3 >= memory.size()) {
            return unsupported(pc, opcode, "truncated STOP status operand");
        }
        result.op = M68kOp::Stop;
        result.immediate = read_be16(memory, pc + 2);
        result.next_pc = pc + 4;
        return result;
    }

    return unsupported(pc, opcode, "68000 opcode is not implemented yet");
}

M68kTranslation translate_68000_blocks(std::span<const std::uint8_t> memory, std::uint32_t entry_pc,
    std::size_t max_blocks, std::size_t max_instructions_per_block) {
    M68kTranslation translation;
    std::deque<std::uint32_t> pending{entry_pc};
    std::set<std::uint32_t> visited;

    while (!pending.empty() && translation.blocks.size() < max_blocks) {
        const std::uint32_t start = pending.front();
        pending.pop_front();
        if (!visited.insert(start).second || start >= memory.size() || (start & 1U) != 0) {
            continue;
        }

        M68kBlock block;
        block.start = start;
        std::uint32_t pc = start;
        bool terminated = false;
        for (std::size_t count = 0; count < max_instructions_per_block; ++count) {
            M68kInstruction instruction;
            try {
                instruction = decode_68000(memory, pc);
            } catch (const std::out_of_range& error) {
                block.stop_pc = pc;
                block.stop_reason = error.what();
                ++translation.unsupported_blocks;
                terminated = true;
                break;
            }

            if (instruction.op == M68kOp::Unsupported) {
                block.stop_pc = pc;
                block.stop_reason = instruction.reason;
                ++translation.unsupported_blocks;
                terminated = true;
                break;
            }

            block.instructions.push_back(instruction);
            ++translation.translated_instructions;

            if (instruction.op == M68kOp::Branch) {
                enqueue_if_in_range(pending, instruction.target, memory.size());
                if (instruction.condition != 0) {
                    enqueue_if_in_range(pending, instruction.next_pc, memory.size());
                }
                block.stop_pc = instruction.next_pc;
                block.stop_reason = "branch terminates basic block";
                terminated = true;
                break;
            }

            if (instruction.op == M68kOp::DecrementBranchWord) {
                enqueue_if_in_range(pending, instruction.target, memory.size());
                enqueue_if_in_range(pending, instruction.next_pc, memory.size());
                block.stop_pc = instruction.next_pc;
                block.stop_reason = "DBF terminates basic block";
                terminated = true;
                break;
            }

            if (instruction.op == M68kOp::CallAbsoluteLong) {
                enqueue_if_in_range(pending, instruction.target, memory.size());
                enqueue_if_in_range(pending, instruction.next_pc, memory.size());
                block.stop_pc = instruction.next_pc;
                block.stop_reason = "subroutine call terminates basic block";
                terminated = true;
                break;
            }

            if (instruction.op == M68kOp::JumpAbsoluteLong) {
                enqueue_if_in_range(pending, instruction.target, memory.size());
                block.stop_pc = instruction.next_pc;
                block.stop_reason = "JMP terminates basic block";
                terminated = true;
                break;
            }

            if (instruction.op == M68kOp::CallRelativeBranch) {
                enqueue_if_in_range(pending, instruction.target, memory.size());
                enqueue_if_in_range(pending, instruction.next_pc, memory.size());
                block.stop_pc = instruction.next_pc;
                block.stop_reason = "BSR terminates basic block";
                terminated = true;
                break;
            }

            if (instruction.op == M68kOp::ReturnSubroutine) {
                block.stop_pc = instruction.next_pc;
                block.stop_reason = "RTS terminates basic block";
                terminated = true;
                break;
            }

            if (instruction.op == M68kOp::Stop) {
                block.stop_pc = instruction.next_pc;
                block.stop_reason = "STOP terminates basic block";
                terminated = true;
                break;
            }

            pc = instruction.next_pc;
        }

        if (!terminated) {
            block.stop_pc = pc;
            block.stop_reason = "basic-block instruction limit reached";
            ++translation.unsupported_blocks;
        }
        translation.blocks.push_back(std::move(block));
    }

    return translation;
}

std::string format_68000_instruction(const M68kInstruction& instruction) {
    std::ostringstream output;
    output << hex_value(instruction.address, 6) << "  " << hex_value(instruction.opcode, 4) << "  ";
    switch (instruction.op) {
    case M68kOp::Branch:
        output << condition_name(instruction.condition);
        output << (instruction.next_pc == instruction.address + 4 ? ".W " : ".S ") << hex_value(instruction.target, 6);
        break;
    case M68kOp::MoveByteImmediateAbsoluteLong:
        output << "MOVE.B #" << hex_value(instruction.immediate, 2) << "," << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::MoveByteImmediateAbsoluteWord:
        output << "MOVE.B #" << hex_value(instruction.immediate, 2) << "," << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::MoveWordImmediateAbsoluteWord:
        output << "MOVE.W #" << hex_value(instruction.immediate, 4) << "," << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::MoveByteAbsoluteLongToData:
        output << "MOVE.B " << hex_value(instruction.absolute_address, 8) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveByteDataToAbsoluteWord:
        output << "MOVE.B D" << static_cast<unsigned>(instruction.reg) << "," << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::MoveByteDataToData:
        output << "MOVE.B D" << static_cast<unsigned>(instruction.source_reg) << ",D"
               << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveByteIndexedToAbsoluteWord:
        output << "MOVE.B (" << static_cast<std::int32_t>(instruction.immediate) << ",A"
               << static_cast<unsigned>(instruction.reg) << "," << (instruction.condition ? "A" : "D")
               << static_cast<unsigned>(instruction.source_reg) << (instruction.operand_size ? ".L)" : ".W)")
               << "," << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::MoveLongImmediateAbsoluteWord:
        output << "MOVE.L #" << hex_value(instruction.immediate, 8) << "," << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::MoveLongDataPostIncrement:
        output << "MOVE.L D" << static_cast<unsigned>(instruction.source_reg) << ",(A"
               << static_cast<unsigned>(instruction.reg) << ")+";
        break;
    case M68kOp::ClearByteAbsoluteWord:
        output << "CLR.B " << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::SetBitImmediateAbsoluteWord:
        output << "BSET #" << instruction.immediate << "," << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::TestByteAbsoluteWord:
        output << "TST.B " << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::MoveWordDataAbsoluteWord:
        output << "MOVE.W D" << static_cast<unsigned>(instruction.reg) << "," << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::MoveLongDataAbsoluteWord:
        output << "MOVE.L D" << static_cast<unsigned>(instruction.reg) << "," << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::AddQuickByteAbsoluteWord:
        output << "ADDQ.B #" << instruction.immediate << "," << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::AndByteImmediateAbsoluteWord:
        output << "ANDI.B #" << hex_value(instruction.immediate, 2) << "," << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::CompareByteImmediateAbsoluteWord:
        output << "CMPI.B #" << hex_value(instruction.immediate, 2) << "," << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::MovemPredecrement:
    case M68kOp::MovemPostincrement:
        output << "MOVEM." << (instruction.operand_size ? "L " : "W ") << "mask="
               << hex_value(instruction.register_mask, 4) << (instruction.op == M68kOp::MovemPredecrement ? ",-(A" : ",(A")
               << static_cast<unsigned>(instruction.reg) << (instruction.op == M68kOp::MovemPostincrement ? ")+" : ")");
        break;
    case M68kOp::TestBitImmediateData:
    case M68kOp::SetBitImmediateData:
        output << (instruction.op == M68kOp::TestBitImmediateData ? "BTST #" : "BSET #")
               << instruction.immediate << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::OrWordDataToData:
        output << "OR.W D" << static_cast<unsigned>(instruction.source_reg) << ",D"
               << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveWordDataPostIncrement:
        output << "MOVE.W D" << static_cast<unsigned>(instruction.source_reg) << ",(A"
               << static_cast<unsigned>(instruction.reg) << ")+";
        break;
    case M68kOp::MoveWordImmediateAddressDisplacement:
        output << "MOVE.W #" << hex_value(instruction.immediate, 4) << ","
               << static_cast<std::int32_t>(instruction.absolute_address) << "(A" << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::MoveLongAddressAddressDisplacement:
        output << "MOVE.L A" << static_cast<unsigned>(instruction.source_reg) << ","
               << static_cast<std::int32_t>(instruction.immediate) << "(A" << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::MoveByteImmediateAddressDisplacement:
        output << "MOVE.B #" << hex_value(instruction.immediate, 2) << ","
               << static_cast<std::int32_t>(instruction.absolute_address) << "(A" << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::MoveWordDataAddressDisplacement:
        output << "MOVE.W D" << static_cast<unsigned>(instruction.source_reg) << ","
               << static_cast<std::int32_t>(instruction.immediate) << "(A" << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::SubWordDataToData:
        output << "SUB.W D" << static_cast<unsigned>(instruction.source_reg) << ",D"
               << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::ClearWordAddressDisplacement:
        output << "CLR.W " << static_cast<std::int32_t>(instruction.absolute_address) << "(A"
               << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::ClearByteAddressDisplacement:
        output << "CLR.B " << static_cast<std::int32_t>(instruction.absolute_address) << "(A"
               << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::MoveByteAddressDisplacementToData:
        output << "MOVE.B " << static_cast<std::int32_t>(instruction.immediate) << "(A"
               << static_cast<unsigned>(instruction.source_reg) << "),D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveByteIndexedToAddressDisplacement:
        output << "MOVE.B (" << static_cast<std::int32_t>(instruction.immediate) << ",A"
               << static_cast<unsigned>(instruction.base_reg) << ","
               << (instruction.index_is_address ? "A" : "D") << static_cast<unsigned>(instruction.index_reg)
               << (instruction.operand_size ? ".L" : ".W") << "),"
               << static_cast<std::int32_t>(instruction.absolute_address) << "(A"
               << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::MoveByteIndexedToData:
        output << "MOVE.B (" << static_cast<std::int32_t>(instruction.immediate) << ",A"
               << static_cast<unsigned>(instruction.base_reg) << ","
               << (instruction.index_is_address ? "A" : "D") << static_cast<unsigned>(instruction.index_reg)
               << (instruction.operand_size ? ".L" : ".W") << "),D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::CompareWordImmediateToData:
        output << "CMPI.W #" << hex_value(instruction.immediate, 4) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveByteAbsoluteWordToData:
        output << "MOVE.B " << hex_value(instruction.absolute_address, 8) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveByteDataToAddressDisplacement:
        output << "MOVE.B D" << static_cast<unsigned>(instruction.source_reg) << ","
               << static_cast<std::int32_t>(instruction.immediate) << "(A" << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::MoveByteDataToIndexed:
        output << "MOVE.B D" << static_cast<unsigned>(instruction.source_reg) << ",("
               << static_cast<std::int32_t>(instruction.immediate) << ",A" << static_cast<unsigned>(instruction.reg)
               << "," << (instruction.index_is_address ? "A" : "D") << static_cast<unsigned>(instruction.index_reg)
               << (instruction.operand_size ? ".L)" : ".W)");
        break;
    case M68kOp::LogicalShiftLeftWordImmediate:
        output << "LSL.W #" << instruction.immediate << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveWordIndexedToData:
        output << "MOVE.W (" << static_cast<std::int32_t>(instruction.immediate) << ",A"
               << static_cast<unsigned>(instruction.base_reg) << ","
               << (instruction.index_is_address ? "A" : "D") << static_cast<unsigned>(instruction.index_reg)
               << (instruction.operand_size ? ".L" : ".W") << "),D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::CompareWordDataToData:
        output << "CMP.W D" << static_cast<unsigned>(instruction.source_reg) << ",D"
               << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveByteImmediateToIndexed:
        output << "MOVE.B #" << hex_value(instruction.immediate, 2) << ",("
               << static_cast<std::int32_t>(instruction.absolute_address) << ",A" << static_cast<unsigned>(instruction.reg)
               << "," << (instruction.index_is_address ? "A" : "D") << static_cast<unsigned>(instruction.index_reg)
               << (instruction.operand_size ? ".L)" : ".W)");
        break;
    case M68kOp::MoveWordDataToAddressIndirect:
        output << "MOVE.W D" << static_cast<unsigned>(instruction.source_reg) << ",(A"
               << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::LeaIndexed:
        output << "LEA (" << static_cast<std::int32_t>(instruction.immediate) << ",A"
               << static_cast<unsigned>(instruction.base_reg) << ","
               << (instruction.index_is_address ? "A" : "D") << static_cast<unsigned>(instruction.index_reg)
               << (instruction.operand_size ? ".L),A" : ".W),A") << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::LeaAddressIndirect:
        output << "LEA (A" << static_cast<unsigned>(instruction.source_reg) << "),A"
               << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveWordDataToIndexed:
        output << "MOVE.W D" << static_cast<unsigned>(instruction.source_reg) << ",("
               << static_cast<std::int32_t>(instruction.immediate) << ",A" << static_cast<unsigned>(instruction.reg)
               << "," << (instruction.index_is_address ? "A" : "D") << static_cast<unsigned>(instruction.index_reg)
               << (instruction.operand_size ? ".L)" : ".W)");
        break;
    case M68kOp::AddQuickByteIndexed:
        output << "ADDQ.B #" << instruction.immediate << ",("
               << static_cast<std::int32_t>(instruction.absolute_address) << ",A" << static_cast<unsigned>(instruction.reg)
               << "," << (instruction.index_is_address ? "A" : "D") << static_cast<unsigned>(instruction.index_reg)
               << (instruction.operand_size ? ".L)" : ".W)");
        break;
    case M68kOp::CompareByteAbsoluteWordToData:
        output << "CMP.B " << hex_value(instruction.absolute_address, 8) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::CompareWordAddressDisplacementToData:
        output << "CMP.W " << static_cast<std::int32_t>(instruction.immediate) << "(A"
               << static_cast<unsigned>(instruction.source_reg) << "),D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::CompareWordAddressIndirectToData:
        output << "CMP.W (A" << static_cast<unsigned>(instruction.source_reg) << "),D"
               << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveLongAddressPostIncrementToAddressPostIncrement:
        output << "MOVE.L (A" << static_cast<unsigned>(instruction.source_reg) << ")+,(A"
               << static_cast<unsigned>(instruction.reg) << ")+";
        break;
    case M68kOp::MoveLongAddressIndirectToAddressIndirect:
        output << "MOVE.L (A" << static_cast<unsigned>(instruction.source_reg) << "),(A"
               << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::MoveLongImmediateToAddressPostIncrement:
        output << "MOVE.L #" << hex_value(instruction.immediate, 8) << ",(A"
               << static_cast<unsigned>(instruction.reg) << ")+";
        break;
    case M68kOp::MoveLongImmediateAddressDisplacement:
        output << "MOVE.L #" << hex_value(instruction.immediate, 8) << ","
               << static_cast<std::int32_t>(instruction.absolute_address) << "(A"
               << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::MoveWordAddressPostIncrementToAddressPostIncrement:
        output << "MOVE.W (A" << static_cast<unsigned>(instruction.source_reg) << ")+,(A"
               << static_cast<unsigned>(instruction.reg) << ")+";
        break;
    case M68kOp::MoveLongAbsoluteWordToAbsoluteWord:
        output << "MOVE.L " << hex_value(instruction.absolute_address, 8) << "," << hex_value(instruction.immediate, 8);
        break;
    case M68kOp::AndWordImmediateSourceToData:
        output << "AND.W #" << hex_value(instruction.immediate, 4) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveLongIndexedToAddress:
        output << "MOVEA.L (" << static_cast<std::int32_t>(instruction.immediate) << ",A"
               << static_cast<unsigned>(instruction.base_reg) << ","
               << (instruction.index_is_address ? "A" : "D") << static_cast<unsigned>(instruction.index_reg)
               << (instruction.operand_size ? ".L),A" : ".W),A") << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::SubQuickWordToData:
        output << "SUBQ.W #" << instruction.immediate << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::OrByteImmediateToData:
        output << "ORI.B #" << hex_value(instruction.immediate, 2) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveLongPostIncrementToAddress:
        output << "MOVEA.L (A" << static_cast<unsigned>(instruction.source_reg) << ")+ ,A"
               << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveWordAddressDisplacementToData:
        output << "MOVE.W " << static_cast<std::int32_t>(instruction.immediate) << "(A"
               << static_cast<unsigned>(instruction.source_reg) << "),D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveLongImmediateToAddress:
        output << "MOVEA.L #" << hex_value(instruction.immediate, 8) << ",A" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveLongDataToAddressIndirect:
        output << "MOVE.L D" << static_cast<unsigned>(instruction.source_reg) << ",(A"
               << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::MoveLongDataAddressDisplacement:
        output << "MOVE.L D" << static_cast<unsigned>(instruction.source_reg) << ","
               << static_cast<std::int32_t>(instruction.immediate) << "(A" << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::MoveWordImmediateAddressIndirect:
        output << "MOVE.W #" << hex_value(instruction.immediate, 4) << ",(A"
               << static_cast<unsigned>(instruction.reg) << ")";
        break;
    case M68kOp::MoveLongImmediateToData:
        output << "MOVE.L #" << hex_value(instruction.immediate, 8) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveWordImmediateToAddress:
        output << "MOVEA.W #" << hex_value(instruction.immediate, 8) << ",A" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::NotDataRegister:
        output << "NOT." << "BWL"[instruction.operand_size] << " D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::AndByteImmediateToData:
        output << "ANDI.B #" << hex_value(instruction.immediate, 2) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::AndWordImmediateToData:
        output << "ANDI.W #" << hex_value(instruction.immediate, 4) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::CompareByteImmediateToData:
        output << "CMPI.B #" << hex_value(instruction.immediate, 2) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::AddWordDataToData:
        output << "ADD.W D" << static_cast<unsigned>(instruction.source_reg) << ",D"
               << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::TestDataRegister:
        output << "TST." << "BWL"[instruction.operand_size] << " D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::LeaAbsoluteWord:
        output << "LEA " << hex_value(instruction.immediate, 8) << ",A" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::LeaAbsoluteLong:
        output << "LEA " << hex_value(instruction.absolute_address, 8) << ",A" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::LeaAddressDisplacement:
        output << "LEA " << static_cast<std::int32_t>(instruction.immediate) << "(A"
               << static_cast<unsigned>(instruction.source_reg) << "),A" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::LeaPcDisplacement:
        output << "LEA " << hex_value(instruction.target, 8) << "(PC),A" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveImmediateToStatus:
        output << "MOVE #" << hex_value(instruction.immediate, 4) << ",SR";
        break;
    case M68kOp::MoveImmediateToCondition:
        output << "MOVE #" << hex_value(instruction.immediate, 4) << ",CCR";
        break;
    case M68kOp::MoveQuick:
        output << "MOVEQ #" << static_cast<std::int32_t>(instruction.immediate) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveLongDataToData:
        output << "MOVE.L D" << static_cast<unsigned>(instruction.source_reg) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveLongDataToAddress:
        output << "MOVEA.L D" << static_cast<unsigned>(instruction.source_reg) << ",A" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveLongAddressToAddress:
        output << "MOVEA.L A" << static_cast<unsigned>(instruction.source_reg) << ",A"
               << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::TestWordAbsoluteLong:
        output << "TST.W " << hex_value(instruction.absolute_address, 8);
        break;
    case M68kOp::MoveWordImmediateToData:
        output << "MOVE.W #" << hex_value(instruction.immediate, 4) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveWordAddressPostIncrementToData:
        output << "MOVE.W (A" << static_cast<unsigned>(instruction.source_reg) << ")+ ,D"
               << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveByteAddressPostIncrementToData:
        output << "MOVE.B (A" << static_cast<unsigned>(instruction.source_reg) << ")+ ,D"
               << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::MoveWordDataToData:
        output << "MOVE.W D" << static_cast<unsigned>(instruction.source_reg) << ",D"
               << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::EorWordDataToData:
        output << "EOR.W D" << static_cast<unsigned>(instruction.source_reg) << ",D"
               << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::EorWordImmediateToData:
        output << "EORI.W #" << hex_value(instruction.immediate, 4) << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::LogicalShiftRightWordImmediate:
        output << "LSR.W #" << instruction.immediate << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::RotateRightWordImmediate:
        output << "ROR.W #" << instruction.immediate << ",D" << static_cast<unsigned>(instruction.reg);
        break;
    case M68kOp::ClearLongAddressPostIncrement:
        output << "CLR.L (A" << static_cast<unsigned>(instruction.reg) << ")+";
        break;
    case M68kOp::OrLongDataAddressPostIncrement:
        output << "OR.L D" << static_cast<unsigned>(instruction.source_reg) << ",(A"
               << static_cast<unsigned>(instruction.reg) << ")+";
        break;
    case M68kOp::DecrementBranchWord:
        output << "DBF D" << static_cast<unsigned>(instruction.reg) << "," << hex_value(instruction.target, 6);
        break;
    case M68kOp::CallAbsoluteLong:
        output << "JSR " << hex_value(instruction.target, 8);
        break;
    case M68kOp::JumpAbsoluteLong:
        output << "JMP " << hex_value(instruction.target, 8);
        break;
    case M68kOp::CallRelativeBranch:
        output << "BSR" << (instruction.next_pc == instruction.address + 4 ? ".W " : ".S ") << hex_value(instruction.target, 6);
        break;
    case M68kOp::ReturnSubroutine:
        output << "RTS";
        break;
    case M68kOp::Nop:
        output << "NOP";
        break;
    case M68kOp::Stop:
        output << "STOP #" << hex_value(instruction.immediate, 4);
        break;
    case M68kOp::Unsupported:
        output << "DC.W " << hex_value(instruction.opcode, 4) << " ; " << instruction.reason;
        break;
    }
    return output.str();
}

std::string emit_c_translation(const M68kTranslation& translation, std::uint32_t entry_pc) {
    std::ostringstream output;
    output << "/* Generated by MameRecomp. Partial translation; unsupported instructions return false. */\n"
           << "#include <stdbool.h>\n#include <stdint.h>\n\n"
           << "typedef struct { uint32_t d[8], a[8], pc; uint16_t sr; bool stopped; } M68kState;\n"
           << "typedef struct { void *opaque; bool (*write8)(void *, uint32_t, uint8_t); bool (*write16)(void *, uint32_t, uint16_t); bool (*read8)(void *, uint32_t, uint8_t *); bool (*read16)(void *, uint32_t, uint16_t *); } M68kBus;\n\n"
           << "static void m68k_set_nz32(M68kState *s, uint32_t value) {\n"
           << "    s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
           << "    if (value == 0) s->sr |= 0x0004U;\n"
           << "    if (value & 0x80000000U) s->sr |= 0x0008U;\n}\n\n"
           << "static void m68k_set_nz_sized(M68kState *s, uint32_t value, uint32_t mask, uint32_t sign) {\n"
           << "    s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
           << "    if ((value & mask) == 0) s->sr |= 0x0004U;\n"
           << "    if (value & sign) s->sr |= 0x0008U;\n}\n\n"
           << "bool m68k_condition(const M68kState *s, unsigned condition) {\n"
           << "    const bool c = (s->sr & 1U) != 0, v = (s->sr & 2U) != 0;\n"
           << "    const bool z = (s->sr & 4U) != 0, n = (s->sr & 8U) != 0;\n"
           << "    switch (condition) {\n"
           << "    case 0: return true; case 2: return !c && !z; case 3: return c || z;\n"
           << "    case 4: return !c; case 5: return c; case 6: return !z; case 7: return z;\n"
           << "    case 8: return !v; case 9: return v; case 10: return !n; case 11: return n;\n"
           << "    case 12: return n == v; case 13: return n != v;\n"
           << "    case 14: return !z && (n == v); case 15: return z || (n != v);\n"
           << "    default: return false; }\n}\n\n";

    for (const auto& block : translation.blocks) {
        output << "bool block_" << std::hex << block.start << "(M68kState *s, const M68kBus *bus) {\n"
               << "    if (!s) return false;\n    (void)bus;\n";
        if (block.instructions.empty()) {
            output << "    s->pc = 0x" << block.stop_pc << "U; /* " << block.stop_reason << " */\n"
                   << "    return false;\n}\n\n";
            continue;
        }

        for (const auto& instruction : block.instructions) {
            switch (instruction.op) {
            case M68kOp::Branch:
                if (instruction.condition == 0) {
                    output << "    s->pc = 0x" << instruction.target << "U;\n    return true;\n";
                } else {
                    output << "    s->pc = m68k_condition(s, " << std::dec << static_cast<unsigned>(instruction.condition)
                           << ") ? 0x" << std::hex << instruction.target << "U : 0x" << instruction.next_pc << "U;\n"
                           << "    return true;\n";
                }
                break;
            case M68kOp::MoveByteImmediateAbsoluteLong:
                output << "    if (!bus || !bus->write8 || !bus->write8(bus->opaque, 0x" << std::hex << instruction.absolute_address << "U, 0x"
                       << instruction.immediate << "U)) return false;\n"
                       << "    s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
                       << "    if (" << std::dec << instruction.immediate << "U == 0) s->sr |= 0x0004U;\n"
                       << "    if (" << instruction.immediate << "U & 0x80U) s->sr |= 0x0008U;\n";
                break;
            case M68kOp::MoveByteImmediateAbsoluteWord:
                output << "    if (!bus || !bus->write8 || !bus->write8(bus->opaque, 0x" << std::hex << instruction.absolute_address
                       << "U, 0x" << instruction.immediate << "U)) return false;\n"
                       << "    s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
                       << "    if (" << std::dec << instruction.immediate << "U == 0) s->sr |= 0x0004U;\n"
                       << "    if (" << instruction.immediate << "U & 0x80U) s->sr |= 0x0008U;\n";
                break;
            case M68kOp::MoveWordImmediateAbsoluteWord:
                output << "    if (!bus || !bus->write16 || !bus->write16(bus->opaque, 0x"
                       << std::hex << instruction.absolute_address << "U, 0x" << instruction.immediate << "U)) return false;\n"
                       << "    m68k_set_nz_sized(s, 0x" << instruction.immediate << "U, 0xffffU, 0x8000U);\n";
                break;
            case M68kOp::MoveByteAbsoluteLongToData:
                output << "    { uint8_t value; if (!bus || !bus->read8 || !bus->read8(bus->opaque, 0x"
                       << std::hex << instruction.absolute_address << "U, &value)) return false;\n"
                       << "      s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffffff00U) | value;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffU, 0x80U); }\n";
                break;
            case M68kOp::MoveByteDataToAbsoluteWord:
                output << "    { uint8_t value = (uint8_t)s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; if (!bus || !bus->write8 || !bus->write8(bus->opaque, 0x" << std::hex
                       << instruction.absolute_address << "U, value)) return false;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffU, 0x80U); }\n";
                break;
            case M68kOp::MoveByteDataToData:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffffff00U) | (s->d["
                       << static_cast<unsigned>(instruction.source_reg) << "] & 0xffU);\n"
                       << "    m68k_set_nz_sized(s, s->d[" << static_cast<unsigned>(instruction.reg)
                       << "], 0xffU, 0x80U);\n";
                break;
            case M68kOp::MoveByteIndexedToAbsoluteWord:
                output << "    { uint32_t index = s->" << (instruction.condition ? "a" : "d") << "["
                       << std::dec << static_cast<unsigned>(instruction.source_reg) << "];\n"
                       << "      if (!" << (instruction.operand_size ? "true" : "false") << ") index = (uint32_t)(int32_t)(int16_t)index;\n"
                       << "      uint32_t address = s->a[" << static_cast<unsigned>(instruction.reg) << "] + index + (uint32_t)0x"
                       << std::hex << instruction.immediate << "U; uint8_t value;\n"
                       << "      if (!bus || !bus->read8 || !bus->write8 || !bus->read8(bus->opaque, address, &value)\n"
                       << "          || !bus->write8(bus->opaque, 0x" << instruction.absolute_address << "U, value)) return false;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffU, 0x80U); }\n";
                break;
            case M68kOp::MoveLongImmediateAbsoluteWord:
                output << "    if (!bus || !bus->write16 || !bus->write16(bus->opaque, 0x" << std::hex
                       << instruction.absolute_address << "U, (uint16_t)(0x" << instruction.immediate
                       << "U >> 16)) || !bus->write16(bus->opaque, 0x" << instruction.absolute_address
                       << "U + 2U, (uint16_t)0x" << instruction.immediate << "U)) return false;\n"
                       << "    m68k_set_nz32(s, 0x" << instruction.immediate << "U);\n";
                break;
            case M68kOp::MoveLongDataPostIncrement:
                output << "    { uint32_t value = s->d[" << std::dec << static_cast<unsigned>(instruction.source_reg)
                       << "]; uint32_t address = s->a[" << static_cast<unsigned>(instruction.reg) << "];\n"
                       << "      if (!bus || !bus->write16 || !bus->write16(bus->opaque, address, (uint16_t)(value >> 16))\n"
                       << "          || !bus->write16(bus->opaque, address + 2U, (uint16_t)value)) return false;\n"
                       << "      s->a[" << static_cast<unsigned>(instruction.reg) << "] += 4U; m68k_set_nz32(s, value); }\n";
                break;
            case M68kOp::ClearByteAbsoluteWord:
                output << "    if (!bus || !bus->write8 || !bus->write8(bus->opaque, 0x"
                       << std::hex << instruction.absolute_address << "U, 0)) return false;\n"
                       << "    s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU) | 0x0004U;\n";
                break;
            case M68kOp::SetBitImmediateAbsoluteWord:
                output << "    { uint8_t value; if (!bus || !bus->read8 || !bus->write8 || !bus->read8(bus->opaque, 0x"
                       << std::hex << instruction.absolute_address << "U, &value)) return false;\n"
                       << "      if ((value & (1U << " << std::dec << instruction.immediate << "U)) == 0) s->sr |= 0x0004U;\n"
                       << "      else s->sr = (uint16_t)(s->sr & (uint16_t)~0x0004U);\n"
                       << "      if (!bus->write8(bus->opaque, 0x" << std::hex << instruction.absolute_address
                       << "U, (uint8_t)(value | (1U << " << std::dec << instruction.immediate << "U)))) return false; }\n";
                break;
            case M68kOp::TestByteAbsoluteWord:
                output << "    { uint8_t value; if (!bus || !bus->read8 || !bus->read8(bus->opaque, 0x"
                       << std::hex << instruction.absolute_address << "U, &value)) return false;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffU, 0x80U); }\n";
                break;
            case M68kOp::MoveWordDataAbsoluteWord:
                output << "    if (!bus || !bus->write16 || !bus->write16(bus->opaque, 0x"
                       << std::hex << instruction.absolute_address << "U, (uint16_t)s->d["
                       << std::dec << static_cast<unsigned>(instruction.reg) << "])) return false;\n"
                       << "    m68k_set_nz_sized(s, s->d[" << static_cast<unsigned>(instruction.reg)
                       << "], 0xffffU, 0x8000U);\n";
                break;
            case M68kOp::MoveLongDataAbsoluteWord:
                output << "    if (!bus || !bus->write16 || !bus->write16(bus->opaque, 0x" << std::hex
                       << instruction.absolute_address << "U, (uint16_t)(s->d[" << std::dec
                       << static_cast<unsigned>(instruction.reg) << "] >> 16)) || !bus->write16(bus->opaque, 0x"
                       << std::hex << instruction.absolute_address << "U + 2U, (uint16_t)s->d["
                       << std::dec << static_cast<unsigned>(instruction.reg) << "])) return false;\n"
                       << "    m68k_set_nz32(s, s->d[" << static_cast<unsigned>(instruction.reg) << "]);\n";
                break;
            case M68kOp::AddQuickByteAbsoluteWord:
                output << "    { uint8_t lhs; uint16_t wide; uint8_t value; if (!bus || !bus->read8 || !bus->write8\n"
                       << "          || !bus->read8(bus->opaque, 0x" << std::hex << instruction.absolute_address << "U, &lhs)) return false;\n"
                       << "      wide = (uint16_t)lhs + " << std::dec << instruction.immediate << "U; value = (uint8_t)wide;\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x001fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x80U) s->sr |= 0x0008U;\n"
                       << "      if (wide & 0x100U) s->sr |= 0x0011U;\n"
                       << "      if (((~(lhs ^ " << instruction.immediate << "U)) & (lhs ^ value) & 0x80U) != 0) s->sr |= 0x0002U;\n"
                       << "      if (!bus->write8(bus->opaque, 0x" << std::hex << instruction.absolute_address << "U, value)) return false; }\n";
                break;
            case M68kOp::AndByteImmediateAbsoluteWord:
                output << "    { uint8_t value; if (!bus || !bus->read8 || !bus->write8 || !bus->read8(bus->opaque, 0x"
                       << std::hex << instruction.absolute_address << "U, &value)) return false;\n"
                       << "      value &= 0x" << instruction.immediate << "U; m68k_set_nz_sized(s, value, 0xffU, 0x80U);\n"
                       << "      if (!bus->write8(bus->opaque, 0x" << instruction.absolute_address << "U, value)) return false; }\n";
                break;
            case M68kOp::CompareByteImmediateAbsoluteWord:
                output << "    { uint8_t lhs; uint8_t rhs = 0x" << std::hex << instruction.immediate
                       << "U; uint8_t value; if (!bus || !bus->read8 || !bus->read8(bus->opaque, 0x"
                       << instruction.absolute_address << "U, &lhs)) return false; value = (uint8_t)(lhs - rhs);\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x80U) s->sr |= 0x0008U;\n"
                       << "      if (lhs < rhs) s->sr |= 0x0001U;\n"
                       << "      if (((lhs ^ rhs) & (lhs ^ value) & 0x80U) != 0) s->sr |= 0x0002U; }\n";
                break;
            case M68kOp::MovemPredecrement:
                output << "    { uint32_t sp = s->a[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; for (int bit = 0; bit < 16; ++bit) { if ((0x" << std::hex << instruction.register_mask
                       << "U & (1U << bit)) != 0) { int reg = bit ^ 15; uint32_t value = (reg < 8) ? s->d[reg] : s->a[reg - 8];\n"
                       << "          sp -= " << (instruction.operand_size ? "4U" : "2U") << ";\n"
                       << "          if (!bus || !bus->write16 || !bus->write16(bus->opaque, sp, "
                       << (instruction.operand_size ? "(uint16_t)(value >> 16)" : "(uint16_t)value") << ")) return false;\n";
                if (instruction.operand_size) {
                    output << "          if (!bus->write16(bus->opaque, sp + 2U, (uint16_t)value)) return false;\n";
                }
                output << "      } } s->a[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = sp; }\n";
                break;
            case M68kOp::MovemPostincrement:
                output << "    { uint32_t address = s->a[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; for (int reg = 0; reg < 16; ++reg) { if ((0x" << std::hex << instruction.register_mask
                       << "U & (1U << reg)) != 0) { uint16_t hi; uint32_t value;\n"
                       << "          if (!bus || !bus->read16 || !bus->read16(bus->opaque, address, &hi)) return false;\n";
                if (instruction.operand_size) {
                    output << "          { uint16_t lo; if (!bus->read16(bus->opaque, address + 2U, &lo)) return false; value = ((uint32_t)hi << 16) | lo; } address += 4U;\n";
                } else {
                    output << "          value = (uint32_t)(int32_t)(int16_t)hi; address += 2U;\n";
                }
                output << "          if (reg < 8) s->d[reg] = value; else s->a[reg - 8] = value;\n"
                       << "      } } s->a[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = address; }\n";
                break;
            case M68kOp::TestBitImmediateData:
            case M68kOp::SetBitImmediateData: {
                const bool is_set = instruction.op == M68kOp::SetBitImmediateData;
                output << "    { uint32_t bit = 1U << " << std::dec << instruction.immediate << "U;\n"
                       << "      if ((s->d[" << static_cast<unsigned>(instruction.reg) << "] & bit) == 0) s->sr |= 0x0004U;\n"
                       << "      else s->sr = (uint16_t)(s->sr & (uint16_t)~0x0004U);\n";
                if (is_set) {
                    output << "      s->d[" << static_cast<unsigned>(instruction.reg) << "] |= bit;\n";
                }
                output << "    }\n";
                break;
            }
            case M68kOp::OrWordDataToData:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | ((s->d["
                       << static_cast<unsigned>(instruction.reg) << "] | s->d["
                       << static_cast<unsigned>(instruction.source_reg) << "]) & 0xffffU);\n"
                       << "    m68k_set_nz_sized(s, s->d[" << static_cast<unsigned>(instruction.reg)
                       << "], 0xffffU, 0x8000U);\n";
                break;
            case M68kOp::MoveWordDataPostIncrement:
                output << "    { uint16_t value = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.source_reg)
                       << "]; uint32_t address = s->a[" << static_cast<unsigned>(instruction.reg) << "];\n"
                       << "      if (!bus || !bus->write16 || !bus->write16(bus->opaque, address, value)) return false;\n"
                       << "      s->a[" << static_cast<unsigned>(instruction.reg) << "] += 2U;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffffU, 0x8000U); }\n";
                break;
            case M68kOp::MoveWordImmediateAddressDisplacement:
                output << "    if (!bus || !bus->write16 || !bus->write16(bus->opaque, s->a["
                       << std::dec << static_cast<unsigned>(instruction.reg) << "] + (uint32_t)0x"
                       << std::hex << instruction.absolute_address << "U, 0x" << instruction.immediate << "U)) return false;\n"
                       << "    m68k_set_nz_sized(s, 0x" << std::hex << instruction.immediate << "U, 0xffffU, 0x8000U);\n";
                break;
            case M68kOp::MoveLongAddressAddressDisplacement:
                output << "    { uint32_t value = s->a[" << std::dec << static_cast<unsigned>(instruction.source_reg)
                       << "]; uint32_t address = s->a[" << static_cast<unsigned>(instruction.reg)
                       << "] + (uint32_t)0x" << std::hex << instruction.immediate << "U;\n"
                       << "      if (!bus || !bus->write16 || !bus->write16(bus->opaque, address, (uint16_t)(value >> 16))\n"
                       << "          || !bus->write16(bus->opaque, address + 2U, (uint16_t)value)) return false;\n"
                       << "      m68k_set_nz32(s, value); }\n";
                break;
            case M68kOp::MoveByteImmediateAddressDisplacement:
                output << "    { uint8_t value = 0x" << std::hex << instruction.immediate << "U;\n"
                       << "      if (!bus || !bus->write8 || !bus->write8(bus->opaque, s->a[" << std::dec
                       << static_cast<unsigned>(instruction.reg) << "] + (uint32_t)0x" << std::hex
                       << instruction.absolute_address << "U, value)) return false;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffU, 0x80U); }\n";
                break;
            case M68kOp::MoveWordDataAddressDisplacement:
                output << "    { uint16_t value = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.source_reg)
                       << "]; uint32_t address = s->a[" << static_cast<unsigned>(instruction.reg)
                       << "] + (uint32_t)0x" << std::hex << instruction.immediate << "U;\n"
                       << "      if (!bus || !bus->write16 || !bus->write16(bus->opaque, address, value)) return false;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffffU, 0x8000U); }\n";
                break;
            case M68kOp::SubWordDataToData:
                output << "    { uint16_t lhs = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; uint16_t rhs = (uint16_t)s->d[" << static_cast<unsigned>(instruction.source_reg)
                       << "]; uint16_t value = (uint16_t)(lhs - rhs);\n"
                       << "      s->d[" << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | value;\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x001fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x8000U) s->sr |= 0x0008U;\n"
                       << "      if (lhs < rhs) s->sr |= 0x0011U;\n"
                       << "      if (((lhs ^ rhs) & (lhs ^ value) & 0x8000U) != 0) s->sr |= 0x0002U; }\n";
                break;
            case M68kOp::NotDataRegister: {
                const std::uint32_t masks[] = {0xffU, 0xffffU, 0xffffffffU};
                const std::uint32_t signs[] = {0x80U, 0x8000U, 0x80000000U};
                const std::uint32_t mask = masks[instruction.operand_size];
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & ~0x" << std::hex << mask
                       << "U) | ((~s->d[" << static_cast<unsigned>(instruction.reg) << "]) & 0x" << mask << "U);\n"
                       << "    m68k_set_nz_sized(s, s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "], 0x" << std::hex << mask << "U, 0x" << signs[instruction.operand_size] << "U);\n";
                break;
            }
            case M68kOp::AndByteImmediateToData:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffffff00U) | (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0x" << std::hex << instruction.immediate << "U);\n"
                       << "    m68k_set_nz_sized(s, s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "], 0xffU, 0x80U);\n";
                break;
            case M68kOp::AndWordImmediateToData:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0x" << std::hex << instruction.immediate << "U);\n"
                       << "    m68k_set_nz_sized(s, s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "], 0xffffU, 0x8000U);\n";
                break;
            case M68kOp::CompareByteImmediateToData:
                output << "    { uint8_t lhs = (uint8_t)s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; uint8_t rhs = 0x" << std::hex << instruction.immediate << "U; uint8_t value = (uint8_t)(lhs - rhs);\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x80U) s->sr |= 0x0008U;\n"
                       << "      if (lhs < rhs) s->sr |= 0x0001U;\n"
                       << "      if (((lhs ^ rhs) & (lhs ^ value) & 0x80U) != 0) s->sr |= 0x0002U; }\n";
                break;
            case M68kOp::AddWordDataToData:
                output << "    { uint16_t lhs = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; uint16_t rhs = (uint16_t)s->d[" << static_cast<unsigned>(instruction.source_reg)
                       << "]; uint32_t wide = (uint32_t)lhs + rhs; uint16_t value = (uint16_t)wide;\n"
                       << "      s->d[" << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | value;\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x001fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x8000U) s->sr |= 0x0008U;\n"
                       << "      if (wide & 0x10000U) s->sr |= 0x0011U;\n"
                       << "      if ((~(lhs ^ rhs) & (lhs ^ value) & 0x8000U) != 0) s->sr |= 0x0002U; }\n";
                break;
            case M68kOp::TestDataRegister: {
                const std::uint32_t masks[] = {0xffU, 0xffffU, 0xffffffffU};
                const std::uint32_t signs[] = {0x80U, 0x8000U, 0x80000000U};
                output << "    m68k_set_nz_sized(s, s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "], 0x" << std::hex << masks[instruction.operand_size] << "U, 0x"
                       << signs[instruction.operand_size] << "U);\n";
                break;
            }
            case M68kOp::LeaAbsoluteWord:
                output << "    s->a[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = 0x"
                       << std::hex << instruction.immediate << "U;\n";
                break;
            case M68kOp::LeaAbsoluteLong:
                output << "    s->a[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = 0x"
                       << std::hex << instruction.absolute_address << "U;\n";
                break;
            case M68kOp::LeaAddressDisplacement:
                output << "    s->a[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = s->a["
                       << static_cast<unsigned>(instruction.source_reg) << "] + (uint32_t)0x" << std::hex
                       << instruction.immediate << "U;\n";
                break;
            case M68kOp::LeaPcDisplacement:
                output << "    s->a[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = 0x"
                       << std::hex << instruction.target << "U;\n";
                break;
            case M68kOp::MoveImmediateToStatus:
                output << "    s->sr = 0x" << std::hex << instruction.immediate << "U;\n";
                break;
            case M68kOp::MoveImmediateToCondition:
                output << "    s->sr = (uint16_t)((s->sr & 0xff00U) | (0x" << std::hex
                       << (instruction.immediate & 0xffU) << "U));\n";
                break;
            case M68kOp::MoveQuick:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = 0x"
                       << std::hex << instruction.immediate << "U;\n"
                       << "    m68k_set_nz32(s, s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "]);\n";
                break;
            case M68kOp::MoveLongDataToData:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = s->d["
                       << static_cast<unsigned>(instruction.source_reg) << "];\n"
                       << "    m68k_set_nz32(s, s->d[" << static_cast<unsigned>(instruction.reg) << "]);\n";
                break;
            case M68kOp::MoveLongDataToAddress:
                output << "    s->a[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = s->d["
                       << static_cast<unsigned>(instruction.source_reg) << "];\n";
                break;
            case M68kOp::MoveLongAddressToAddress:
                output << "    s->a[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = s->a["
                       << static_cast<unsigned>(instruction.source_reg) << "];\n";
                break;
            case M68kOp::TestWordAbsoluteLong:
                output << "    { uint16_t value; if (!bus || !bus->read16 || !bus->read16(bus->opaque, 0x"
                       << std::hex << instruction.absolute_address << "U, &value)) return false;\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
                       << "      if ((value & 0x8000U) != 0) s->sr |= 0x0008U;\n"
                       << "      if (value == 0) s->sr |= 0x0004U; }\n";
                break;
            case M68kOp::MoveWordImmediateToData:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | 0x" << std::hex << instruction.immediate << "U;\n"
                       << "    s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
                       << "    if ((s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] & 0x8000U) != 0) s->sr |= 0x0008U;\n"
                       << "    if ((s->d[" << static_cast<unsigned>(instruction.reg) << "] & 0xffffU) == 0) s->sr |= 0x0004U;\n";
                break;
            case M68kOp::MoveWordAddressPostIncrementToData:
                output << "    { uint16_t value; if (!bus || !bus->read16 || !bus->read16(bus->opaque, s->a["
                       << std::dec << static_cast<unsigned>(instruction.source_reg) << "], &value)) return false;\n"
                       << "      s->d[" << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | value;\n"
                       << "      s->a[" << static_cast<unsigned>(instruction.source_reg) << "] += 2U;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffffU, 0x8000U); }\n";
                break;
            case M68kOp::MoveByteAddressPostIncrementToData:
                output << "    { uint8_t value; if (!bus || !bus->read8 || !bus->read8(bus->opaque, s->a["
                       << std::dec << static_cast<unsigned>(instruction.source_reg) << "], &value)) return false;\n"
                       << "      s->d[" << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffffff00U) | value;\n"
                       << "      s->a[" << static_cast<unsigned>(instruction.source_reg) << "] += 1U;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffU, 0x80U); }\n";
                break;
            case M68kOp::MoveWordDataToData:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | (s->d["
                       << static_cast<unsigned>(instruction.source_reg) << "] & 0xffffU);\n"
                       << "    m68k_set_nz_sized(s, s->d[" << static_cast<unsigned>(instruction.reg)
                       << "], 0xffffU, 0x8000U);\n";
                break;
            case M68kOp::EorWordDataToData:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | ((s->d["
                       << static_cast<unsigned>(instruction.reg) << "] ^ s->d["
                       << static_cast<unsigned>(instruction.source_reg) << "]) & 0xffffU);\n"
                       << "    m68k_set_nz_sized(s, s->d[" << static_cast<unsigned>(instruction.reg)
                       << "], 0xffffU, 0x8000U);\n";
                break;
            case M68kOp::EorWordImmediateToData:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | ((s->d["
                       << static_cast<unsigned>(instruction.reg) << "] ^ 0x" << std::hex << instruction.immediate
                       << "U) & 0xffffU);\n"
                       << "    m68k_set_nz_sized(s, s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "], 0xffffU, 0x8000U);\n";
                break;
            case M68kOp::LogicalShiftRightWordImmediate:
                output << "    { uint16_t old = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; uint16_t value = (uint16_t)(old >> " << instruction.immediate << "U);\n"
                       << "      uint16_t carry = (uint16_t)((old >> (" << instruction.immediate << "U - 1U)) & 1U);\n"
                       << "      s->d[" << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | value;\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x001fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (carry) s->sr |= 0x0011U; }\n";
                break;
            case M68kOp::LogicalShiftLeftWordImmediate:
                output << "    { uint16_t old = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; uint16_t value = (uint16_t)(old << " << instruction.immediate
                       << "U); uint16_t carry = (uint16_t)((old >> (16U - " << instruction.immediate << "U)) & 1U);\n"
                       << "      s->d[" << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | value;\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x001fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x8000U) s->sr |= 0x0008U;\n"
                       << "      if (carry) s->sr |= 0x0011U; }\n";
                break;
            case M68kOp::MoveWordIndexedToData:
                output << "    { uint16_t value; uint32_t index = "
                       << (instruction.index_is_address ? "s->a[" : "s->d[") << std::dec
                       << static_cast<unsigned>(instruction.index_reg) << "]; uint32_t address = s->a["
                       << static_cast<unsigned>(instruction.base_reg) << "] + (uint32_t)0x" << std::hex
                       << instruction.immediate << "U + (uint32_t)("
                       << (instruction.operand_size ? "index" : "(uint32_t)(int32_t)(int16_t)index") << ");\n"
                       << "      if (!bus || !bus->read16 || !bus->read16(bus->opaque, address, &value)) return false;\n"
                       << "      s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | value;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffffU, 0x8000U); }\n";
                break;
            case M68kOp::CompareWordDataToData:
                output << "    { uint16_t lhs = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; uint16_t rhs = (uint16_t)s->d[" << static_cast<unsigned>(instruction.source_reg)
                       << "]; uint16_t value = (uint16_t)(lhs - rhs);\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x8000U) s->sr |= 0x0008U;\n"
                       << "      if (lhs < rhs) s->sr |= 0x0001U;\n"
                       << "      if (((lhs ^ rhs) & (lhs ^ value) & 0x8000U) != 0) s->sr |= 0x0002U; }\n";
                break;
            case M68kOp::MoveByteImmediateToIndexed:
                output << "    { uint32_t index = " << (instruction.index_is_address ? "s->a[" : "s->d[")
                       << std::dec << static_cast<unsigned>(instruction.index_reg) << "]; uint32_t address = s->a["
                       << static_cast<unsigned>(instruction.reg) << "] + (uint32_t)0x" << std::hex
                       << instruction.absolute_address << "U + (uint32_t)("
                       << (instruction.operand_size ? "index" : "(uint32_t)(int32_t)(int16_t)index")
                       << "); uint8_t value = 0x" << std::hex << instruction.immediate << "U;\n"
                       << "      if (!bus || !bus->write8 || !bus->write8(bus->opaque, address, value)) return false;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffU, 0x80U); }\n";
                break;
            case M68kOp::MoveWordDataToAddressIndirect:
                output << "    { uint16_t value = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.source_reg)
                       << "]; if (!bus || !bus->write16 || !bus->write16(bus->opaque, s->a["
                       << static_cast<unsigned>(instruction.reg) << "], value)) return false;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffffU, 0x8000U); }\n";
                break;
            case M68kOp::LeaIndexed:
                output << "    { uint32_t index = " << (instruction.index_is_address ? "s->a[" : "s->d[")
                       << std::dec << static_cast<unsigned>(instruction.index_reg) << "]; s->a["
                       << static_cast<unsigned>(instruction.reg) << "] = s->a[" << static_cast<unsigned>(instruction.base_reg)
                       << "] + (uint32_t)0x" << std::hex << instruction.immediate << "U + (uint32_t)("
                       << (instruction.operand_size ? "index" : "(uint32_t)(int32_t)(int16_t)index") << "); }\n";
                break;
            case M68kOp::LeaAddressIndirect:
                output << "    s->a[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = s->a["
                       << static_cast<unsigned>(instruction.source_reg) << "];\n";
                break;
            case M68kOp::MoveWordDataToIndexed:
                output << "    { uint32_t index = " << (instruction.index_is_address ? "s->a[" : "s->d[")
                       << std::dec << static_cast<unsigned>(instruction.index_reg) << "]; uint32_t address = s->a["
                       << static_cast<unsigned>(instruction.reg) << "] + (uint32_t)0x" << std::hex << instruction.immediate
                       << "U + (uint32_t)(" << (instruction.operand_size ? "index" : "(uint32_t)(int32_t)(int16_t)index")
                       << "); uint16_t value = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.source_reg) << "];\n"
                       << "      if (!bus || !bus->write16 || !bus->write16(bus->opaque, address, value)) return false;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffffU, 0x8000U); }\n";
                break;
            case M68kOp::AddQuickByteIndexed:
                output << "    { uint32_t index = " << (instruction.index_is_address ? "s->a[" : "s->d[")
                       << std::dec << static_cast<unsigned>(instruction.index_reg) << "]; uint32_t address = s->a["
                       << static_cast<unsigned>(instruction.reg) << "] + (uint32_t)0x" << std::hex
                       << instruction.absolute_address << "U + (uint32_t)("
                       << (instruction.operand_size ? "index" : "(uint32_t)(int32_t)(int16_t)index")
                       << "); uint8_t old, value; if (!bus || !bus->read8 || !bus->write8 || !bus->read8(bus->opaque, address, &old)) return false;\n"
                       << "      value = (uint8_t)(old + " << std::dec << instruction.immediate
                       << "U); if (!bus->write8(bus->opaque, address, value)) return false;\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x001fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x80U) s->sr |= 0x0008U;\n"
                       << "      if ((uint16_t)old + " << instruction.immediate << "U > 0xffU) s->sr |= 0x0011U;\n"
                       << "      if ((~(old ^ " << instruction.immediate << "U) & (old ^ value) & 0x80U) != 0) s->sr |= 0x0002U; }\n";
                break;
            case M68kOp::CompareByteAbsoluteWordToData:
                output << "    { uint8_t rhs; uint8_t lhs = (uint8_t)s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; if (!bus || !bus->read8 || !bus->read8(bus->opaque, (uint32_t)0x" << std::hex
                       << instruction.absolute_address << "U, &rhs)) return false; uint8_t value = (uint8_t)(lhs - rhs);\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x80U) s->sr |= 0x0008U;\n"
                       << "      if (lhs < rhs) s->sr |= 0x0001U;\n"
                       << "      if (((lhs ^ rhs) & (lhs ^ value) & 0x80U) != 0) s->sr |= 0x0002U; }\n";
                break;
            case M68kOp::CompareWordAddressDisplacementToData:
                output << "    { uint16_t rhs; uint16_t lhs = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; uint32_t address = s->a[" << static_cast<unsigned>(instruction.source_reg)
                       << "] + (uint32_t)0x" << std::hex << instruction.immediate
                       << "U; if (!bus || !bus->read16 || !bus->read16(bus->opaque, address, &rhs)) return false; uint16_t value = (uint16_t)(lhs - rhs);\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x8000U) s->sr |= 0x0008U;\n"
                       << "      if (lhs < rhs) s->sr |= 0x0001U;\n"
                       << "      if (((lhs ^ rhs) & (lhs ^ value) & 0x8000U) != 0) s->sr |= 0x0002U; }\n";
                break;
            case M68kOp::CompareWordAddressIndirectToData:
                output << "    { uint16_t rhs; uint16_t lhs = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; if (!bus || !bus->read16 || !bus->read16(bus->opaque, s->a["
                       << static_cast<unsigned>(instruction.source_reg) << "], &rhs)) return false; uint16_t value = (uint16_t)(lhs - rhs);\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x8000U) s->sr |= 0x0008U;\n"
                       << "      if (lhs < rhs) s->sr |= 0x0001U;\n"
                       << "      if (((lhs ^ rhs) & (lhs ^ value) & 0x8000U) != 0) s->sr |= 0x0002U; }\n";
                break;
            case M68kOp::MoveLongAddressPostIncrementToAddressPostIncrement:
                output << "    { uint16_t hi, lo; uint32_t value; uint32_t source = s->a["
                       << std::dec << static_cast<unsigned>(instruction.source_reg) << "];\n"
                       << "      if (!bus || !bus->read16 || !bus->write16 || !bus->read16(bus->opaque, source, &hi)\n"
                       << "          || !bus->read16(bus->opaque, source + 2U, &lo)) return false;\n"
                       << "      value = ((uint32_t)hi << 16) | lo; s->a[" << static_cast<unsigned>(instruction.source_reg)
                       << "] += 4U; source = s->a[" << static_cast<unsigned>(instruction.reg) << "];\n"
                       << "      if (!bus->write16(bus->opaque, source, (uint16_t)(value >> 16))\n"
                       << "          || !bus->write16(bus->opaque, source + 2U, (uint16_t)value)) return false;\n"
                       << "      s->a[" << static_cast<unsigned>(instruction.reg) << "] += 4U; m68k_set_nz32(s, value); }\n";
                break;
            case M68kOp::MoveLongAddressIndirectToAddressIndirect:
                output << "    { uint16_t hi, lo; uint32_t value; uint32_t source = s->a["
                       << std::dec << static_cast<unsigned>(instruction.source_reg) << "]; uint32_t destination = s->a["
                       << static_cast<unsigned>(instruction.reg) << "];\n"
                       << "      if (!bus || !bus->read16 || !bus->write16 || !bus->read16(bus->opaque, source, &hi)\n"
                       << "          || !bus->read16(bus->opaque, source + 2U, &lo)) return false;\n"
                       << "      value = ((uint32_t)hi << 16) | lo;\n"
                       << "      if (!bus->write16(bus->opaque, destination, (uint16_t)(value >> 16))\n"
                       << "          || !bus->write16(bus->opaque, destination + 2U, (uint16_t)value)) return false;\n"
                       << "      m68k_set_nz32(s, value); }\n";
                break;
            case M68kOp::MoveLongImmediateToAddressPostIncrement:
                output << "    { uint32_t value = 0x" << std::hex << instruction.immediate << "U; uint32_t address = s->a["
                       << std::dec << static_cast<unsigned>(instruction.reg) << "];\n"
                       << "      if (!bus || !bus->write16 || !bus->write16(bus->opaque, address, (uint16_t)(value >> 16))\n"
                       << "          || !bus->write16(bus->opaque, address + 2U, (uint16_t)value)) return false;\n"
                       << "      s->a[" << static_cast<unsigned>(instruction.reg) << "] += 4U; m68k_set_nz32(s, value); }\n";
                break;
            case M68kOp::MoveLongImmediateAddressDisplacement:
                output << "    { uint32_t value = 0x" << std::hex << instruction.immediate << "U; uint32_t address = s->a["
                       << std::dec << static_cast<unsigned>(instruction.reg) << "] + (uint32_t)0x" << std::hex
                       << instruction.absolute_address << "U;\n"
                       << "      if (!bus || !bus->write16 || !bus->write16(bus->opaque, address, (uint16_t)(value >> 16))\n"
                       << "          || !bus->write16(bus->opaque, address + 2U, (uint16_t)value)) return false;\n"
                       << "      m68k_set_nz32(s, value); }\n";
                break;
            case M68kOp::MoveWordAddressPostIncrementToAddressPostIncrement:
                output << "    { uint16_t value; uint32_t source = s->a[" << std::dec
                       << static_cast<unsigned>(instruction.source_reg) << "]; if (!bus || !bus->read16 || !bus->write16\n"
                       << "          || !bus->read16(bus->opaque, source, &value)) return false;\n"
                       << "      s->a[" << static_cast<unsigned>(instruction.source_reg) << "] += 2U; source = s->a["
                       << static_cast<unsigned>(instruction.reg) << "]; if (!bus->write16(bus->opaque, source, value)) return false;\n"
                       << "      s->a[" << static_cast<unsigned>(instruction.reg) << "] += 2U; m68k_set_nz_sized(s, value, 0xffffU, 0x8000U); }\n";
                break;
            case M68kOp::MoveLongAbsoluteWordToAbsoluteWord:
                output << "    { uint16_t hi, lo; uint32_t value; uint32_t source = (uint32_t)0x" << std::hex
                       << instruction.absolute_address << "U; uint32_t destination = (uint32_t)0x"
                       << instruction.immediate << "U;\n"
                       << "      if (!bus || !bus->read16 || !bus->write16 || !bus->read16(bus->opaque, source, &hi)\n"
                       << "          || !bus->read16(bus->opaque, source + 2U, &lo)) return false;\n"
                       << "      value = ((uint32_t)hi << 16) | lo;\n"
                       << "      if (!bus->write16(bus->opaque, destination, (uint16_t)(value >> 16))\n"
                       << "          || !bus->write16(bus->opaque, destination + 2U, (uint16_t)value)) return false;\n"
                       << "      m68k_set_nz32(s, value); }\n";
                break;
            case M68kOp::AndWordImmediateSourceToData:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | ((s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffffU) & 0x" << std::hex
                       << instruction.immediate << "U);\n"
                       << "    m68k_set_nz_sized(s, s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "], 0xffffU, 0x8000U);\n";
                break;
            case M68kOp::MoveLongIndexedToAddress:
                output << "    { uint32_t index = " << (instruction.index_is_address ? "s->a[" : "s->d[")
                       << std::dec << static_cast<unsigned>(instruction.index_reg) << "]; uint32_t address = s->a["
                       << static_cast<unsigned>(instruction.base_reg) << "] + (uint32_t)0x" << std::hex
                       << instruction.immediate << "U + (uint32_t)("
                       << (instruction.operand_size ? "index" : "(uint32_t)(int32_t)(int16_t)index") << "); uint16_t hi, lo;\n"
                       << "      if (!bus || !bus->read16 || !bus->read16(bus->opaque, address, &hi)\n"
                       << "          || !bus->read16(bus->opaque, address + 2U, &lo)) return false;\n"
                       << "      s->a[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = ((uint32_t)hi << 16) | lo; }\n";
                break;
            case M68kOp::SubQuickWordToData:
                output << "    { uint16_t lhs = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; uint16_t rhs = " << instruction.immediate << "U; uint16_t value = (uint16_t)(lhs - rhs);\n"
                       << "      s->d[" << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | value;\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x001fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x8000U) s->sr |= 0x0008U;\n"
                       << "      if (lhs < rhs) s->sr |= 0x0011U;\n"
                       << "      if (((lhs ^ rhs) & (lhs ^ value) & 0x8000U) != 0) s->sr |= 0x0002U; }\n";
                break;
            case M68kOp::OrByteImmediateToData:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffffff00U) | ((s->d["
                       << static_cast<unsigned>(instruction.reg) << "] | 0x" << std::hex << instruction.immediate
                       << "U) & 0xffU);\n"
                       << "    m68k_set_nz_sized(s, s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "], 0xffU, 0x80U);\n";
                break;
            case M68kOp::MoveLongPostIncrementToAddress:
                output << "    { uint16_t hi, lo; uint32_t source = s->a[" << std::dec
                       << static_cast<unsigned>(instruction.source_reg) << "];\n"
                       << "      if (!bus || !bus->read16 || !bus->read16(bus->opaque, source, &hi)\n"
                       << "          || !bus->read16(bus->opaque, source + 2U, &lo)) return false;\n"
                       << "      s->a[" << static_cast<unsigned>(instruction.source_reg) << "] += 4U; s->a["
                       << static_cast<unsigned>(instruction.reg) << "] = ((uint32_t)hi << 16) | lo; }\n";
                break;
            case M68kOp::MoveWordAddressDisplacementToData:
                output << "    { uint16_t value; uint32_t address = s->a[" << std::dec
                       << static_cast<unsigned>(instruction.source_reg) << "] + (uint32_t)0x" << std::hex
                       << instruction.immediate << "U; if (!bus || !bus->read16 || !bus->read16(bus->opaque, address, &value)) return false;\n"
                       << "      s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | value;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffffU, 0x8000U); }\n";
                break;
            case M68kOp::MoveLongImmediateToAddress:
                output << "    s->a[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = 0x"
                       << std::hex << instruction.immediate << "U;\n";
                break;
            case M68kOp::MoveLongDataToAddressIndirect:
                output << "    { uint32_t value = s->d[" << std::dec << static_cast<unsigned>(instruction.source_reg)
                       << "]; uint32_t address = s->a[" << static_cast<unsigned>(instruction.reg) << "];\n"
                       << "      if (!bus || !bus->write16 || !bus->write16(bus->opaque, address, (uint16_t)(value >> 16))\n"
                       << "          || !bus->write16(bus->opaque, address + 2U, (uint16_t)value)) return false;\n"
                       << "      m68k_set_nz32(s, value); }\n";
                break;
            case M68kOp::MoveLongDataAddressDisplacement:
                output << "    { uint32_t value = s->d[" << std::dec << static_cast<unsigned>(instruction.source_reg)
                       << "]; uint32_t address = s->a[" << static_cast<unsigned>(instruction.reg)
                       << "] + (uint32_t)0x" << std::hex << instruction.immediate << "U;\n"
                       << "      if (!bus || !bus->write16 || !bus->write16(bus->opaque, address, (uint16_t)(value >> 16))\n"
                       << "          || !bus->write16(bus->opaque, address + 2U, (uint16_t)value)) return false;\n"
                       << "      m68k_set_nz32(s, value); }\n";
                break;
            case M68kOp::MoveWordImmediateAddressIndirect:
                output << "    if (!bus || !bus->write16 || !bus->write16(bus->opaque, s->a["
                       << std::dec << static_cast<unsigned>(instruction.reg) << "], 0x" << std::hex
                       << instruction.immediate << "U)) return false;\n"
                       << "    m68k_set_nz_sized(s, 0x" << std::hex << instruction.immediate << "U, 0xffffU, 0x8000U);\n";
                break;
            case M68kOp::MoveLongImmediateToData:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = 0x"
                       << std::hex << instruction.immediate << "U;\n"
                       << "    m68k_set_nz32(s, s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "]);\n";
                break;
            case M68kOp::MoveWordImmediateToAddress:
                output << "    s->a[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (uint32_t)0x"
                       << std::hex << instruction.immediate << "U;\n";
                break;
            case M68kOp::RotateRightWordImmediate:
                output << "    { uint16_t old = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; uint16_t value = (uint16_t)((old >> " << instruction.immediate << "U) | (old << (16U - "
                       << instruction.immediate << "U))); uint16_t carry = (uint16_t)((old >> (" << instruction.immediate
                       << "U - 1U)) & 1U);\n"
                       << "      s->d[" << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | value;\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x8000U) s->sr |= 0x0008U;\n"
                       << "      if (carry) s->sr |= 0x0001U; }\n";
                break;
            case M68kOp::ClearLongAddressPostIncrement:
                output << "    if (!bus || !bus->write16 || !bus->write16(bus->opaque, s->a["
                       << std::dec << static_cast<unsigned>(instruction.reg) << "], 0) || !bus->write16(bus->opaque, s->a["
                       << static_cast<unsigned>(instruction.reg) << "] + 2U, 0)) return false;\n"
                       << "    s->a[" << static_cast<unsigned>(instruction.reg) << "] += 4U;\n"
                       << "    s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU) | 0x0004U;\n";
                break;
            case M68kOp::ClearWordAddressDisplacement:
                output << "    if (!bus || !bus->write16 || !bus->write16(bus->opaque, s->a["
                       << std::dec << static_cast<unsigned>(instruction.reg) << "] + (uint32_t)0x"
                       << std::hex << instruction.absolute_address << "U, 0)) return false;\n"
                       << "    s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU) | 0x0004U;\n";
                break;
            case M68kOp::ClearByteAddressDisplacement:
                output << "    if (!bus || !bus->write8 || !bus->write8(bus->opaque, s->a["
                       << std::dec << static_cast<unsigned>(instruction.reg) << "] + (uint32_t)0x"
                       << std::hex << instruction.absolute_address << "U, 0)) return false;\n"
                       << "    s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU) | 0x0004U;\n";
                break;
            case M68kOp::MoveByteAddressDisplacementToData:
                output << "    { uint8_t value; uint32_t address = s->a[" << std::dec
                       << static_cast<unsigned>(instruction.source_reg) << "] + (uint32_t)0x" << std::hex
                       << instruction.immediate << "U; if (!bus || !bus->read8 || !bus->read8(bus->opaque, address, &value)) return false;\n"
                       << "      s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffffff00U) | value;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffU, 0x80U); }\n";
                break;
            case M68kOp::MoveByteIndexedToAddressDisplacement:
                output << "    { uint8_t value; uint32_t index = "
                       << (instruction.index_is_address ? "s->a[" : "s->d[") << std::dec
                       << static_cast<unsigned>(instruction.index_reg) << "];\n"
                       << "      if (!bus || !bus->read8 || !bus->write8 || !bus->read8(bus->opaque, s->a["
                       << static_cast<unsigned>(instruction.base_reg) << "] + (uint32_t)0x" << std::hex
                       << instruction.immediate << "U + (uint32_t)("
                       << (instruction.operand_size ? "index" : "(uint32_t)(int32_t)(int16_t)index") << "), &value)\n"
                       << "          || !bus->write8(bus->opaque, s->a[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "] + (uint32_t)0x" << std::hex << instruction.absolute_address << "U, value)) return false;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffU, 0x80U); }\n";
                break;
            case M68kOp::MoveByteIndexedToData:
                output << "    { uint8_t value; uint32_t index = "
                       << (instruction.index_is_address ? "s->a[" : "s->d[") << std::dec
                       << static_cast<unsigned>(instruction.index_reg) << "]; uint32_t address = s->a["
                       << static_cast<unsigned>(instruction.base_reg) << "] + (uint32_t)0x" << std::hex
                       << instruction.immediate << "U + (uint32_t)("
                       << (instruction.operand_size ? "index" : "(uint32_t)(int32_t)(int16_t)index") << ");\n"
                       << "      if (!bus || !bus->read8 || !bus->read8(bus->opaque, address, &value)) return false;\n"
                       << "      s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffffff00U) | value;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffU, 0x80U); }\n";
                break;
            case M68kOp::CompareWordImmediateToData:
                output << "    { uint16_t lhs = (uint16_t)s->d[" << std::dec << static_cast<unsigned>(instruction.reg)
                       << "]; uint16_t rhs = 0x" << std::hex << instruction.immediate << "U; uint16_t value = (uint16_t)(lhs - rhs);\n"
                       << "      s->sr = (uint16_t)(s->sr & (uint16_t)~0x000fU);\n"
                       << "      if (value == 0) s->sr |= 0x0004U;\n"
                       << "      if (value & 0x8000U) s->sr |= 0x0008U;\n"
                       << "      if (lhs < rhs) s->sr |= 0x0001U;\n"
                       << "      if (((lhs ^ rhs) & (lhs ^ value) & 0x8000U) != 0) s->sr |= 0x0002U; }\n";
                break;
            case M68kOp::MoveByteAbsoluteWordToData:
                output << "    { uint8_t value; if (!bus || !bus->read8 || !bus->read8(bus->opaque, (uint32_t)0x"
                       << std::hex << instruction.absolute_address << "U, &value)) return false;\n"
                       << "      s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffffff00U) | value;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffU, 0x80U); }\n";
                break;
            case M68kOp::MoveByteDataToAddressDisplacement:
                output << "    { uint8_t value = (uint8_t)s->d[" << std::dec << static_cast<unsigned>(instruction.source_reg)
                       << "]; if (!bus || !bus->write8 || !bus->write8(bus->opaque, s->a["
                       << static_cast<unsigned>(instruction.reg) << "] + (uint32_t)0x" << std::hex
                       << instruction.immediate << "U, value)) return false;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffU, 0x80U); }\n";
                break;
            case M68kOp::MoveByteDataToIndexed:
                output << "    { uint32_t index = " << (instruction.index_is_address ? "s->a[" : "s->d[")
                       << std::dec << static_cast<unsigned>(instruction.index_reg) << "]; uint32_t address = s->a["
                       << static_cast<unsigned>(instruction.reg) << "] + (uint32_t)0x" << std::hex << instruction.immediate
                       << "U + (uint32_t)(" << (instruction.operand_size ? "index" : "(uint32_t)(int32_t)(int16_t)index")
                       << "); uint8_t value = (uint8_t)s->d[" << std::dec << static_cast<unsigned>(instruction.source_reg) << "];\n"
                       << "      if (!bus || !bus->write8 || !bus->write8(bus->opaque, address, value)) return false;\n"
                       << "      m68k_set_nz_sized(s, value, 0xffU, 0x80U); }\n";
                break;
            case M68kOp::OrLongDataAddressPostIncrement:
                output << "    { uint16_t hi, lo; uint32_t value; uint32_t address = s->a["
                       << std::dec << static_cast<unsigned>(instruction.reg) << "];\n"
                       << "      if (!bus || !bus->read16 || !bus->write16 || !bus->read16(bus->opaque, address, &hi)\n"
                       << "          || !bus->read16(bus->opaque, address + 2U, &lo)) return false;\n"
                       << "      value = (((uint32_t)hi << 16) | lo) | s->d["
                       << static_cast<unsigned>(instruction.source_reg) << "];\n"
                       << "      if (!bus->write16(bus->opaque, address, (uint16_t)(value >> 16))\n"
                       << "          || !bus->write16(bus->opaque, address + 2U, (uint16_t)value)) return false;\n"
                       << "      s->a[" << static_cast<unsigned>(instruction.reg) << "] += 4U;\n"
                       << "      m68k_set_nz32(s, value); }\n";
                break;
            case M68kOp::DecrementBranchWord:
                output << "    s->d[" << std::dec << static_cast<unsigned>(instruction.reg) << "] = (s->d["
                       << static_cast<unsigned>(instruction.reg) << "] & 0xffff0000U) | ((s->d["
                       << static_cast<unsigned>(instruction.reg) << "] - 1U) & 0xffffU);\n"
                       << "    s->pc = ((s->d[" << static_cast<unsigned>(instruction.reg) << "] & 0xffffU) != 0xffffU) ? 0x"
                       << std::hex << instruction.target << "U : 0x" << instruction.next_pc << "U;\n"
                       << "    return true;\n";
                break;
            case M68kOp::CallAbsoluteLong:
                output << "    { uint32_t sp = s->a[7] - 4U; if (!bus || !bus->write16\n"
                       << "          || !bus->write16(bus->opaque, sp, (uint16_t)(0x" << std::hex << instruction.next_pc << "U >> 16))\n"
                       << "          || !bus->write16(bus->opaque, sp + 2U, (uint16_t)0x" << instruction.next_pc << "U)) return false;\n"
                       << "      s->a[7] = sp; s->pc = 0x" << instruction.target << "U; return true; }\n";
                break;
            case M68kOp::JumpAbsoluteLong:
                output << "    s->pc = 0x" << std::hex << instruction.target << "U; return true;\n";
                break;
            case M68kOp::CallRelativeBranch:
                output << "    { uint32_t sp = s->a[7] - 4U; if (!bus || !bus->write16\n"
                       << "          || !bus->write16(bus->opaque, sp, (uint16_t)(0x" << std::hex << instruction.next_pc << "U >> 16))\n"
                       << "          || !bus->write16(bus->opaque, sp + 2U, (uint16_t)0x" << instruction.next_pc << "U)) return false;\n"
                       << "      s->a[7] = sp; s->pc = 0x" << instruction.target << "U; return true; }\n";
                break;
            case M68kOp::ReturnSubroutine:
                output << "    { uint16_t hi, lo; uint32_t sp = s->a[7]; if (!bus || !bus->read16\n"
                       << "          || !bus->read16(bus->opaque, sp, &hi) || !bus->read16(bus->opaque, sp + 2U, &lo)) return false;\n"
                       << "      s->a[7] = sp + 4U; s->pc = ((uint32_t)hi << 16) | lo; return true; }\n";
                break;
            case M68kOp::Nop:
                break;
            case M68kOp::Stop:
                output << "    s->sr = 0x" << std::hex << instruction.immediate << "U; s->stopped = true;\n";
                break;
            case M68kOp::Unsupported:
                break;
            }
            if (instruction.op != M68kOp::Branch && instruction.op != M68kOp::DecrementBranchWord
                && instruction.op != M68kOp::CallAbsoluteLong && instruction.op != M68kOp::CallRelativeBranch
                && instruction.op != M68kOp::JumpAbsoluteLong
                && instruction.op != M68kOp::ReturnSubroutine) {
                output << "    s->pc = 0x" << std::hex << instruction.next_pc << "U;\n";
            }
            if (instruction.op == M68kOp::Stop) {
                output << "    return true;\n";
            }
        }

        if (block.stop_reason.find("branch terminates") == std::string::npos
            && block.stop_reason.find("DBF terminates") == std::string::npos
            && block.stop_reason.find("subroutine call terminates") == std::string::npos
            && block.stop_reason.find("JMP terminates") == std::string::npos
            && block.stop_reason.find("BSR terminates") == std::string::npos
            && block.stop_reason.find("RTS terminates") == std::string::npos
            && block.stop_reason.find("STOP terminates") == std::string::npos) {
            output << "    /* Translation stops here: " << block.stop_reason << " */\n"
                   << "    return false;\n";
        }
        output << "}\n\n";
    }

    output << "bool mamerecomp_dispatch(M68kState *s, const M68kBus *bus) {\n"
           << "    if (!s) return false;\n    switch (s->pc) {\n";
    for (const auto& block : translation.blocks) {
        output << "    case 0x" << std::hex << block.start << "U: return block_" << block.start << "(s, bus);\n";
    }
    output << "    default: return false;\n    }\n}\n\n"
           << "uint32_t mamerecomp_entry_pc(void) { return 0x" << std::hex << entry_pc << "U; }\n";
    return output.str();
}

} // namespace mamerecomp
