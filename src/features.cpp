#include "features.h"

#include "config.h"
#include "log.h"
#include "memory.h"
#include "patcher.h"
#include "scanner.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

// -----------------------------------------------------------------------------
// Presets: the friendly face of the config.
//
// Users should be able to write `[fps] value=250` and nothing else. Everything
// mechanical - which cvar, whether it is an int or a float, which offsets hold
// the value - is built in here.
//
// Note there is no hard-coded address anywhere in a preset. The cvar is found
// by scanning the module for its null-terminated name, so this works in both
// iw4mp.exe and iw4sp.exe, and re-locates correctly after every launch.
// -----------------------------------------------------------------------------
struct DvarPreset {
    const char* section;       // section name, matched case-insensitively
    const char* cvar;          // cvar to locate by name
    bool isFloat;              // cg_fov is a float, com_maxfps is an int
    const char* valueOffsets;  // dvar offsets holding the live value
    const char* defaultValue;  // used when `value=` is absent
    const char* clampOffset;   // "" when the dvar has no clamp worth raising
    const char* clampKey;      // config key that overrides the clamp
};

const DvarPreset kPresets[] = {
    // com_maxfps: the multiplayer frame cap. An INTEGER defaulting to 85, which
    // is why no float search ever found it.
    {"fps", "com_maxfps", false, "0x10,0x20,0x30", "1000", "", ""},
    // cg_fov: a FLOAT. The float at dvar+0x44 reads 80.0 and behaves like the
    // max-FOV clamp, so it is exposed as the optional `max=` key.
    {"fov", "cg_fov", true, "0x10,0x20,0x30", "90", "0x44", "max"},
    // cg_drawFPS: the engine's own frame counter, and an enum rather than a
    // flag: 0 = Off, 1 = Simple, 2 = SimpleRanges, 3 = Verbose,
    // 4 = Verbose+Viewpos.
    //
    // Single player only. iw4sp.exe reads this cvar from six places; iw4mp.exe
    // registers it and then never reads it, so in multiplayer the value is
    // written and ignored and there is no on-screen counter to enable. That is
    // measured, not assumed - see "cg_drawFPS is dead in the multiplayer
    // build" in docs/finding-signatures.md. The config ships it disabled.
    //
    // The capital FPS is the binary's spelling rather than a typo, which is
    // why name lookups are case-insensitive.
    {"drawfps", "cg_drawFPS", false, "0x10,0x20,0x30", "1", "", ""},
    // sv_network_fps: the one debug counter the multiplayer client still reads.
    // It reports the network/snapshot rate, not the render frame rate - a much
    // lower number, and the one the old "91" cap was actually about. It is the
    // only counter multiplayer has, so it is offered even though it is not the
    // figure a frame-rate cap change would show up in.
    {"netfps", "sv_network_fps", false, "0x10,0x20,0x30", "1", "", ""},
};

const DvarPreset* FindPreset(const std::string& section) {
    for (const DvarPreset& preset : kPresets) {
        if (_stricmp(preset.section, section.c_str()) == 0) {
            return &preset;
        }
    }
    return nullptr;
}

// One configurable patch site.
struct Feature {
    std::string name;
    // "bytes" | "float" | "float_ptr" | "dvar_int" | "dvar_float"
    std::string type;

    // Empty means: use the global gameExe, or the host executable.
    std::wstring module;

    // ---- signature (AOB) driven types ------------------------------------
    std::string signature;
    uintptr_t matchOffset = 0;

    std::vector<uint8_t> patchBytes;
    std::vector<bool> patchMask;

    float value = 0.0f;
    int intValue = 0;

    // "float_ptr"
    uintptr_t displacementOffset = 0;
    size_t instructionLength = 0;

