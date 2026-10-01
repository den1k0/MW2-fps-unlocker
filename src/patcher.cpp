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

bool Patcher::AddOrUpdate(const std::string& name, uintptr_t address,
                          const std::vector<uint8_t>& patched,
                          const std::vector<bool>& mask) {
    if (address == 0 || patched.empty() || patched.size() != mask.size()) {
        return false;
    }

    bool found = false;
    for (Entry& entry : entries_) {
        if (entry.address != address || entry.name != name) {
            continue;
        }

        // Deliberately not re-capturing `original`: it describes the process
        // before any of our writes, not before the previous value. Overwriting
        // it here would make a restore put back our own earlier value instead of
        // the game's.
        entry.patched = patched;
        for (size_t i = 0; i < entry.patched.size() && i < entry.original.size(); ++i) {
            if (!mask[i]) {
                entry.patched[i] = entry.original[i];
            }
        }
        entry.active = true;
        found = true;

        if (applied_) {
            if (!meml::WriteProtected(entry.address, entry.patched.data(), entry.patched.size())) {
                mwlog::Line("patcher: failed to update '%s' at 0x%llX", name.c_str(),
                            static_cast<unsigned long long>(address));
                return false;
            }
            mwlog::Line("patcher: updated '%s' at 0x%llX", name.c_str(),
                        static_cast<unsigned long long>(address));
        }
        break;
    }

    if (!found) {
        if (!Add(name, address, patched, mask)) {
            return false;
        }
        // Add() only registers; the write happens on the next ApplyAll, which is
        // what the initial apply wants.
        return true;
    }
    return true;
}

bool Patcher::Restore(const std::string& name) {
    bool found = false;
    for (Entry& entry : entries_) {
        if (entry.name != name) {
            continue;
        }
        found = true;
        entry.active = false;

        if (!applied_ || entry.original.empty()) {
            continue;
        }
        if (meml::WriteProtected(entry.address, entry.original.data(), entry.original.size())) {
            mwlog::Line("patcher: reverted '%s' at 0x%llX (switched off)", entry.name.c_str(),
                        static_cast<unsigned long long>(entry.address));
        } else {
            mwlog::Line("patcher: failed to revert '%s' at 0x%llX", entry.name.c_str(),
                        static_cast<unsigned long long>(entry.address));
        }
    }
    return found;
}

bool Patcher::ApplyAll() {
    bool allOk = true;
    for (Entry& entry : entries_) {
        if (!entry.active) {
            continue; // switched off by the caller; leave the original in place
        }
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
        if (!entry.active || entry.patched.empty()) {
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
