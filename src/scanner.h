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

// Case-insensitive search for a null-terminated ASCII name inside
// [start, start + size). Returns the address of the string, or 0.
//
// Cvar capitalisation is inconsistent and sources disagree: this build uses
// "cg_drawFPS", while every guide writes "cg_drawfps". Since a cvar is located
// by its exact bytes, getting the case wrong would silently break a feature -
// so name lookups go through here rather than the byte-exact scanner.
uintptr_t FindInsensitive(uintptr_t start, size_t size, const std::string& name);

// Every place the name appears as a whole null-terminated string, best
// candidate first.
//
// One match is not enough to identify a cvar. This build holds "Sensitivity"
// (a profile field label) as well as the cvar "sensitivity", so a
// case-insensitive first-match search returns the label, finds nothing pointing
// at it, and gives up - which looks exactly like a cvar that does not exist.
// The caller has to be able to try the next candidate, so all of them are
// returned, ranked:
//
//   * an exact-case match that also starts a string is what a cvar name is
//   * the same match with different capitalisation still counts, because
//     guides disagree with the binary about case
//   * a match inside a longer string is ranked last, since a name that does
//     not begin its string cannot be the name of anything
void FindCandidates(uintptr_t start, size_t size, const std::string& name,
                    std::vector<uintptr_t>& out);

// Resolve a RIP-relative disp32 operand to an absolute address, x64 style:
//     target = instructionAddress + instructionLength + disp32
// `displacementOffset` is the byte offset of the 4-byte displacement inside the
// instruction (relative to `instructionAddress`).
uintptr_t ResolveRipRelative(uintptr_t instructionAddress, size_t displacementOffset,
                             size_t instructionLength);

} // namespace pattern