    // ---- dvar driven types ------------------------------------------------
    // Preferred: the cvar name, located by scanning for its string.
    std::string cvarName;
    // Alternative for advanced users: a known RVA of the name string.
    uintptr_t nameStringRva = 0;
    // Offsets inside the dvar_t to write, e.g. "0x10,0x20,0x30".
    std::vector<uintptr_t> valueOffsets;

    // Set when this feature came from a preset (used to expand `max=`).
    const DvarPreset* preset = nullptr;
};

Config g_config;
Patcher g_patcher;
std::vector<Feature> g_features;
std::wstring g_gameModule; // empty => host executable
bool g_applied = false;
std::string g_error;

// Locating a dvar costs a full module scan, and two features can target the
// same cvar (cg_fov and its clamp), so remember what we found.
std::map<std::string, uintptr_t> g_dvarCache;

uintptr_t ParseUInt(const std::string& text) {
    if (text.empty()) {
        return 0;
    }
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 0);
    if (end == text.c_str()) {
        return 0;
    }
    return static_cast<uintptr_t>(parsed);
}

std::wstring Widen(const std::string& text) {
    if (text.empty()) {
        return std::wstring();
    }
    const int length = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, nullptr, 0);
    if (length <= 1) {
        return std::wstring();
    }
    // `length` includes the terminating null; size the buffer accordingly and
    // trim the null afterwards.
    std::wstring wide(static_cast<size_t>(length), L'\0');
    const int written = ::MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, wide.data(), length);
    if (written <= 0) {
        return std::wstring();
    }
    wide.resize(static_cast<size_t>(written - 1));
    return wide;
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) {
        return std::string();
    }
    const int length =
        ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                             nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return std::string();
    }
    std::string narrow(static_cast<size_t>(length), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()),
                          narrow.data(), length, nullptr, nullptr);
    return narrow;
}

std::vector<uint8_t> FloatBytes(float value) {
    uint8_t raw[sizeof(float)] = {};
    std::memcpy(raw, &value, sizeof(float));
    return std::vector<uint8_t>(raw, raw + sizeof(float));
}

std::vector<uint8_t> Int32Bytes(int value) {
    uint8_t raw[sizeof(int32_t)] = {};
    std::memcpy(raw, &value, sizeof(int32_t));
    return std::vector<uint8_t>(raw, raw + sizeof(int32_t));
}

// Render raw bytes as an IDA-style signature ("B8 C3 11 52 F7 7F 00 00").
std::string BytesToPattern(const uint8_t* data, size_t size) {
    std::ostringstream stream;
    for (size_t i = 0; i < size; ++i) {
        if (i != 0) {
            stream << ' ';
        }
        stream << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
               << static_cast<int>(data[i]);
    }
    return stream.str();
}

std::string Trim(const std::string& value) {
    const char* whitespace = " \t\r\n";
    const size_t first = value.find_first_not_of(whitespace);
    if (first == std::string::npos) {
        return "";
    }
    const size_t last = value.find_last_not_of(whitespace);
    return value.substr(first, last - first + 1);
}

std::vector<uintptr_t> ParseOffsetList(const std::string& text) {
    std::vector<uintptr_t> offsets;
    std::istringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        const std::string trimmed = Trim(token);
        if (!trimmed.empty()) {
            offsets.push_back(ParseUInt(trimmed));
        }
    }
    return offsets;
}

bool IsDvarType(const std::string& type) {
    return type == "dvar_int" || type == "dvar_float";
}

const std::wstring& FeatureModule(const Feature& feature) {
    return feature.module.empty() ? g_gameModule : feature.module;
}

void SetValueFromConfig(const std::string& section, Feature& feature) {
    if (feature.type == "dvar_int") {
        feature.intValue = g_config.GetInt(section, "value", feature.intValue);
        feature.patchBytes = Int32Bytes(feature.intValue);
    } else {
        feature.value = g_config.GetFloat(section, "value", feature.value);
        feature.patchBytes = FloatBytes(feature.value);
    }
    feature.patchMask.assign(feature.patchBytes.size(), true);
}

