#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Records original bytes so a patch can be reverted (toggle off, clean unload).
class Patcher {
public:
    // Register a patch writing `patched` bytes at `address`. Only positions
    // where mask[i] is true are written; others keep their original value.
    // The original bytes are captured immediately.
    bool Add(const std::string& name, uintptr_t address,
             const std::vector<uint8_t>& patched, const std::vector<bool>& mask);

    bool ApplyAll();
    void RestoreAll();

    // Re-write any entry whose patched bytes are no longer in place. The game
    // can legitimately reset some of them (cg_fov is re-initialised when a level
    // loads), so a watchdog calls this periodically to keep our values applied.
    // Returns true if anything had to be rewritten.
    bool ReapplyChanged();

    // Returns the new state: true if patches are now applied, false if reverted.
    bool Toggle();

    bool IsApplied() const { return applied_; }
    size_t Count() const { return entries_.size(); }

private:
    struct Entry {
        std::string name;
        uintptr_t address = 0;
        std::vector<uint8_t> original;
        std::vector<uint8_t> patched;
    };

    std::vector<Entry> entries_;
    bool applied_ = false;
};
