#pragma once

#include <string>

#include "ipc.h"

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

// Apply values pushed from the launcher window, without re-reading the config.
//
// Features are located once and cached, so this only rewrites the words that
// changed - which is what allows a slider to be dragged while the game runs.
// A feature the config never mentioned is built on demand from its preset, so
// the window can switch on something the file did not ship.
//
// Returns true if anything was written or reverted.
bool ApplyLive(const ipc::Values& values);

// Human-readable description of the last failure (empty on success).
const char* LastError();

} // namespace features
