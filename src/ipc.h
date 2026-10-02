#pragma once

#include <cstdint>

// -----------------------------------------------------------------------------
// The live channel between the launcher window and the injected DLL.
//
// The DLL reads its configuration once, at injection, and applies it once. That
// is fine for a config file but useless for a slider: without a channel, moving
// a slider could only ever change what happens on the *next* launch, which makes
// the window a config editor rather than a control panel.
//
// So the DLL creates a hidden window inside the game process and the launcher
// sends it the new values as WM_COPYDATA. The DLL applies them to the cvars it
// has already located, with no re-read of the file and no re-scan of the module.
//
// This header is included by both binaries, so the class name, the message name
// and the struct layout cannot drift apart.
// -----------------------------------------------------------------------------
namespace ipc {

// Window class created by the DLL inside the game process. A plain hidden
// top-level window rather than a message-only one, because message-only windows
// are invisible to FindWindow - which is exactly how the launcher finds this.
inline constexpr wchar_t kWindowClass[] = L"MW2UnlockerLive";

// The values travel as WM_COPYDATA, carrying this value in
// COPYDATASTRUCT.dwData so the receiver knows the message is ours.
//
// WM_COPYDATA specifically, and not a custom registered message. WM_COPYDATA is
// a system message, so the kernel copies the struct - and the payload it points
// at - into the receiving process. A message of our own in the WM_APP range is
// delivered verbatim, which means the receiver would follow a pointer that
// belongs to the *sender's* address space. That is not a subtle failure: it is
// an access violation inside a window procedure, which the kernel escalates to
// STATUS_FATAL_USER_CALLBACK_EXCEPTION (0xC000041D) and the game dies with it.
// This cost one multiplayer session to learn; it is written down here so the
// message choice is not "simplified" back later.
inline constexpr std::uintptr_t kCopyDataMagic = 0x4D573255; // 'MW2U'

// Bumped whenever Values changes shape. Both sides compare it and refuse a
// mismatch, rather than reinterpreting a struct laid out by another version -
// the two binaries are shipped side by side but can be updated independently.
//
// 2: added the toggle key.
// 3: added the mouse sensitivity.
// 4: added the music switch.
// 5: added the other-settings switches.
// 6: added the fog and film-tweak switches.
// 7: added the viewmodel offsets.
// 8: added the film tweak parameters.
// 9: added the film tweak tints.
// 10: added the glow tweak parameters.
// 11: added the blur and black level values.
// 12: added the HUD safe area.
// 13: dropped the base safe-area pair, which moved nothing in game.
// 14: added the compass size, alongside the safe area.
// 15: added the crosshair switch, timescale and phys_gravity.
// 16: added the switch those last two hang behind.
inline constexpr std::uint32_t kProtocolVersion = 16;

// Everything the window can change. Mirrors the preset sections in the config.
struct Values {
    std::uint32_t version;
    std::int32_t fpsEnabled;
    std::int32_t fpsValue;
    std::int32_t fovEnabled;
    float fovValue;
    std::int32_t fovClampEnabled;
    float fovClampValue;
    std::int32_t counterEnabled; // cg_drawFPS - single player only
    std::int32_t counterMode;    // 0 Off, 1 Simple, 2 SimpleRanges, 3 Verbose, 4 Verbose+Viewpos
    std::int32_t netFpsEnabled;  // sv_network_fps - the only counter multiplayer reads

    // The in-game hotkey, so it can be changed without restarting the game. The
    // window offers F1..F12, which is 0x70..0x7B.
    std::int32_t toggleKey;

    // Mouse sensitivity. The game's own slider has no numbers on it, so the
    // window shows two decimals: that is the whole point of offering it here.
    std::int32_t sensitivityEnabled;
    float sensitivityValue;

    // Music off. snd_enableStream is what the sound code tests before it starts
    // a streamed sound, and the soundtrack is streamed - so holding it at zero
    // is what keeps the music away. It is a value the game saves and reloads
    // with its other sound settings, which is why it is re-applied continuously
    // rather than written once.
    std::int32_t musicEnabled;

    // The rest of the "Other Settings" group. Each of these is a *switch* rather
    // than a value: the number written is fixed, and it is chosen so that ticking
    // the box does what its label says.
    std::int32_t fullbrightEnabled; // r_fullbright -> 1, everything drawn unlit
    std::int32_t hudEnabled;        // cg_draw2D    -> 0, no 2D overlay at all
    std::int32_t gunEnabled;        // cg_drawGun   -> 0, no first-person weapon
    std::int32_t fogEnabled;        // r_fog        -> 0, no fog
    std::int32_t filmTweakEnabled;  // r_filmUseTweaks + r_filmTweakEnable -> 1

