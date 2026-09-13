#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Byte-signature ("AOB") scanning. This is what removes the dependency on
// hard-coded 32-bit addresses: we locate the code by its shape at run time.
namespace pattern {

// Parse an IDA-style signature such as "48 8B 05 ?? ?? ?? ?? 48 85 C0".
// Tokens: two hex digits = exact byte, "??"/"?"/"*" = wildcard.
// `bytes` holds the concrete byte (0 for wildcards) and `mask` is true where
// the byte must match.
bool Parse(const std::string& text, std::vector<uint8_t>& bytes, std::vector<bool>& mask);

// First match of a parsed signature inside [start, start + size), or 0.
uintptr_t Find(uintptr_t start, size_t size, const std::vector<uint8_t>& bytes,
               const std::vector<bool>& mask);

// Parse + scan a whole module. `moduleName` may be null/empty for the host exe.
uintptr_t FindInModule(const wchar_t* moduleName, const std::string& signature);
uintptr_t FindInModule(const char* moduleName, const std::string& signature);

// Resolve a RIP-relative disp32 operand to an absolute address, x64 style:
//     target = instructionAddress + instructionLength + disp32
// `displacementOffset` is the byte offset of the 4-byte displacement inside the
// instruction (relative to `instructionAddress`).
uintptr_t ResolveRipRelative(uintptr_t instructionAddress, size_t displacementOffset,
                             size_t instructionLength);

} // namespace pattern
