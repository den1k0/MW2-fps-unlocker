#include "patcher.h"

#include "log.h"
#include "memory.h"

bool Patcher::Add(const std::string& name, uintptr_t address,
                  const std::vector<uint8_t>& patched, const std::vector<bool>& mask) {
    if (address == 0 || patched.empty() || patched.size() != mask.size()) {
        return false;
    }

    Entry entry;
    entry.name = name;
    entry.address = address;
    entry.patched = patched;
    entry.original.resize(patched.size());

    // Capture the bytes we are about to overwrite.
    if (!meml::Read(address, entry.original.data(), entry.original.size())) {
        mwlog::Line("patcher: cannot read %zu bytes at 0x%llX for '%s'", entry.original.size(),
                    static_cast<unsigned long long>(address), name.c_str());
        return false;
    }

    // Preserve original bytes at wildcard positions so a partial patch is
    // represented as a full compare-and-write.
    for (size_t i = 0; i < entry.patched.size(); ++i) {
        if (!mask[i]) {
            entry.patched[i] = entry.original[i];
        }
    }

    entries_.push_back(std::move(entry));
    return true;
}

bool Patcher::ApplyAll() {
    bool allOk = true;
    for (Entry& entry : entries_) {
        if (!meml::WriteProtected(entry.address, entry.patched.data(), entry.patched.size())) {
            mwlog::Line("patcher: failed to apply '%s' at 0x%llX", entry.name.c_str(),
                        static_cast<unsigned long long>(entry.address));
            allOk = false;
            continue;
        }
        mwlog::Line("patcher: applied '%s' (%zu bytes at 0x%llX)", entry.name.c_str(),
                    entry.patched.size(), static_cast<unsigned long long>(entry.address));
    }
    applied_ = true;
    return allOk;
}

void Patcher::RestoreAll() {
    for (Entry& entry : entries_) {
        if (meml::WriteProtected(entry.address, entry.original.data(), entry.original.size())) {
            mwlog::Line("patcher: restored '%s' at 0x%llX", entry.name.c_str(),
                        static_cast<unsigned long long>(entry.address));
        } else {
            mwlog::Line("patcher: failed to restore '%s' at 0x%llX", entry.name.c_str(),
                        static_cast<unsigned long long>(entry.address));
        }
    }
    applied_ = false;
}

bool Patcher::ReapplyChanged() {
    if (!applied_) {
        return false;
    }

    bool rewroteAnything = false;
    for (Entry& entry : entries_) {
        if (entry.patched.empty()) {
            continue;
        }

        std::vector<uint8_t> current(entry.patched.size(), 0);
        if (!meml::Read(entry.address, current.data(), current.size())) {
            continue;
        }
        if (current == entry.patched) {
            continue; // still holds our value, nothing to do
        }

        if (meml::WriteProtected(entry.address, entry.patched.data(), entry.patched.size())) {
            mwlog::Line("patcher: re-applied '%s' at 0x%llX (the game had reset it)",
                        entry.name.c_str(), static_cast<unsigned long long>(entry.address));
            rewroteAnything = true;
        }
    }
    return rewroteAnything;
}

bool Patcher::Toggle() {
    if (applied_) {
        RestoreAll();
        return false;
    }
    ApplyAll();
    return true;
}
