#include "memory.h"

#include <cstring>

namespace {

// A protection value is "readable" if it grants at least one of the read bits.
bool IsReadableProtection(DWORD protect) {
    if (protect & (PAGE_NOACCESS | PAGE_GUARD)) {
        return false;
    }
    switch (protect & 0xFF) {
        case PAGE_READONLY:
        case PAGE_READWRITE:
        case PAGE_WRITECOPY:
        case PAGE_EXECUTE_READ:
        case PAGE_EXECUTE_READWRITE:
        case PAGE_EXECUTE_WRITECOPY:
            return true;
        default:
            return false;
    }
}

bool ResolveModuleRange(HMODULE module, uintptr_t& base, size_t& size) {
    if (module == nullptr) {
        return false;
    }

    base = reinterpret_cast<uintptr_t>(module);

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    // Under a 64-bit target IMAGE_NT_HEADERS resolves to the 64-bit variant.
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    size = nt->OptionalHeader.SizeOfImage;
    return true;
}

} // namespace

bool meml::GetModuleRange(const wchar_t* name, uintptr_t& base, size_t& size) {
    HMODULE module = nullptr;
    if (name == nullptr || name[0] == L'\0') {
        module = ::GetModuleHandleW(nullptr); // host executable
    } else {
        module = ::GetModuleHandleW(name);
    }
    return ResolveModuleRange(module, base, size);
}

bool meml::GetModuleRange(const char* name, uintptr_t& base, size_t& size) {
    if (name == nullptr || name[0] == '\0') {
        return GetModuleRange(static_cast<const wchar_t*>(nullptr), base, size);
    }

    wchar_t wide[MAX_PATH] = {};
    const int written = ::MultiByteToWideChar(CP_UTF8, 0, name, -1, wide, MAX_PATH);
    if (written <= 0) {
        return false;
    }
    return GetModuleRange(wide, base, size);
}

bool meml::IsReadable(uintptr_t address, size_t size) {
    if (address == 0 || size == 0) {
        return false;
    }

    uintptr_t current = address;
    const uintptr_t end = address + size;

    while (current < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (::VirtualQuery(reinterpret_cast<LPCVOID>(current), &mbi, sizeof(mbi)) == 0) {
            return false;
        }
        if (mbi.State != MEM_COMMIT || !IsReadableProtection(mbi.Protect)) {
            return false;
        }

        const uintptr_t regionEnd =
            reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd <= current) {
            return false; // malformed query, avoid an infinite loop
        }
        current = regionEnd;
    }

    return true;
}

bool meml::Read(uintptr_t address, void* buffer, size_t size) {
    if (!IsReadable(address, size)) {
        return false;
    }
    std::memcpy(buffer, reinterpret_cast<const void*>(address), size);
    return true;
}

bool meml::Write(uintptr_t address, const void* buffer, size_t size) {
    if (!IsReadable(address, size)) {
        return false;
    }
    std::memcpy(reinterpret_cast<void*>(address), buffer, size);
    return true;
}

bool meml::WriteProtected(uintptr_t address, const void* buffer, size_t size) {
    if (address == 0 || size == 0) {
        return false;
    }

    DWORD oldProtect = 0;
    if (!::VirtualProtect(reinterpret_cast<LPVOID>(address), size, PAGE_EXECUTE_READWRITE,
                          &oldProtect)) {
        return false;
    }

    std::memcpy(reinterpret_cast<void*>(address), buffer, size);

    DWORD ignored = 0;
    ::VirtualProtect(reinterpret_cast<LPVOID>(address), size, oldProtect, &ignored);
    ::FlushInstructionCache(::GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), size);
    return true;
}

void meml::ForEachReadableRegion(uintptr_t start, size_t size, RegionCallback callback,
                                 void* user) {
    if (start == 0 || size == 0 || callback == nullptr) {
        return;
    }

    const uintptr_t end = start + size;
    uintptr_t current = start;

    while (current < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (::VirtualQuery(reinterpret_cast<LPCVOID>(current), &mbi, sizeof(mbi)) == 0) {
            break;
        }

        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(mbi.BaseAddress);
        const uintptr_t regionEnd = regionBase + mbi.RegionSize;

        if (mbi.State == MEM_COMMIT && IsReadableProtection(mbi.Protect)) {
            const uintptr_t scanStart = regionBase > current ? regionBase : current;
            const uintptr_t scanEnd = regionEnd < end ? regionEnd : end;
            if (scanEnd > scanStart) {
                if (!callback(scanStart, static_cast<size_t>(scanEnd - scanStart), user)) {
                    return;
                }
            }
        }

        if (regionEnd <= current) {
            break; // defensive: never loop forever
        }
        current = regionEnd < end ? regionEnd : end;
    }
}