// Expand a `[fps]` / `[fov]` style preset into a full feature description.
bool BuildFromPreset(const std::string& section, const DvarPreset& preset, Feature& feature) {
    feature.type = preset.isFloat ? "dvar_float" : "dvar_int";
    feature.cvarName = preset.cvar;
    feature.valueOffsets = ParseOffsetList(preset.valueOffsets);
    feature.preset = &preset;

    feature.value = static_cast<float>(std::strtod(preset.defaultValue, nullptr));
    feature.intValue = static_cast<int>(std::strtol(preset.defaultValue, nullptr, 0));
    SetValueFromConfig(section, feature);

    mwlog::Line("features: '%s' uses the built-in preset for '%s' (type %s)", section.c_str(),
                preset.cvar, feature.type.c_str());
    return true;
}

bool BuildFeature(const std::string& section, Feature& feature) {
    feature.name = section;
    feature.module = Widen(g_config.Get(section, "module"));

    // A section with no `type=` is assumed to be a preset, so the common case
    // in the ini is just `[fps] enabled=1 value=250`.
    const bool hasType = g_config.Has(section, "type") || g_config.Has(section, "signature") ||
                         g_config.Has(section, "nameStringRva");
    if (!hasType) {
        const DvarPreset* preset = FindPreset(section);
        if (preset == nullptr) {
            g_error = "section '" + section + "' is not a preset and has no type";
            return false;
        }
        return BuildFromPreset(section, *preset, feature);
    }

    feature.type = g_config.Get(section, "type", "bytes");

    // ---- dvar driven types -------------------------------------------------
    if (IsDvarType(feature.type)) {
        feature.cvarName = Trim(g_config.Get(section, "cvar"));
        feature.nameStringRva = ParseUInt(g_config.Get(section, "nameStringRva", "0"));
        feature.valueOffsets = ParseOffsetList(g_config.Get(section, "valueOffsets"));

        if (feature.nameStringRva == 0 && feature.cvarName.empty()) {
            g_error = "feature '" + section + "' needs cvar (or nameStringRva)";
            return false;
        }
        if (feature.valueOffsets.empty()) {
            g_error = "feature '" + section + "' needs valueOffsets";
            return false;
        }

        SetValueFromConfig(section, feature);
        return true;
    }

    // ---- signature driven types -------------------------------------------
    feature.signature = g_config.Get(section, "signature");
    feature.matchOffset = ParseUInt(g_config.Get(section, "matchOffset", "0"));

    if (feature.signature.empty()) {
        g_error = "feature '" + section + "' has no signature";
        return false;
    }

    if (feature.type == "bytes") {
        const std::string patchText = g_config.Get(section, "patch");
        if (!pattern::Parse(patchText, feature.patchBytes, feature.patchMask)) {
            g_error = "feature '" + section + "' has an invalid patch string";
            return false;
        }
    } else if (feature.type == "float") {
        feature.value = g_config.GetFloat(section, "value", 0.0f);
        feature.patchBytes = FloatBytes(feature.value);
        feature.patchMask.assign(feature.patchBytes.size(), true);
    } else if (feature.type == "float_ptr") {
        feature.value = g_config.GetFloat(section, "value", 0.0f);
        feature.patchBytes = FloatBytes(feature.value);
        feature.patchMask.assign(feature.patchBytes.size(), true);
        feature.displacementOffset = ParseUInt(g_config.Get(section, "dispOffset", "0"));
        feature.instructionLength =
            static_cast<size_t>(ParseUInt(g_config.Get(section, "instrLen", "0")));
        if (feature.instructionLength == 0) {
            g_error = "feature '" + section + "' (float_ptr) needs instrLen";
            return false;
        }
    } else {
        g_error = "feature '" + section + "' has unknown type '" + feature.type + "'";
        return false;
    }

    return true;
}

