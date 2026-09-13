#pragma once

#include <string>

// High-level feature engine. Reads the config, resolves every enabled feature
// by signature and turns it into a patch.
namespace features {

// Load configuration + build the feature list. Safe to call once.
void Init(const std::wstring& configPath);

// Scan and apply every enabled feature. Returns true if at least one patch was
// applied. On failure, LastError() explains why.
bool Apply();

// Restore all original bytes.
void Restore();

// Toggle. Returns the resulting state (true == applied).
bool Toggle();

bool Applied();

// Re-write any value the game has reset since we applied it. Some cvars are
// re-initialised by the engine (cg_fov when a level loads, for instance), so a
// watchdog can call this periodically. Cheap: it only reads until something
// has actually changed.
void KeepApplied();

// Human-readable description of the last failure (empty on success).
const char* LastError();

} // namespace features
