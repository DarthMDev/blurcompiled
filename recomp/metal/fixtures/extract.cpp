// SPDX-License-Identifier: GPL-3.0-or-later
// Decode only the user's supported XEX. No game execution or GPU initialization.
#include <rex/runtime.h>
#include <rex/kernel/init.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string_view>
int main(int argc, char** argv) {
  if (argc != 5) return 1;
  uint32_t rva = 0, size = 0;
  auto number = [](const char* str, uint32_t& value) {
    std::string_view s(str);
    auto result = std::from_chars(s.data(), s.data() + s.size(), value, 16);
    return result.ec == std::errc{} && result.ptr == s.data() + s.size();
  };
  if (!number(argv[2], rva) || !number(argv[3], size) || !size || size > 4096 ||
      rva > std::numeric_limits<uint32_t>::max() - 0x82000000u) return 2;
  rex::Runtime runtime(argv[1]);
  if (runtime.Setup({.kernel_init = rex::kernel::InitializeKernel, .tool_mode = true}) != 0 ||
      runtime.LoadXexImage("game:\\default.xex") != 0) return 3;
  auto module = runtime.kernel_state()->GetExecutableModule()->xex_module();
  const uint32_t address = 0x82000000u + rva;
  for (const auto& section : module->binary_sections()) {
    if (address < section.virtual_address) continue;
    uint32_t offset = address - section.virtual_address;
    if (offset > section.virtual_size || size > section.virtual_size - offset) continue;
    std::ofstream output(argv[4], std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(section.host_data + offset), size);
    return output ? 0 : 4;
  }
  return 5;
}