uintptr_t LocateFeature(const Feature& feature) {
    const std::wstring& module = FeatureModule(feature);
    const uintptr_t match = pattern::FindInModule(module.c_str(), feature.signature);
    if (match == 0) {
        return 0;
    }
    mwlog::Line("features: '%s' signature matched at 0x%llX", feature.name.c_str(),
                static_cast<unsigned long long>(match));

    if (feature.type == "float_ptr") {
        const uintptr_t globalAddress = pattern::ResolveRipRelative(
            match + feature.matchOffset, feature.displacementOffset, feature.instructionLength);
        if (globalAddress == 0) {
            return 0;
        }
        mwlog::Line("features: '%s' resolved RIP-relative global -> 0x%llX",
                    feature.name.c_str(),
                    static_cast<unsigned long long>(globalAddress));
        return globalAddress;
    }

    return match + feature.matchOffset;
}

// How much of a dvar_t to capture in the log. 0x60 covers the whole structure
// as measured on this build: the values written by the presets sit at +0x10,
// +0x20 and +0x30, and the fields after them are what identify the type.
constexpr size_t kDvarDumpSize = 0x60;

std::string HexBytes(const std::vector<uint8_t>& bytes) {
    static const char* digits = "0123456789ABCDEF";
    std::string text;
    text.reserve(bytes.size() * 3);
    for (const uint8_t byte : bytes) {
        if (!text.empty()) {
            text.push_back(' ');
        }
        text.push_back(digits[byte >> 4]);
        text.push_back(digits[byte & 0x0F]);
    }
    return text;
}

// Locate a live dvar_t.
//
// The first field of the structure is a pointer to its own name, so:
//   1. find the null-terminated cvar name inside the module, then
//   2. scan for a pointer to that address.
// Step 1 needs no addresses baked in, which is what makes the presets work in
// both executables and survive every relaunch.
uintptr_t LocateDvar(const Feature& feature) {
    const std::wstring& module = FeatureModule(feature);
    const std::string moduleKey = Narrow(module);

    uintptr_t base = 0;
    size_t size = 0;
    if (!meml::GetModuleRange(module.c_str(), base, size)) {
        mwlog::Line("features: '%s' could not resolve the module range",
                    feature.name.c_str());
        return 0;
    }

    uintptr_t nameAddress = 0;
    std::string cacheKey;

    if (!feature.cvarName.empty()) {
        cacheKey = moduleKey + "|" + feature.cvarName;
        const auto cached = g_dvarCache.find(cacheKey);
        if (cached != g_dvarCache.end()) {
            mwlog::Line("features: '%s' reused the cached dvar for '%s' at 0x%llX",
                        feature.name.c_str(), feature.cvarName.c_str(),
                        static_cast<unsigned long long>(cached->second));
            return cached->second;
        }

        // Case-insensitive, because cvar capitalisation is inconsistent and
        // guides disagree with the binary (cg_drawFPS vs cg_drawfps). Getting
        // the case wrong must not be the reason a feature silently does nothing.
        nameAddress = pattern::FindInsensitive(base, size, feature.cvarName);
        if (nameAddress == 0) {
            mwlog::Line("features: '%s' could not find the cvar named '%s' in the module",
                        feature.name.c_str(), feature.cvarName.c_str());
            return 0;
        }
    } else {
        cacheKey = moduleKey + "|rva:" + std::to_string(feature.nameStringRva);
        nameAddress = base + feature.nameStringRva;
    }

    // Sanity check: read the name back so a wrong match is reported clearly.
    char probe[64] = {};
    if (!meml::Read(nameAddress, probe, sizeof(probe) - 1)) {
        mwlog::Line("features: '%s' name string at 0x%llX is not readable",
                    feature.name.c_str(), static_cast<unsigned long long>(nameAddress));
        return 0;
    }
    probe[sizeof(probe) - 1] = '\0';
    mwlog::Line("features: '%s' cvar name at 0x%llX (rva 0x%llX) reads as '%s'",
                feature.name.c_str(), static_cast<unsigned long long>(nameAddress),
                static_cast<unsigned long long>(nameAddress - base), probe);

    uint8_t raw[sizeof(uintptr_t)] = {};
    std::memcpy(raw, &nameAddress, sizeof(raw));

    std::vector<uint8_t> bytes;
    std::vector<bool> mask;
    if (!pattern::Parse(BytesToPattern(raw, sizeof(raw)), bytes, mask)) {
        return 0;
    }

    const uintptr_t dvar = pattern::Find(base, size, bytes, mask);
    if (dvar == 0) {
        mwlog::Line("features: '%s' no pointer to that name was found in the module",
                    feature.name.c_str());
        return 0;
    }

    mwlog::Line("features: '%s' dvar located at 0x%llX (rva 0x%llX)", feature.name.c_str(),
                static_cast<unsigned long long>(dvar),
                static_cast<unsigned long long>(dvar - base));

    // Log the whole structure, not just where it is.
    //
    // The dvar_t layout is undocumented and the value slots are only
    // identifiable by comparing one struct against another. When a write lands
    // cleanly and the game still ignores it - which is exactly what happened
    // with cg_drawFPS - this dump is the difference between a guess and an
    // answer, and it saves instrumenting and rebuilding again.
    std::vector<uint8_t> dump(kDvarDumpSize, 0);
    if (meml::Read(dvar, dump.data(), dump.size())) {
        mwlog::Line("features: '%s' dvar bytes: %s", feature.name.c_str(),
                    HexBytes(dump).c_str());
    }

    // The field after the name is the description. Reading it back confirms the
    // entry really is the cvar we asked for, and not merely a struct that
    // happens to start with a pointer to the right string.
    uintptr_t description = 0;
    if (meml::Read(dvar + sizeof(uintptr_t), &description, sizeof(description)) &&
        description != 0) {
        char text[96] = {};
        if (meml::Read(description, text, sizeof(text) - 1)) {
            mwlog::Line("features: '%s' description reads as '%s'", feature.name.c_str(), text);
        }
    }

    g_dvarCache[cacheKey] = dvar;
    return dvar;
}

