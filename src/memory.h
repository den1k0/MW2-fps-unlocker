#pragma once

#include <windows.h>

#include <cstddef>
#include <cstdint>

// Small self-process memory helper. Everything here operates on the process the
// DLL is injected into (the game), which is why we can use VirtualQuery and a
// plain memcpy instead of ReadProcessMemory.
namespace meml {

// Resolve a loaded module's image base and size (SizeOfImage, i.e. the full
// mapped range including .data/.rdata, not just committed file size).
//
// `name` may be null or empty, in which case the host executable is used.
bool GetModuleRange(const wchar_t* name, uintptr_t& base, size_t& size);
bool GetModuleRange(const char* name, uintptr_t& base, size_t& size);

// True if every page in [address, address+size) is committed and readable.
bool IsReadable(uintptr_t address, size_t size);

// Safe reads/writes of already-readable memory.
bool Read(uintptr_t address, void* buffer, size_t size);
bool Write(uintptr_t address, const void* buffer, size_t size);

// Write through an explicit VirtualProtect(PAGE_EXECUTE_READWRITE) and flush
// the instruction cache afterwards. Use this for code patches.
bool WriteProtected(uintptr_t address, const void* buffer, size_t size);

// Walk [start, start+size) and invoke `callback` for every committed, readable,
// non-guard region. The callback receives the region start and length and
// returns false to stop the walk early.
using RegionCallback = bool (*)(uintptr_t regionStart, size_t regionSize, void* user);
void ForEachReadableRegion(uintptr_t start, size_t size, RegionCallback callback, void* user);

} // namespace meml
