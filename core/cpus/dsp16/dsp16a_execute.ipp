// Shared DSP16A instruction semantics. Static opcode instantiations let the
// compiler fold the opcode group and bit fields while preserving the exact
// interpreter implementation for every instruction.
template <bool StaticOpcode, std::uint16_t Opcode>
unsigned Dsp16A::execute_instruction_specialized(std::uint16_t runtime_op, bool from_cache)
{
    const std::uint16_t op = StaticOpcode ? Opcode : runtime_op;

    if (bus_.instruction_trace)
        bus_.instruction_trace(Dsp16ATracePhase::Before, instruction_pc_, op, state_);
    const unsigned group = StaticOpcode ? (Opcode >> 11) : (op >> 11);
    unsigned cost = 1;
    switch (group) {
    case 0x00: case 0x01: case 0x10: case 0x11:
        if (check_predicate()) {
            if (op & 0x8000U) state_.pr = state_.pc;
            set_pc_offset(op_ja(op));
        }
        phase_ = Phase::Purge;
        break;
    case 0x02: case 0x03: yaau_short_immediate_load(op); break;
    case 0x04: case 0x1c: {
        phase_ = Phase::Op2;
        cost = 2;
        break;
    }
    case 0x05: {
        const auto d = dau_f1(op);
        state_.temp = static_cast<std::int16_t>(dau_saturate(op_d(static_cast<std::uint16_t>(~op))) >> (op_x(op) ? 16 : 0));
        state_.a[op_d(op)] = d;
        dau_set_atx(op, yaau_read(op));
        phase_ = Phase::Op2;
        cost = 2;
        break;
    }
    case 0x06:
        state_.a[op_d(op)] = dau_f1(op);
        (void)yaau_read(op);
        break;
    case 0x07:
        state_.a[op_d(op)] = dau_f1(op);
        dau_set_atx(op, yaau_read(op));
        break;
    case 0x08:
        dau_set_at(op, get_r(op));
        cost = 2;
        break;
    case 0x09: case 0x0b:
        set_r(op, static_cast<std::int16_t>(dau_saturate((op >> 12) & 1U) >> 16));
        cost = 2;
        break;
    case 0x0a:
        phase_ = Phase::Op2;
        cost = 2;
        break;
    case 0x0c:
        phase_ = Phase::Op2;
        cost = 2;
        break;
    case 0x0d: {
        const auto old = get_r(op);
        state_.temp = old;
        const auto value = yaau_read(op);
        set_r(op, value);
        phase_ = Phase::Op2;
        cost = 2;
        break;
    }
    case 0x0e: {
        const unsigned ni = op_ni(op);
        const unsigned iterations = op_k(op);
        if (ni) {
            cache_pcbase_ = state_.pc;
            cache_limit_ = static_cast<std::uint16_t>(ni);
            cache_iterations_ = static_cast<std::uint16_t>(iterations);
            cache_ptr_ = 1;
            cache_mode_ = CacheMode::Load;
        } else {
            // MAME accepts the hardware's REDO form even before an explicit
            // DO load. The cache contents are then whatever the DSP cache
            // currently contains (zeroed here on reset), not an illegal-op
            // condition. Do not reject valid firmware at reset.
            cache_ptr_ = 1;
            cache_iterations_ = static_cast<std::uint16_t>(iterations);
            cache_mode_ = CacheMode::Execute;
            phase_ = Phase::Prefetch;
        }
        break;
    }
    case 0x0f: set_r(op, yaau_read(op)); cost = 2; break;
    case 0x12: {
        const bool con = op_dau_con(op, false);
        ++state_.c[1];
        if (con) { dau_f2(op); state_.c[2] = state_.c[1]; }
        break;
    }
    case 0x13: if (op_dau_con(op, true, true)) dau_f2(op); break;
    case 0x14:
        state_.a[op_d(op)] = dau_f1(op);
        phase_ = Phase::Op2;
        cost = 2;
        break;
    case 0x15:
        state_.a[op_d(op)] = dau_f1(op);
        state_.temp = static_cast<std::int16_t>(op_x(op) ? state_.y >> 16 : state_.y);
        dau_set_y(op, yaau_read(op));
        phase_ = Phase::Op2;
        cost = 2;
        break;
    case 0x16:
        state_.a[op_d(op)] = dau_f1(op);
        state_.x = yaau_read(op);
        break;
    case 0x17:
        state_.a[op_d(op)] = dau_f1(op);
        dau_set_y(op, yaau_read(op));
        break;
    case 0x18:
        switch (op_b(op)) {
        case 0: if (check_predicate()) state_.pc = state_.pr; break;
        case 1: set_iack(iack_clear_); state_.pc = state_.pi; break;
        case 2: if (check_predicate()) state_.pc = state_.pt; break;
        case 3: if (check_predicate()) { state_.pr = state_.pc; state_.pc = state_.pt; } break;
        default: throw std::runtime_error("DSP16A reserved branch operation");
        }
        if (iack_out_) state_.pi = state_.pc;
        if (!from_cache) phase_ = Phase::Purge;
        break;
    case 0x19: case 0x1b: {
        const unsigned src = group == 0x19 ? 0U : 1U;
        // MAME evaluates F1, snapshots the selected accumulator into Y, and
        // only then commits the destination accumulator. Keep the snapshot
        // separate because source and destination may name the same A register.
        const auto result = dau_f1(op);
        const auto y_source = state_.a[src];
        state_.a[op_d(op)] = result;
        state_.y = std::bit_cast<std::int32_t>(static_cast<std::uint32_t>(y_source));
        if (from_cache && !(cache_ptr_ == cache_limit_ && cache_iterations_ == 1)) {
            increment_pt(op);
            state_.x = static_cast<std::int16_t>(rom_data_);
        } else {
            rom_data_ = read_program(state_.pt);
            phase_ = Phase::Op2;
            cost = 2;
        }
        break;
    }
    case 0x1a: {
        const bool condition = op_dau_con(op, true);
        set_predicate(condition ? pred_true_ : pred_false_);
        if (op_d(op)) {
            phase_ = Phase::Op2;
            cost = 2;
        }
        break;
    }
    case 0x1d:
        state_.a[op_d(op)] = dau_f1(op);
        state_.temp = static_cast<std::int16_t>(state_.y >> 16);
        dau_set_y(op, yaau_read(op));
        rom_data_ = read_program(state_.pt);
        phase_ = Phase::Op2;
        cost = 2;
        break;
    case 0x1e: throw std::runtime_error("DSP16A reserved opcode group");
    case 0x1f:
        state_.a[op_d(op)] = dau_f1(op);
        dau_set_y_high(yaau_read(op));
        if (from_cache && !(cache_ptr_ == cache_limit_ && cache_iterations_ == 1)) {
            increment_pt(op);
            state_.x = static_cast<std::int16_t>(rom_data_);
        } else {
            rom_data_ = read_program(state_.pt);
            phase_ = Phase::Op2;
            cost = 2;
        }
        break;
    default: throw std::runtime_error("DSP16A invalid opcode group");
    }
    if (flags_ & pred_mask_) {
        // Predicates are consumed by branch/conditional instructions only.
        if (group != 0x1a) set_predicate(pred_none_);
    }
    if (from_cache && (phase_ == Phase::Purge)) phase_ = Phase::Op1;
    if (bus_.instruction_trace)
        bus_.instruction_trace(Dsp16ATracePhase::After, instruction_pc_, op, state_);
    return cost;
}
