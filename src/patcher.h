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

    // Add a patch, or replace the bytes of the one already registered under this
    // name and address. Replacing keeps the *original* bytes captured the first
    // time, so a later restore still puts the process back exactly as it was
    // found - which is what makes a live slider safe to drag repeatedly.
    //
    // If the patcher is currently applied the new bytes are written immediately;
    // if it is toggled off they are only remembered, so a slider cannot quietly
    // switch the tool back on.
    bool AddOrUpdate(const std::string& name, uintptr_t address,
                     const std::vector<uint8_t>& patched, const std::vector<bool>& mask);

    bool ApplyAll();
    void RestoreAll();

    // Revert one labelled patch and mark it inactive, so the watchdog does not
    // immediately put back the value that was just switched off.
    bool Restore(const std::string& name);

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
        // False once the caller has switched this particular patch off. The
        // entry stays registered so it can be switched back on, and so a later
        // RestoreAll still knows what the original bytes were.
        bool active = true;
    };

    std::vector<Entry> entries_;
    bool applied_ = false;
};
