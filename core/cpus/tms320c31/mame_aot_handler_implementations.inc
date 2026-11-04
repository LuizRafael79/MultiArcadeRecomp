bool Interpreter::aot_execute_absf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    absf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_absf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    absf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_absf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    absf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_absf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    absf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_absfstf(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    absfstf(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_absi_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    absi_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_absi_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    absi_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_absi_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    absi_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_absi_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    absi_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_absisti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    absisti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addc3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addc3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addc3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addc3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addc3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addc3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addc3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addc3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addc_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addc_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addc_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addc_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addc_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addc_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addc_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addc_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addf3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addf3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addf3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addf3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addf3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addf3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addf3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addf3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addf3stf(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addf3stf(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addi3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addi3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addi3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addi3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addi3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addi3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addi3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addi3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addi3sti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addi3sti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addi_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addi_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addi_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addi_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addi_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addi_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_addi_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    addi_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_and3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    and3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_and3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    and3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_and3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    and3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_and3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    and3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_and3sti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    and3sti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_and_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    and_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_and_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    and_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_and_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    and_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_and_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    and_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_andn3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    andn3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_andn3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    andn3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_andn3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    andn3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_andn3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    andn3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_andn_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    andn_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_andn_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    andn_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_andn_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    andn_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_andn_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    andn_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ash3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ash3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ash3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ash3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ash3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ash3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ash3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ash3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ash3sti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ash3sti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ash_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ash_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ash_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ash_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ash_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ash_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ash_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ash_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_br_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    br_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_brc_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    brc_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_brc_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    brc_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_brcd_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    brcd_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_brcd_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    brcd_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_brd_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    brd_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_call_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    call_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_callc_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    callc_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_callc_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    callc_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpf3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpf3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpf3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpf3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpf3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpf3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpf3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpf3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpi3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpi3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpi3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpi3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpi3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpi3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpi3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpi3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpi_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpi_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpi_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpi_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpi_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpi_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_cmpi_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    cmpi_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_dbc_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    dbc_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_dbc_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    dbc_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_dbcd_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    dbcd_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_dbcd_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    dbcd_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_fix_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    fix_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_fix_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    fix_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_fix_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    fix_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_fix_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    fix_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_fixsti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    fixsti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_float_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    float_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_float_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    float_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_float_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    float_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_float_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    float_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_floatstf(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    floatstf(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_iack_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    iack_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_iack_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    iack_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_idle(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    idle(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_lde_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    lde_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_lde_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    lde_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_lde_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    lde_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_lde_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    lde_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfeq_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfeq_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfeq_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfeq_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfeq_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfeq_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfeq_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfeq_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfge_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfge_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfge_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfge_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfge_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfge_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfge_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfge_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfgt_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfgt_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfgt_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfgt_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfgt_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfgt_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfgt_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfgt_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfhi_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfhi_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfhi_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfhi_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfhi_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfhi_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfhi_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfhi_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfhs_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfhs_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfhs_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfhs_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfhs_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfhs_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfhs_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfhs_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfi_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfi_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfi_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfi_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfldf(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfldf(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfle_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfle_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfle_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfle_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfle_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfle_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfle_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfle_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldflo_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldflo_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldflo_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldflo_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldflo_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldflo_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldflo_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldflo_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfls_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfls_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfls_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfls_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfls_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfls_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfls_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfls_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldflt_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldflt_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldflt_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldflt_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldflt_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldflt_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldflt_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldflt_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfluf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfluf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfluf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfluf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfluf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfluf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfluf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfluf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldflv_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldflv_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldflv_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldflv_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldflv_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldflv_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldflv_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldflv_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfne_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfne_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfne_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfne_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfne_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfne_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfne_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfne_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnluf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnluf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnluf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnluf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnluf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnluf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnluf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnluf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnlv_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnlv_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnlv_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnlv_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnlv_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnlv_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnlv_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnlv_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnuf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnuf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnuf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnuf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnuf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnuf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnuf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnuf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnv_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnv_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnv_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnv_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnv_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnv_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfnv_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfnv_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfstf(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfstf(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfu_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfu_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfu_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfu_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfu_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfu_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfu_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfu_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfuf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfuf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfuf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfuf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfuf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfuf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfuf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfuf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfv_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfv_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfv_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfv_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfv_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfv_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfv_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfv_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfzuf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfzuf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfzuf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfzuf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfzuf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfzuf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldfzuf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldfzuf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldi_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldi_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldi_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldi_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldi_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldi_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldi_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldi_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldieq_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldieq_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldieq_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldieq_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldieq_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldieq_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldieq_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldieq_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldige_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldige_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldige_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldige_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldige_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldige_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldige_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldige_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldigt_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldigt_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldigt_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldigt_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldigt_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldigt_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldigt_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldigt_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldihi_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldihi_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldihi_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldihi_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldihi_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldihi_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldihi_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldihi_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldihs_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldihs_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldihs_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldihs_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldihs_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldihs_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldihs_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldihs_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldii_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldii_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldii_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldii_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldildi(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldildi(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldile_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldile_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldile_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldile_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldile_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldile_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldile_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldile_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldilo_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldilo_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldilo_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldilo_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldilo_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldilo_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldilo_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldilo_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldils_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldils_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldils_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldils_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldils_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldils_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldils_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldils_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldilt_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldilt_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldilt_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldilt_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldilt_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldilt_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldilt_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldilt_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiluf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiluf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiluf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiluf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiluf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiluf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiluf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiluf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldilv_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldilv_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldilv_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldilv_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldilv_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldilv_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldilv_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldilv_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldine_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldine_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldine_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldine_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldine_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldine_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldine_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldine_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinluf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinluf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinluf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinluf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinluf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinluf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinluf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinluf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinlv_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinlv_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinlv_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinlv_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinlv_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinlv_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinlv_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinlv_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinuf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinuf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinuf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinuf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinuf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinuf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinuf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinuf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinv_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinv_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinv_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinv_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinv_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinv_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldinv_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldinv_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldisti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldisti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiu_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiu_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiu_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiu_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiu_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiu_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiu_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiu_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiuf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiuf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiuf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiuf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiuf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiuf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiuf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiuf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiv_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiv_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiv_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiv_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiv_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiv_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldiv_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldiv_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldizuf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldizuf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldizuf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldizuf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldizuf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldizuf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldizuf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldizuf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldm_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldm_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldm_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldm_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldm_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldm_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ldm_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ldm_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_lsh3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    lsh3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_lsh3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    lsh3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_lsh3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    lsh3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_lsh3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    lsh3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_lsh3sti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    lsh3sti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_lsh_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    lsh_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_lsh_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    lsh_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_lsh_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    lsh_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_lsh_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    lsh_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_maxspeed(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    maxspeed(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyaddf_0(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyaddf_0(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyaddf_1(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyaddf_1(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyaddf_2(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyaddf_2(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyaddf_3(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyaddf_3(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyaddi_0(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyaddi_0(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyaddi_1(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyaddi_1(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyaddi_2(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyaddi_2(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyaddi_3(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyaddi_3(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyf3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyf3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyf3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyf3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyf3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyf3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyf3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyf3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyf3stf(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyf3stf(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyi3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyi3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyi3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyi3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyi3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyi3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyi3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyi3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyi3sti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyi3sti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyi_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyi_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyi_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyi_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyi_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyi_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpyi_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpyi_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpysubf_0(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpysubf_0(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpysubf_1(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpysubf_1(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpysubf_2(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpysubf_2(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpysubf_3(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpysubf_3(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpysubi_0(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpysubi_0(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpysubi_1(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpysubi_1(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpysubi_2(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpysubi_2(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_mpysubi_3(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    mpysubi_3(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negb_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negb_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negb_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negb_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negb_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negb_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negb_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negb_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negfstf(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negfstf(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negi_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negi_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negi_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negi_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negi_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negi_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negi_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negi_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_negisti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    negisti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_nop_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    nop_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_nop_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    nop_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_norm_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    norm_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_norm_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    norm_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_norm_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    norm_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_norm_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    norm_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_not_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    not_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_not_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    not_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_not_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    not_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_not_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    not_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_notsti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    notsti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_or3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    or3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_or3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    or3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_or3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    or3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_or3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    or3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_or3sti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    or3sti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_or_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    or_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_or_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    or_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_or_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    or_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_or_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    or_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_pop(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    pop(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_popf(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    popf(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_push(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    push(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_pushf(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    pushf(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_retic_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    retic_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_retsc_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    retsc_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_rnd_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    rnd_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_rnd_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    rnd_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_rnd_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    rnd_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_rnd_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    rnd_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_rol(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    rol(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_rolc(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    rolc(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_ror(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    ror(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_rorc(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    rorc(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_rptb_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    rptb_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_rpts_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    rpts_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_rpts_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    rpts_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_rpts_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    rpts_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_rpts_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    rpts_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_sigi(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    sigi(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_stf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    stf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_stf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    stf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_stfi_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    stfi_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_stfi_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    stfi_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_stfstf(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    stfstf(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_sti_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    sti_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_sti_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    sti_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_stii_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    stii_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_stii_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    stii_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_stisti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    stisti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subb3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subb3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subb3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subb3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subb3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subb3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subb3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subb3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subb_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subb_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subb_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subb_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subb_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subb_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subb_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subb_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subc_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subc_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subc_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subc_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subc_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subc_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subc_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subc_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subf3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subf3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subf3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subf3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subf3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subf3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subf3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subf3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subf3stf(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subf3stf(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subi3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subi3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subi3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subi3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subi3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subi3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subi3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subi3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subi3sti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subi3sti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subi_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subi_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subi_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subi_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subi_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subi_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subi_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subi_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subrb_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subrb_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subrb_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subrb_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subrb_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subrb_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subrb_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subrb_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subrf_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subrf_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subrf_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subrf_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subrf_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subrf_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subrf_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subrf_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subri_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subri_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subri_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subri_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subri_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subri_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_subri_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    subri_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_swi(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    swi(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_trapc(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    trapc(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_tstb3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    tstb3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_tstb3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    tstb3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_tstb3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    tstb3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_tstb3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    tstb3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_tstb_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    tstb_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_tstb_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    tstb_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_tstb_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    tstb_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_tstb_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    tstb_reg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_xor3_indind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    xor3_indind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_xor3_indreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    xor3_indreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_xor3_regind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    xor3_regind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_xor3_regreg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    xor3_regreg(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_xor3sti(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    xor3sti(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_xor_dir(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    xor_dir(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_xor_imm(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    xor_imm(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_xor_ind(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    xor_ind(expected_opcode);
    return aot_end(expected_pc);
}
bool Interpreter::aot_execute_xor_reg(std::uint32_t expected_pc, std::uint32_t expected_opcode) noexcept {
    if (!aot_begin(expected_pc, expected_opcode)) return false;
    xor_reg(expected_opcode);
    return aot_end(expected_pc);
}