    // The viewmodel's position, in engine units: forward, right and up. Unlike
    // the switches above these are values, so the window offers them as sliders
    // and the numbers travel as they are.
    //
    // moveGunEnabled rather than gunEnabled: gunEnabled is already the
    // "hide the weapon model" switch above, and one struct cannot have two
    // members with the same name.
    std::int32_t moveGunEnabled;
    float gunX;
    float gunY;
    float gunZ;

    // The film tweak's three scalars. The tints and r_filmTweakInvert are not
    // here: the tints are registered as *colour* dvars (four float arguments,
    // through a different registration helper), so a slider is the wrong control
    // for them, and invert is a flag rather than a value.
    float filmContrast;      // r_filmTweakContrast,   1.4 by default
    float filmBrightness;    // r_filmTweakBrightness, 0 by default
    float filmDesaturation;  // r_filmTweakDesaturation, 0.2 by default

    // The three tints. These are *colour* dvars - four floats in a 16-byte
    // block, type 9 - so one number cannot describe one. What the window offers
    // is the grey level: the same value goes into the first three components
    // (r, g and b), which is exactly what the single numbers the guides quote for
    // these cvars achieve. The fourth component is left as the game set it.
    float filmLightTint;     // r_filmTweakLightTint,  1.1 by default
    float filmMediumTint;    // r_filmTweakMediumTint, 0.9
    float filmDarkTint;      // r_filmTweakDarkTint,   0.7

    // The glow tweak: the bloom the renderer puts around everything bright.
    // It needs FOUR gates, not two - r_glow_allowed decides whether glow is
    // permitted at all and ships at 0, then r_glowTweakEnable sits behind
    // r_glowUseTweaks exactly as the film tweak does. One switch writes all
    // four, because any one of them left at its default makes the rest inert.
    std::int32_t glowEnabled; // r_glow_allowed + r_glow + r_glowUseTweaks + r_glowTweakEnable
    // 1 and 2 rather than the 5 and 20 the engine registers: at those the bloom
    // already washes the screen out, so the window starts lower than the game.
    float glowRadius;         // r_glowTweakRadius0
    float glowIntensity;      // r_glowTweakBloomIntensity0
    float glowCutoff;         // r_glowTweakBloomCutoff,       0.5
    float glowDesaturation;   // r_glowTweakBloomDesaturation,   0

    // Two screen effects with no gate of their own: r_blur ("Dev tweak to blur
    // the screen", registered at 0 with a minimum of 0) and r_blacklevel ("Black
    // level (negative brightens output)", registered at 0 between -0.99 and
    // +0.99). Both are neutral at 0, so there is nothing to switch - the window
    // offers them as values and always writes them, and a slider left at zero
    // does what the game does anyway.
    float blurValue;          // r_blur
    float blackLevel;         // r_blacklevel

    // The HUD safe area: the fraction of the screen the 2D overlay is laid out
    // within, so a smaller number pulls the whole HUD in towards the centre.
    //
    // The game registers four of these floats between 0 and 1, but only the
    // "adjusted" pair is carried here. Both pairs were offered and tried; moving
    // safeArea_horizontal or safeArea_vertical (the base pair, registered at
    // 0.85) changed nothing that could be seen, while
    // safeArea_adjusted_horizontal and _vertical (both 1.0 - the pair the game's
    // own Options > Safe Area menu writes, and what the getadjustedsafearea*
    // script functions return) did move the HUD. The base pair keeps its sections
    // in the config, so a value set by hand there is still applied; the window
    // neither reads nor writes them.
    //
    // The switch is the window's own - no cvar gates the pair - and the values
    // are the registered defaults, so a slider nobody has moved writes what the
    // game already had.
    std::int32_t safeAreaEnabled;
    float safeAreaAdjustedH;   // safeArea_adjusted_horizontal, 1.0
    float safeAreaAdjustedV;   // safeArea_adjusted_vertical,   1.0

    // compassSize: "Scale the compass", a float the engine registers at 1.0 with
    // 0 as its minimum and FLT_MAX as its maximum, read as a float from +0x10 in
    // ten places. It is a HUD size like the safe area, so it shares the switch
    // above rather than carrying one of its own.
    float compassSize;