// Add one written value slot to the patcher, labelled so the log is readable.
bool AddDvarPatch(const Feature& feature, uintptr_t dvar, uintptr_t offset,
                  const std::vector<uint8_t>& bytes, const std::vector<bool>& mask,
                  const char* suffix) {
    char label[256] = {};
    std::snprintf(label, sizeof(label), "%s%s+0x%llX", feature.name.c_str(), suffix,
                  static_cast<unsigned long long>(offset));
    return g_patcher.Add(label, dvar + offset, bytes, mask);
}

} // namespace

void features::Init(const std::wstring& configPath) {
    g_error.clear();
    g_features.clear();
    g_patcher = Patcher();
    g_dvarCache.clear();
    g_applied = false;

    if (!g_config.Load(configPath)) {
        g_error = "could not open config file";
        mwlog::Line("features: %s", g_error.c_str());
        return;
    }

    g_gameModule = Widen(g_config.Get("general", "gameExe"));
    if (!g_gameModule.empty()) {
        mwlog::Line("features: default module %ls", g_gameModule.c_str());
    } else {
        mwlog::Line("features: default module is the host executable");
    }

    for (const std::string& section : g_config.Sections()) {
        if (section == "general") {
            continue;
        }
        const bool isFeature = g_config.Has(section, "signature") ||
                               g_config.Has(section, "nameStringRva") ||
                               g_config.Has(section, "cvar") ||
                               FindPreset(section) != nullptr;
        if (!isFeature) {
            continue;
        }
        if (g_config.GetInt(section, "enabled", 1) == 0) {
            mwlog::Line("features: '%s' disabled, skipping", section.c_str());
            continue;
        }

        Feature feature;
        if (!BuildFeature(section, feature)) {
            mwlog::Line("features: %s", g_error.c_str());
            continue;
        }

        // A preset may carry a second, separate value: the clamp. It needs its
        // own feature because it writes a different number.
        if (feature.preset != nullptr && feature.preset->clampOffset[0] != '\0') {
            const float clampValue =
                g_config.GetFloat(section, feature.preset->clampKey, 0.0f);
            if (clampValue > 0.0f) {
                Feature clamp = feature;
                clamp.name = section + ":max";
                clamp.preset = nullptr;
                clamp.value = clampValue;
                clamp.valueOffsets = ParseOffsetList(feature.preset->clampOffset);
                clamp.patchBytes = FloatBytes(clampValue);
                clamp.patchMask.assign(clamp.patchBytes.size(), true);
                g_features.push_back(std::move(clamp));
            }
        }

        g_features.push_back(std::move(feature));
    }

    mwlog::Line("features: %zu enabled feature(s)", g_features.size());
}

