#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace mamerecomp {

enum class M68kOp {
    Branch,
    MoveByteImmediateAbsoluteLong,
    MoveByteImmediateAbsoluteWord,
    TestDataRegister,
    LeaAbsoluteWord,
    LeaAddressDisplacement,
    LeaPcDisplacement,
    LeaAbsoluteLong,
    MoveImmediateToStatus,
    MoveImmediateToCondition,
    MoveQuick,
    MoveLongDataToData,
    MoveLongDataToAddress,
    MoveLongAddressToAddress,
    MoveWordImmediateToData,
    MoveWordAddressPostIncrementToData,
    MoveByteAddressPostIncrementToData,
    MoveWordDataToData,
    MoveByteDataToAbsoluteWord,
    MoveByteDataToData,
    MoveByteIndexedToAbsoluteWord,
    MoveLongImmediateAbsoluteWord,
    MoveLongDataPostIncrement,
    ClearByteAbsoluteWord,
    SetBitImmediateAbsoluteWord,
    TestByteAbsoluteWord,
    MoveWordDataAbsoluteWord,
    MoveLongDataAbsoluteWord,
    AddQuickByteAbsoluteWord,
    AndByteImmediateAbsoluteWord,
    CompareByteImmediateAbsoluteWord,
    MovemPredecrement,
    MovemPostincrement,
    TestBitImmediateData,
    SetBitImmediateData,
    OrWordDataToData,
    MoveWordDataPostIncrement,
    MoveWordImmediateAddressDisplacement,
    MoveLongAddressAddressDisplacement,
    MoveByteImmediateAddressDisplacement,
    MoveWordDataAddressDisplacement,
    SubWordDataToData,
    ClearWordAddressDisplacement,
    ClearByteAddressDisplacement,
    MoveByteAddressDisplacementToData,
    MoveByteIndexedToAddressDisplacement,
    MoveByteIndexedToData,
    CompareWordImmediateToData,
    MoveByteAbsoluteWordToData,
    MoveByteDataToAddressDisplacement,
    MoveByteDataToIndexed,
    LogicalShiftLeftWordImmediate,
    MoveWordIndexedToData,
    CompareWordDataToData,
    MoveByteImmediateToIndexed,
    MoveWordDataToAddressIndirect,
    MoveWordDataToIndexed,
    AddQuickByteIndexed,
    CompareByteAbsoluteWordToData,
    CompareWordAddressDisplacementToData,
    MoveLongAddressPostIncrementToAddressPostIncrement,
    MoveLongImmediateToAddress,
    MoveWordImmediateToAddress,
    MoveLongDataToAddressIndirect,
    MoveLongDataAddressDisplacement,
    MoveWordImmediateAddressIndirect,
    MoveLongImmediateToData,
    LeaAddressIndirect,
    CompareWordAddressIndirectToData,
    MoveLongAddressIndirectToAddressIndirect,
    MoveLongImmediateToAddressPostIncrement,
    MoveLongImmediateAddressDisplacement,
    MoveWordAddressPostIncrementToAddressPostIncrement,
    MoveLongAbsoluteWordToAbsoluteWord,
    AndWordImmediateSourceToData,
    MoveLongIndexedToAddress,
    SubQuickWordToData,
    OrByteImmediateToData,
    MoveLongPostIncrementToAddress,
    MoveWordAddressDisplacementToData,
    LeaIndexed,
    MoveWordImmediateAbsoluteWord,
    MoveByteAbsoluteLongToData,
    AndByteImmediateToData,
    AndWordImmediateToData,
    CompareByteImmediateToData,
    AddWordDataToData,
    NotDataRegister,
    EorWordDataToData,
    EorWordImmediateToData,
    LogicalShiftRightWordImmediate,
    RotateRightWordImmediate,
    ClearLongAddressPostIncrement,
    OrLongDataAddressPostIncrement,
    TestWordAbsoluteLong,
    DecrementBranchWord,
    CallAbsoluteLong,
    JumpAbsoluteLong,
    CallRelativeBranch,
    ReturnSubroutine,
    Nop,
    Stop,
    Unsupported,
};

struct M68kInstruction {
    M68kOp op = M68kOp::Unsupported;
    std::uint32_t address = 0;
    std::uint32_t next_pc = 0;
    std::uint32_t target = 0;
    std::uint32_t immediate = 0;
    std::uint32_t absolute_address = 0;
    std::uint16_t opcode = 0;
    std::uint16_t register_mask = 0;
    std::uint8_t reg = 0;
    std::uint8_t source_reg = 0;
    std::uint8_t condition = 0;
    std::uint8_t operand_size = 0;
    std::uint8_t index_reg = 0;
    std::uint8_t base_reg = 0;
    bool index_is_address = false;
    std::string reason;
};

struct M68kBlock {
    std::uint32_t start = 0;
    std::vector<M68kInstruction> instructions;
    std::uint32_t stop_pc = 0;
    std::string stop_reason;
};

struct M68kTranslation {
    std::vector<M68kBlock> blocks;
    std::size_t translated_instructions = 0;
    std::size_t unsupported_blocks = 0;
};

M68kInstruction decode_68000(std::span<const std::uint8_t> memory, std::uint32_t pc);
M68kTranslation translate_68000_blocks(std::span<const std::uint8_t> memory, std::uint32_t entry_pc,
    std::size_t max_blocks = 2048, std::size_t max_instructions_per_block = 512);
std::string format_68000_instruction(const M68kInstruction& instruction);
std::string emit_c_translation(const M68kTranslation& translation, std::uint32_t entry_pc);

} // namespace mamerecomp