    // ---- for testing ---------------------------------------------------------
    // Three more cvars the game reads, added to the window so they can be tried
    // in a match. Each is written the way its own section already works rather
    // than inventing a new shape for it:
    //
    //   crosshairEnabled  cg_drawCrosshair. An int, registered at 1 ("Turn on
    //                     weapon crosshair", cached 0x809990), read once as
    //                     `cmp byte ptr [rax + 0x10], 0` over a block of HUD
    //                     code - so 0 hides the crosshair and anything else
    //                     draws it. 0 means "hide it", which is the state the
    //                     switch is named after.
    //   timescale         A float registered at 1.0 and read as one (cached
    //                     0x1D26580): the game's own clock. 1.0 is neutral.
    //   serverEnabled     the window's "Tweak the server" switch. Both of the two
    //                     values above are written only while it is on: unlike the
    //                     other switches there is no cvar to gate behind, so this
    //                     one decides on its own whether they are applied.
    //
    //   physGravity       phys_gravity, "Physics gravity in units/sec^2.", a
    //                     float read by code (cached 0x1B53BE0). It is the
    //                     gravity on objects rather than the player - the player
    //                     one is `g_gravity`, which this binary does not have.
    //                     The engine registers it at 800, which is neutral; the
    //                     default here is -800, the same magnitude with the sign
    //                     flipped, because that is the end of the range worth
    //                     trying in a match.
    std::int32_t crosshairEnabled; // 0 hides the crosshair
    float timescale;               // 1.0 = the game's own speed
    float physGravity;             // -800 for testing; the engine's 800 is neutral
    std::int32_t serverEnabled;    // 0 leaves both of them alone
};

inline Values MakeDefault() {
    Values values{};
    values.version = kProtocolVersion;
    values.fpsEnabled = 1;
    values.fpsValue = 250;
    values.fovEnabled = 1;
    values.fovValue = 90.0f;
    values.fovClampEnabled = 0;
    values.fovClampValue = 179.0f;
    values.counterEnabled = 0;
    values.counterMode = 1;
    values.netFpsEnabled = 0;
    values.toggleKey = 0x75; // F6

    // Left off on purpose. Sensitivity is the one setting here that changes how
    // the game *plays* rather than how it looks, so it is not written until it
    // is asked for.
    values.sensitivityEnabled = 0;
    values.sensitivityValue = 5.0f;

    // Off as well: these change the game rather than fixing anything, so they are
    // left alone until they are ticked.
    values.musicEnabled = 0;
    values.fullbrightEnabled = 0;
    values.hudEnabled = 0;
    values.gunEnabled = 0;
    values.fogEnabled = 0;
    values.filmTweakEnabled = 0;
    values.moveGunEnabled = 0;
    values.gunX = 0.0f;
    values.gunY = 0.0f;
    values.gunZ = 0.0f;

    // The registered defaults, so the sliders start where the game does.
    values.filmContrast = 1.4f;
    values.filmBrightness = 0.0f;
    values.filmDesaturation = 0.2f;
    values.filmLightTint = 1.1f;
    values.filmMediumTint = 0.9f;
    values.filmDarkTint = 0.7f;

    // The glow tweak is off by default, like the other things that change the
    // game rather than fix something, and its values are the registered ones.
    values.glowEnabled = 0;
    values.glowRadius = 1.0f;
    values.glowIntensity = 2.0f;
    values.glowCutoff = 0.5f;
    values.glowDesaturation = 0.0f;

    // The registered values, which are also the neutral ones.
    values.blurValue = 0.0f;
    values.blackLevel = 0.0f;

    // Off by default, like the other things that change how the game looks, and
    // with the values the engine registers.
    values.safeAreaEnabled = 0;
    values.safeAreaAdjustedH = 1.0f;
    values.safeAreaAdjustedV = 1.0f;
    values.compassSize = 1.0f;

    // The crosshair starts as the game draws it - 1 - so the switch beside it is
    // off, and the other two start at the values the engine registers, which are
    // the neutral ones.
    values.crosshairEnabled = 1;
    values.timescale = 1.0f;
    values.physGravity = -800.0f; // the testing default; 800 is the engine's neutral
    values.serverEnabled = 0;     // off, so the testing pair is not applied until asked
    return values;
}

} // namespace ipc
