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
    // snd_enableStream: the switch the sound code tests before it starts a
    // streamed sound. The soundtrack is streamed, so 0 is "no music".
    //
    // The guides say to follow it with snd_restart, and in this build that is
    // pointless: snd_restart is registered in the command table pointing at
    // 0x80E50, which is `C2 00 00` - a function that returns immediately - and
    // nothing else refers to the name, so there is no code left that could
    // react to it. What the docs do not mention is that this cvar is saved and
    // reloaded with the rest of the sound settings, so a single write does not
    // survive; the engine's watchdog re-applies it, which is why it is listed
    // as an ordinary live feature here.
    {"music", "snd_enableStream", false, "0x10,0x20,0x30", "0", "", ""},
    // r_fullbright: everything drawn unlit. Read from six places, so the value
    // lands. The switch turns it *on*, which is the only way anyone wants it.
    {"r_fullbright", "r_fullbright", false, "0x10,0x20,0x30", "1", "", ""},
    // cg_draw2D: the whole 2D overlay - the HUD, the crosshair, the on-screen
    // counters. Read from three places. The switch turns it *off*; leaving it on
    // is what the game already does, so a checkbox that wrote 1 would do nothing.
    {"cg_draw2D", "cg_draw2D", false, "0x10,0x20,0x30", "0", "", ""},
    // cg_drawGun: the first-person view model - "Draw the view model" in the
    // binary's own words. Read from four places, all in the viewmodel drawing
    // path. Off again, for the same reason.
    {"cg_drawGun", "cg_drawGun", false, "0x10,0x20,0x30", "0", "", ""},
    // r_fog: the distance fog the renderer draws. Read once, from the view
    // setup, so 0 takes the fog out. Off is the useful direction, so the switch
    // writes 0.
    {"r_fog", "r_fog", false, "0x10,0x20,0x30", "0", "", ""},
    // r_filmUseTweaks: the *outer* gate, and the piece that was missing. The
    // renderer copies the r_filmTweak* family into its per-frame struct only
    // when this is on:
    //
    //     rva 0x2127B  mov  rax, [r_filmUseTweaks]
    //     rva 0x21282  cmp  byte ptr [rax + 0x10], 0
    //     rva 0x21286  je   <skip the copy entirely>
    //     rva 0x2128C  mov  rax, [r_filmTweakEnable]      ; only reached now
    //     rva 0x2129E  mov  [rbx + 0x220], cl             ; the flag, per frame
    //     ...                                             ; then the parameters
    //
    // So r_filmTweakEnable on its own is read and then ignored - which is what
    // was seen in game. This one is written by the same switch.
    {"r_filmUseTweaks", "r_filmUseTweaks", false, "0x10,0x20,0x30", "1", "", ""},
    // r_filmTweakEnable: the flag inside that family. Registered with
    // `xor edx, edx` (default 0, described as "Tweak dev var; enable film color
    // effects"), so it is turned *on*. The parameters it sits with are
    // registered at their real values - r_filmTweakContrast at 1.4,
    // r_filmTweakDesaturation at 0.2 - so opening the two gates is the whole
    // setting; writing the parameters would be writing what is already there.
    {"r_filmTweakEnable", "r_filmTweakEnable", false, "0x10,0x20,0x30", "1", "", ""},
    // cg_gun_x, cg_gun_y, cg_gun_z: where the first-person view model sits, in
    // engine units - forward, right and up, in the binary's own descriptions.
    // All three are floats with a default of 0 and no real clamp (the minimum
    // passed to the registration is -FLT_MAX), read once each where the
    // viewmodel origin is built. They are values rather than switches, so the
    // window gives them sliders.
    // The film tweak's scalars, so the grade can be tuned rather than only
    // switched. All three are floats whose registered defaults are the values
    // below - 1.0 for contrast would be "no contrast change", so 1.4 is what the
    // grade looks like by default - and each is read where the film pass is set
    // up. The tints and r_filmTweakInvert are deliberately absent: the tints are
    // colour dvars and invert is a flag, so neither is a slider.
    {"r_filmTweakContrast", "r_filmTweakContrast", true, "0x10,0x20,0x30", "1.4", "", ""},
    {"r_filmTweakBrightness", "r_filmTweakBrightness", true, "0x10,0x20,0x30", "0", "", ""},
    {"r_filmTweakDesaturation", "r_filmTweakDesaturation", true, "0x10,0x20,0x30", "0.2", "", ""},
    // The three tints, and the odd ones out. Their registration does not go
    // through the float helper at all: it gathers four floats into a 16-byte
    // block and passes a pointer to it with type 9, so they are *colour* dvars.
    // One float would therefore set only the first component and leave the rest
    // at zero - a pure red tint rather than a grey one - so the offsets list the
    // first three components of the current value and the same number goes into
    // all of them. That is the grey level, which is what the single numbers the
    // guides quote for these cvars are about; the fourth component is untouched.
    {"r_filmTweakLightTint", "r_filmTweakLightTint", true, "0x10,0x14,0x18", "1.1", "", ""},
    {"r_filmTweakMediumTint", "r_filmTweakMediumTint", true, "0x10,0x14,0x18", "0.9", "", ""},
    {"r_filmTweakDarkTint", "r_filmTweakDarkTint", true, "0x10,0x14,0x18", "0.7", "", ""},
    {"cg_gun_x", "cg_gun_x", true, "0x10,0x20,0x30", "0", "", ""},
    {"cg_gun_y", "cg_gun_y", true, "0x10,0x20,0x30", "0", "", ""},
    {"cg_gun_z", "cg_gun_z", true, "0x10,0x20,0x30", "0", "", ""},
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
    // sensitivity: the mouse sensitivity, and the reason it belongs here is that
    // the game's own slider carries no numbers, so there is no way to set a
    // specific value in game. Registered as a float - the call passes its
    // arguments in XMM registers - so this is a dvar_float.
    {"sensitivity", "sensitivity", true, "0x10,0x20,0x30", "5", "", ""},
    // sv_network_fps: the one debug counter the multiplayer client still reads.
    // It reports the network/snapshot rate, not the render frame rate - a much
    // lower number, and the one the old "91" cap was actually about. It is the
    // only counter multiplayer has, so it is offered even though it is not the
    // figure a frame-rate cap change would show up in.
    {"netfps", "sv_network_fps", false, "0x10,0x20,0x30", "1", "", ""},
    // drawLagometer: the network lagometer, described in the binary as "Enable
    // the 'lagometer'". It exists in iw4mp.exe and not at all in iw4sp.exe, and
    // it has no readers in the multiplayer client - the same signature as the
    // dead debug-HUD block. Shipped disabled, with the switch left here on
    // purpose: the write does land, so a live test costs one restart and
    // settles it where more static analysis would not.
    {"lagometer", "drawLagometer", false, "0x10,0x20,0x30", "1", "", ""},
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

    std::string cacheKey;
    std::vector<uintptr_t> candidates;

    if (!feature.cvarName.empty()) {
        cacheKey = moduleKey + "|" + feature.cvarName;
        const auto cached = g_dvarCache.find(cacheKey);
        if (cached != g_dvarCache.end()) {
            mwlog::Line("features: '%s' reused the cached dvar for '%s' at 0x%llX",
                        feature.name.c_str(), feature.cvarName.c_str(),
                        static_cast<unsigned long long>(cached->second));
            return cached->second;
        }

        // Every occurrence, not just the first, and ranked. Case-insensitive
        // matching is still needed because cvar capitalisation is inconsistent
        // and guides disagree with the binary (cg_drawFPS vs cg_drawfps), but a
        // case-insensitive *first* match is not good enough: this build holds
        // the string "Sensitivity" - a profile field label - and stopping there
        // reported the cvar "sensitivity" as missing, which is why the setting
        // silently did nothing even though the game reads it every frame.
        pattern::FindCandidates(base, size, feature.cvarName, candidates);
        if (candidates.empty()) {
            mwlog::Line("features: '%s' could not find the cvar named '%s' in the module",
                        feature.name.c_str(), feature.cvarName.c_str());
            return 0;
        }
    } else {
        cacheKey = moduleKey + "|rva:" + std::to_string(feature.nameStringRva);
        candidates.push_back(base + feature.nameStringRva);
    }

    // A dvar_t begins with a pointer to its own name, so the name string is what
    // is searched for and that pointer is what identifies the structure. A
    // string that nothing points at is a label, not a cvar name - and trying
    // each candidate against that test is how the two get told apart.
    uintptr_t dvar = 0;
    for (const uintptr_t candidate : candidates) {
        char probe[64] = {};
        if (!meml::Read(candidate, probe, sizeof(probe) - 1)) {
            mwlog::Line("features: '%s' name string at 0x%llX is not readable",
                        feature.name.c_str(), static_cast<unsigned long long>(candidate));
            continue;
        }
        probe[sizeof(probe) - 1] = '\0';
        mwlog::Line("features: '%s' cvar name candidate at 0x%llX (rva 0x%llX) reads as '%s'",
                    feature.name.c_str(), static_cast<unsigned long long>(candidate),
                    static_cast<unsigned long long>(candidate - base), probe);

        uint8_t raw[sizeof(uintptr_t)] = {};
        std::memcpy(raw, &candidate, sizeof(raw));

        std::vector<uint8_t> bytes;
        std::vector<bool> mask;
        if (!pattern::Parse(BytesToPattern(raw, sizeof(raw)), bytes, mask)) {
            continue;
        }

        dvar = pattern::Find(base, size, bytes, mask);
        if (dvar != 0) {
            break;
        }
        mwlog::Line("features: '%s' nothing points at that string, so it is a label, not a cvar name",
                    feature.name.c_str());
    }

    if (dvar == 0) {
        mwlog::Line("features: '%s' none of the %d name candidate(s) for '%s' is a dvar",
                    feature.name.c_str(), static_cast<int>(candidates.size()),
                    feature.cvarName.c_str());
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

// Label for one written value slot. Live updates switch individual features on
// and off by name, so the label has to be derived identically in both places.
std::string DvarLabel(const Feature& feature, const char* suffix, uintptr_t offset) {
    char label[256] = {};
    std::snprintf(label, sizeof(label), "%s%s+0x%llX", feature.name.c_str(), suffix,
                  static_cast<unsigned long long>(offset));
    return std::string(label);
}

// Add one written value slot to the patcher, labelled so the log is readable.
bool AddDvarPatch(const Feature& feature, uintptr_t dvar, uintptr_t offset,
                  const std::vector<uint8_t>& bytes, const std::vector<bool>& mask,
                  const char* suffix) {
    return g_patcher.Add(DvarLabel(feature, suffix, offset), dvar + offset, bytes, mask);
}

// -----------------------------------------------------------------------------
// Live updates, driven by the launcher window.
// -----------------------------------------------------------------------------

// Find the feature built for `section`, or build it from its preset.
//
// The window is allowed to switch on something the config never mentioned - the
// shipped config only documents what it ships - so this falls back to the same
// preset table the config reader uses instead of requiring a matching section.
int EnsureFeatureIndex(const std::string& section) {
    for (size_t i = 0; i < g_features.size(); ++i) {
        if (g_features[i].name == section) {
            return static_cast<int>(i);
        }
    }

    const DvarPreset* preset = FindPreset(section);
    if (preset == nullptr) {
        return -1;
    }

    Feature feature;
    feature.name = section;
    feature.type = preset->isFloat ? "dvar_float" : "dvar_int";
    feature.cvarName = preset->cvar;
    feature.valueOffsets = ParseOffsetList(preset->valueOffsets);
    feature.preset = preset;
    g_features.push_back(std::move(feature));
    mwlog::Line("features: '%s' built on demand for a live update", section.c_str());
    return static_cast<int>(g_features.size() - 1);
}

// The FOV clamp is a second patch on cg_fov at a different offset, so it needs a
// feature of its own - and being optional, it may not exist yet either.
int EnsureClampIndex() {
    const std::string section = "fov:max";
    for (size_t i = 0; i < g_features.size(); ++i) {
        if (g_features[i].name == section) {
            return static_cast<int>(i);
        }
    }

    const DvarPreset* preset = FindPreset("fov");
    if (preset == nullptr || preset->clampOffset[0] == '\0') {
        return -1;
    }

    Feature feature;
    feature.name = section;
    feature.type = "dvar_float";
    feature.cvarName = preset->cvar;
    feature.valueOffsets = ParseOffsetList(preset->clampOffset);
    g_features.push_back(std::move(feature));
    mwlog::Line("features: '%s' built on demand for a live update", section.c_str());
    return static_cast<int>(g_features.size() - 1);
}

// Write, or switch off, one feature's value in place.
bool WriteLiveFeature(Feature& feature, bool enabled, double value) {
    const uintptr_t dvar = LocateDvar(feature);
    if (dvar == 0) {
        mwlog::Line("features: live update for '%s' skipped - the cvar was not located",
                    feature.name.c_str());
        return false;
    }

    // Remember the value even when switching off: switching back on has to
    // restore the same number without the config being re-read.
    if (feature.type == "dvar_int") {
        feature.intValue = static_cast<int>(value);
        feature.patchBytes = Int32Bytes(feature.intValue);
    } else {
        feature.value = static_cast<float>(value);
        feature.patchBytes = FloatBytes(feature.value);
    }
    feature.patchMask.assign(feature.patchBytes.size(), true);

    bool changed = false;
    for (const uintptr_t offset : feature.valueOffsets) {
        const std::string label = DvarLabel(feature, "", offset);
        if (enabled) {
            changed |= g_patcher.AddOrUpdate(label, dvar + offset, feature.patchBytes,
                                             feature.patchMask);
        } else {
            changed |= g_patcher.Restore(label);
        }
    }

    if (feature.type == "dvar_int") {
        mwlog::Line("features: live '%s' -> %s, value %d", feature.name.c_str(),
                    enabled ? "on" : "off", feature.intValue);
    } else {
        mwlog::Line("features: live '%s' -> %s, value %g", feature.name.c_str(),
                    enabled ? "on" : "off", static_cast<double>(feature.value));
    }
    return changed;
}

bool SetLive(const std::string& section, bool enabled, double value) {
    const int index = EnsureFeatureIndex(section);
    if (index < 0) {
        mwlog::Line("features: live update for '%s' ignored - no such preset", section.c_str());
        return false;
    }
    return WriteLiveFeature(g_features[static_cast<size_t>(index)], enabled, value);
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

bool features::ApplyLive(const ipc::Values& values) {
    if (values.version != ipc::kProtocolVersion) {
        mwlog::Line("features: ignoring live values for protocol %u (this build speaks %u)",
                    static_cast<unsigned>(values.version),
                    static_cast<unsigned>(ipc::kProtocolVersion));
        return false;
    }

    bool changed = false;
    changed |= SetLive("fps", values.fpsEnabled != 0, static_cast<double>(values.fpsValue));
    changed |= SetLive("fov", values.fovEnabled != 0, static_cast<double>(values.fovValue));

    const int clampIndex = EnsureClampIndex();
    if (clampIndex >= 0) {
        changed |= WriteLiveFeature(g_features[static_cast<size_t>(clampIndex)],
                                    values.fovClampEnabled != 0,
                                    static_cast<double>(values.fovClampValue));
    }

    // The counter is an enum rather than a flag, so the window sends the index.
    changed |= SetLive("drawfps", values.counterEnabled != 0,
                       static_cast<double>(values.counterMode));
    changed |= SetLive("netfps", values.netFpsEnabled != 0, 1.0);

    // snd_enableStream. The value written is always 0 - switching this on means
    // "no streamed audio", so there is nothing to configure - and it is written
    // on every pass like the others, which is what keeps the game's own sound
    // settings from quietly turning the music back on.
    changed |= SetLive("music", values.musicEnabled != 0, 0.0);

    // The other-settings switches, same shape: the value is part of the setting,
    // not something the window configures, so it is fixed here.
    changed |= SetLive("r_fullbright", values.fullbrightEnabled != 0, 1.0);
    changed |= SetLive("cg_draw2D", values.hudEnabled != 0, 0.0);
    changed |= SetLive("cg_drawGun", values.gunEnabled != 0, 0.0);
    changed |= SetLive("r_fog", values.fogEnabled != 0, 0.0);
    // The film tweak needs *both* gates. r_filmUseTweaks decides whether the
    // renderer copies the family into its frame struct at all, and
    // r_filmTweakEnable is the flag inside it; opening one without the other
    // leaves the look untouched, which is exactly how this failed the first time.
    changed |= SetLive("r_filmUseTweaks", values.filmTweakEnabled != 0, 1.0);
    changed |= SetLive("r_filmTweakEnable", values.filmTweakEnabled != 0, 1.0);

    // The viewmodel offsets, three values behind one switch. They are one
    // setting in the window ("move the viewmodel") because moving it in one axis
    // only is not a thing anyone wants.
    // The film tweak's scalars ride the same switch as the grade itself: the
    // gate has to be open for any of them to be read at all.
    changed |= SetLive("r_filmTweakContrast", values.filmTweakEnabled != 0,
                       static_cast<double>(values.filmContrast));
    changed |= SetLive("r_filmTweakBrightness", values.filmTweakEnabled != 0,
                       static_cast<double>(values.filmBrightness));
    changed |= SetLive("r_filmTweakDesaturation", values.filmTweakEnabled != 0,
                       static_cast<double>(values.filmDesaturation));
    changed |= SetLive("r_filmTweakLightTint", values.filmTweakEnabled != 0,
                       static_cast<double>(values.filmLightTint));
    changed |= SetLive("r_filmTweakMediumTint", values.filmTweakEnabled != 0,
                       static_cast<double>(values.filmMediumTint));
    changed |= SetLive("r_filmTweakDarkTint", values.filmTweakEnabled != 0,
                       static_cast<double>(values.filmDarkTint));

    changed |= SetLive("cg_gun_x", values.moveGunEnabled != 0, static_cast<double>(values.gunX));
    changed |= SetLive("cg_gun_y", values.moveGunEnabled != 0, static_cast<double>(values.gunY));
    changed |= SetLive("cg_gun_z", values.moveGunEnabled != 0, static_cast<double>(values.gunZ));

    changed |= SetLive("sensitivity", values.sensitivityEnabled != 0,
                       static_cast<double>(values.sensitivityValue));

    return changed;
}

const char* features::LastError() {
    return g_error.c_str();
}