bool features::Apply() {
    if (g_applied) {
        return true;
    }
    if (g_features.empty()) {
        g_error = "no enabled features configured";
        return false;
    }

    g_patcher = Patcher();
    bool anyAdded = false;

    for (const Feature& feature : g_features) {
        if (IsDvarType(feature.type)) {
            const uintptr_t dvar = LocateDvar(feature);
            if (dvar == 0) {
                g_error = "dvar not found for '" + feature.name + "'";
                continue;
            }
            for (const uintptr_t offset : feature.valueOffsets) {
                if (AddDvarPatch(feature, dvar, offset, feature.patchBytes, feature.patchMask,
                                 "")) {
                    anyAdded = true;
                }
            }
            continue;
        }

        const uintptr_t target = LocateFeature(feature);
        if (target == 0) {
            mwlog::Line("features: signature for '%s' not found", feature.name.c_str());
            g_error = "signature not found for '" + feature.name + "'";
            continue;
        }

        if (g_patcher.Add(feature.name, target, feature.patchBytes, feature.patchMask)) {
            anyAdded = true;
        }
    }

    if (!anyAdded) {
        g_error = "no patches could be applied";
        return false;
    }

    if (!g_patcher.ApplyAll()) {
        g_error = "one or more patches failed to apply";
        // Continue anyway: partial success is still useful for diagnosis.
    }

    // Read the values back through the cache.
    //
    // "The patch applied" and "the engine's value is now what we set" are two
    // different claims, and only the second one can be true when a feature
    // writes without error and still changes nothing on screen. The cache makes
    // this free: no rescanning.
    for (const Feature& feature : g_features) {
        if (!IsDvarType(feature.type) || feature.valueOffsets.empty()) {
            continue;
        }
        const uintptr_t dvar = LocateDvar(feature);
        if (dvar == 0) {
            continue;
        }
        const uintptr_t offset = feature.valueOffsets.front();
        uint32_t raw = 0;
        if (!meml::Read(dvar + offset, &raw, sizeof(raw))) {
            continue;
        }
        float asFloat = 0.0f;
        std::memcpy(&asFloat, &raw, sizeof(asFloat));
        mwlog::Line("features: '%s' reads back +0x%llX = int %d (float %g)",
                    feature.name.c_str(), static_cast<unsigned long long>(offset),
                    static_cast<int>(raw), static_cast<double>(asFloat));
    }

    g_applied = true;
    g_error.clear();
    return true;
}

void features::Restore() {
    if (!g_applied) {
        return;
    }
    g_patcher.RestoreAll();
    g_applied = false;
}

bool features::Toggle() {
    if (g_applied) {
        Restore();
        return false;
    }
    Apply();
    return g_applied;
}

bool features::Applied() {
    return g_applied;
}

void features::KeepApplied() {
    if (!g_applied) {
        return;
    }
    g_patcher.ReapplyChanged();
}

const char* features::LastError() {
    return g_error.c_str();
}
