#pragma once

#include <string>

namespace mamerecomp::cps1::detail {

void cps1_eeprom_set_path(std::string path);
void cps1_eeprom_reset();
bool cps1_eeprom_read_do();
void cps1_eeprom_write_lines(unsigned data);
bool cps1_eeprom_save();

} // namespace mamerecomp::cps1::detail
