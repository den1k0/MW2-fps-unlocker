// -----------------------------------------------------------------------------
// The control window.
//
// Two jobs: keep unlocker.ini in step with what the window shows, and push the
// values into the injected DLL so a change takes effect in the running game
// rather than on the next launch.
//
// Style note, because it explains why there is painting code in a launcher. The
// stock trackbar cannot be recoloured - it is drawn by the theme - so on a dark
// background it looks like a light grey strip left behind by another program.
// Rather than fight it, the slider is drawn here (a small custom control), and
// the checkboxes and buttons are owner-drawn, which is the only way to colour
// them at all. Everything that behaves like a control still behaves like one:
// tab order, focus, arrows, Enter and Escape all come from the dialog itself.
// -----------------------------------------------------------------------------
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <uxtheme.h>

#include <string>
#include <vector>

#include "app.h"
#include "gui.h"
#include "ipc.h"

// Version 6 of the common controls, so the parts that are still native (the
// edits, and the window frame) match the rest of the desktop.
#pragma comment(linker, "/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' " \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
                        "language='*'\"")

namespace {

constexpr int kDialogMain = 103;

// Control ids. These MUST match the template in launcher/generate_rc.cmake.
constexpr int kIdFpsEnable = 1001;
constexpr int kIdFpsSlider = 1002;
constexpr int kIdFpsValue = 1003;
constexpr int kIdFovEnable = 1004;
constexpr int kIdFovSlider = 1005;
constexpr int kIdFovValue = 1006;
constexpr int kIdFovClamp = 1007;
constexpr int kIdStatus = 1011;
constexpr int kIdApply = 1012;
constexpr int kIdFpsHeader = 1013;
constexpr int kIdFovHeader = 1014;
// 1015 is free: it was the old catch-all card's title, which became
// kIdOtherHeader when the viewmodel and film tweak cards were split out of it.
constexpr int kIdToggleKey = 1016;
constexpr int kIdFilmHeader = 1017; // the film tweak card, split out of Other Settings
constexpr int kIdMusicEnable = 1018;
constexpr int kIdFullbrightEnable = 1019;
constexpr int kIdHudEnable = 1020;
constexpr int kIdGunEnable = 1021;
constexpr int kIdFogEnable = 1022;
constexpr int kIdFilmTweakEnable = 1023;
constexpr int kIdViewModelHeader = 1024;
constexpr int kIdGunXSlider = 1025;
constexpr int kIdGunXValue = 1026;
constexpr int kIdGunYSlider = 1027;
constexpr int kIdGunYValue = 1028;
constexpr int kIdGunZSlider = 1029;
constexpr int kIdGunZValue = 1030;
constexpr int kIdMoveGunEnable = 1031;
constexpr int kIdOtherHeader = 1032;
constexpr int kIdCloseWithGame = 1033;
constexpr int kIdFilmContrastSlider = 1034;
constexpr int kIdFilmContrastValue = 1035;
constexpr int kIdFilmBrightnessSlider = 1036;
constexpr int kIdFilmBrightnessValue = 1037;
constexpr int kIdFilmDesaturationSlider = 1038;
constexpr int kIdFilmDesaturationValue = 1039;
constexpr int kIdFilmLightTintSlider = 1040;
constexpr int kIdFilmLightTintValue = 1041;
constexpr int kIdFilmMediumTintSlider = 1042;
constexpr int kIdFilmMediumTintValue = 1043;
constexpr int kIdFilmDarkTintSlider = 1044;
constexpr int kIdFilmDarkTintValue = 1045;

// The row labels carry ids, although nothing ever asks them for their text,
// because the two collapsible cards move and hide whole rows by id. An
// unlabelled static is id -1, and -1 finds nothing.
constexpr int kIdGunXLabel = 1046;
constexpr int kIdGunYLabel = 1047;
constexpr int kIdGunZLabel = 1048;
constexpr int kIdFilmContrastLabel = 1049;
constexpr int kIdFilmBrightnessLabel = 1050;
constexpr int kIdFilmDesaturationLabel = 1051;
constexpr int kIdFilmLightTintLabel = 1052;
constexpr int kIdFilmMediumTintLabel = 1053;
constexpr int kIdFilmDarkTintLabel = 1054;
constexpr int kIdToggleKeyLabel = 1055;

// The frame cap accepts anything up to 1000 because the engine does not enforce
// the 100 its own registration declares: 250 and 333 were measured working.
constexpr int kFpsMin = 0;
constexpr int kFpsMax = 1000;
constexpr int kFovMin = 65;
constexpr int kFovMax = 179;

// The viewmodel offsets are floats in engine units and the game clamps them not
// at all (the registration passes -FLT_MAX as its minimum), so the range is the
// window's own choice: -10.00 to +10.00, in hundredths. Ten units is far more
// than the pose can usefully be shifted by, and the hundredths still give the
// slider fine steps. The config accepts anything if you want to go further.
constexpr int kGunMin = -1000;
constexpr int kGunMax = 1000;

// The film tweak's six sliders, in hundredths. These follow the game's own
// registered domains rather than widening them: contrast has nowhere to go below
// 0, and the desaturation the game considers valid starts at -2. The tints are
// colour dvars whose defaults all sit near 1 - below 0 they invert the channel
// rather than dim it - so 0.00 to 2.00 is the useful span.
//
// The defaults are the registered ones, so the sliders start where the game
// does: contrast 1.4, brightness 0, desaturation 0.2, tints 1.1, 0.9 and 0.7.
constexpr int kContrastMin = 0;
constexpr int kContrastMax = 300;
constexpr int kBrightnessMin = -100;
constexpr int kBrightnessMax = 100;
constexpr int kDesaturationMin = -100;
constexpr int kDesaturationMax = 200;
constexpr int kTintMin = 0;
constexpr int kTintMax = 200;

// The clamp offset used when the clamp box is ticked. 179 is the widest the
// engine accepts without the view shearing.
constexpr float kFovClampValue = 179.0f;

// Slot this process's messages. The window is modal and single-instance, so one
// set of brushes and fonts is enough.
constexpr UINT kMsgStatus = WM_APP + 1;
constexpr UINT kMsgSliderSetRange = WM_APP + 10;
constexpr UINT kMsgSliderSetPosition = WM_APP + 11;
constexpr UINT kMsgSliderGetPosition = WM_APP + 12;
constexpr UINT kMsgSliderChanged = WM_APP + 13;
constexpr UINT kMsgKeyBoxSetKey = WM_APP + 15;
constexpr UINT kMsgKeyBoxGetKey = WM_APP + 16;
constexpr UINT kMsgKeyChanged = WM_APP + 17;

// -----------------------------------------------------------------------------
// Palette. A dark window with a warm accent, rather than the system greys.
// -----------------------------------------------------------------------------
// Orange, green and dark grey, which is the palette the game itself is built
// from: olive greys for the surfaces, orange for anything that wants attention,
// green for "this one is on".
constexpr COLORREF kBackground = RGB(0x16, 0x18, 0x15);
constexpr COLORREF kCard = RGB(0x20, 0x23, 0x1E);
constexpr COLORREF kCardPressed = RGB(0x2A, 0x2E, 0x27);
constexpr COLORREF kGroove = RGB(0x32, 0x36, 0x2D);
constexpr COLORREF kEdge = RGB(0x3B, 0x40, 0x35);
constexpr COLORREF kText = RGB(0xE9, 0xE7, 0xDD);
constexpr COLORREF kTextDim = RGB(0x8F, 0x93, 0x86);
constexpr COLORREF kAccent = RGB(0xE2, 0x8B, 0x2C);        // orange
constexpr COLORREF kAccentPressed = RGB(0xBC, 0x70, 0x1E);
constexpr COLORREF kGreen = RGB(0x8F, 0xB4, 0x4A);         // switched on
constexpr COLORREF kGreenPressed = RGB(0x74, 0x94, 0x3B);

// One window per process, so file-scope handles are simpler here than threading
// them through every drawing call. Created in Run, released after the dialog.
HBRUSH g_backgroundBrush = nullptr;
HBRUSH g_cardBrush = nullptr;
HBRUSH g_grooveBrush = nullptr;
HFONT g_headerFont = nullptr;

// The dialog's five cards, in dialog units: the frame cap, the field of view,
// the viewmodel, the film tweak, and the catch-all that holds the switches, the
// hotkey and the close-with-game box. ApplyLayout shortens the two collapsible
// ones when their rows are hidden.
constexpr int kCardCount = 5;
const RECT kCardUnits[kCardCount] = {
    {10, 20, 290, 88},   // frame rate cap
    {10, 94, 290, 182},  // field of view
    {10, 188, 290, 277}, // viewmodel offsets: switch, then X, Y and Z
    {10, 283, 290, 407}, // film tweak: switch, then three scalars and three tints
    {10, 413, 290, 504}, // other settings, which holds the hotkey too
};

// The height the template asks for, in dialog units. ApplyLayout takes the two
// collapsible cards' current heights off that.
constexpr int kDialogHeight = 556;

// The same cards in pixels, converted whenever the window is re-laid out.
RECT g_cards[kCardCount] = {};

// Working state for one window. Owned by gui::Run and freed only after the
// worker thread has been joined, so the worker can never touch freed memory.
struct State {
    ipc::Values values;
    std::wstring iniPath;
    std::wstring nextToExeIniPath;
    std::wstring dllPath;
    HWND dialog = nullptr;
    HANDLE stopEvent = nullptr;
    HANDLE worker = nullptr;
    CRITICAL_SECTION lock;
    std::wstring status;

    // The three check states live here, not in the controls. An owner-drawn
    // button does not maintain a check state of its own: BS_OWNERDRAW replaces
    // BS_AUTOCHECKBOX rather than adding to it, because they are different
    // values of the same style field. Ask the control and it always answers
    // "unchecked", which would switch every feature off on Apply.
    bool fpsOn = true;
    bool fovOn = true;
    bool clampOn = false;
    // The viewmodel offsets: a value rather than a switch, but one setting, so
    // they share a single enable and are kept in hundredths like the sliders
    // that show them.
    bool moveGunOn = false;
    int gunX = 0;
    int gunY = 0;
    int gunZ = 0;

    // The film tweak's scalars, held in hundredths like every other slider here.
    int filmContrast = 140;    // 1.40, the game's own default
    int filmBrightness = 0;
    int filmDesaturation = 20; // 0.20
    int filmLightTint = 110;   // 1.10 - these three are grey levels, written to
    int filmMediumTint = 90;   // the first three components of a colour dvar
    int filmDarkTint = 70;     // 0.70

    // A launcher setting, not a game cvar: it lives in [general] and says whether
    // this window closes itself when the game exits.
    bool closeWithGame = true;

    bool musicOn = false;      // these four are off by default as well: they
    bool fullbrightOn = false; // change the game rather than fixing something
    bool hudOn = false;
    bool gunOn = false;
    bool fogOn = false;
    bool filmTweakOn = false;

    // Filled in by the worker once the game is found. Together these locate the
    // game's own settings file, which is what the [sensitivity] section of the
    // config is written into - see the note in app.h. That section has no
    // controls in this window (see the note at the card rectangles below), but
    // it is still applied when it is switched on in the file.
    std::wstring gameDirectory;
    std::wstring gameName;
};

bool* CheckState(State& state, int id) {
    switch (id) {
    case kIdFpsEnable:
        return &state.fpsOn;
    case kIdFovEnable:
        return &state.fovOn;
    case kIdFovClamp:
        return &state.clampOn;
    case kIdMusicEnable:
        return &state.musicOn;
    case kIdFullbrightEnable:
        return &state.fullbrightOn;
    case kIdHudEnable:
        return &state.hudOn;
    case kIdGunEnable:
        return &state.gunOn;
    case kIdFogEnable:
        return &state.fogOn;
    case kIdFilmTweakEnable:
        return &state.filmTweakOn;
    case kIdMoveGunEnable:
        return &state.moveGunOn;
    case kIdCloseWithGame:
        return &state.closeWithGame;
    default:
        return nullptr;
    }
}

bool IsChecked(State& state, int id) {
    const bool* field = CheckState(state, id);
    return field != nullptr && *field;
}

void FillRounded(HDC dc, const RECT& rect, int radius, HBRUSH brush) {
    HRGN region = ::CreateRoundRectRgn(rect.left, rect.top, rect.right + 1, rect.bottom + 1,
                                       radius, radius);
    ::FillRgn(dc, region, brush);
    ::DeleteObject(region);
}

void FrameRounded(HDC dc, const RECT& rect, int radius, COLORREF colour) {
    // RoundRect with a hollow brush draws the outline and fills nothing, which
    // is exactly "a one pixel rounded border".
    HPEN pen = ::CreatePen(PS_SOLID, 1, colour);
    HPEN oldPen = static_cast<HPEN>(::SelectObject(dc, pen));
    HBRUSH oldBrush = static_cast<HBRUSH>(::SelectObject(dc, ::GetStockObject(HOLLOW_BRUSH)));
    ::RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom, radius, radius);
    ::SelectObject(dc, oldBrush);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(pen);
}

// -----------------------------------------------------------------------------
// The slider.
//
// A rounded groove, the filled part in the accent colour, and a circular thumb.
// What it keeps from the real thing: click anywhere to jump, drag to sweep, tab
// focus, arrows for small steps, PageUp/PageDown for large ones, Home/End, and a
// notification to the parent on every change.
// -----------------------------------------------------------------------------
constexpr wchar_t kSliderClass[] = L"MW2Slider";
constexpr int kThumbRadius = 6;
constexpr int kGrooveHeight = 5;

struct Slider {
    int minimum = 0;
    int maximum = 100;
    int position = 0;
    int step = 1;
    bool dragging = false;
    bool hovering = false;
};

Slider* SliderOf(HWND window) {
    return reinterpret_cast<Slider*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
}

int SliderSpan(const RECT& client) {
    return (client.right - kThumbRadius - 2) - (kThumbRadius + 2);
}

void SliderNotifyParent(HWND window, int position) {
    ::PostMessageW(::GetParent(window), kMsgSliderChanged,
                   static_cast<WPARAM>(::GetDlgCtrlID(window)), static_cast<LPARAM>(position));
}

int SliderPositionFromX(HWND window, int x) {
    Slider* slider = SliderOf(window);
    RECT client{};
    ::GetClientRect(window, &client);

    const int span = SliderSpan(client);
    if (span <= 0 || slider->maximum <= slider->minimum) {
        return slider->minimum;
    }

    const int inset = kThumbRadius + 2;
    const double fraction =
        static_cast<double>(x - inset) / static_cast<double>(span);
    int value = slider->minimum +
                static_cast<int>(fraction * (slider->maximum - slider->minimum) + 0.5);

    if (value < slider->minimum) {
        value = slider->minimum;
    }
    if (value > slider->maximum) {
        value = slider->maximum;
    }
    return value;
}

void SliderMoveTo(HWND window, int position, bool notify) {
    Slider* slider = SliderOf(window);
    if (slider == nullptr) {
        return;
    }

    if (position < slider->minimum) {
        position = slider->minimum;
    }
    if (position > slider->maximum) {
        position = slider->maximum;
    }
    if (position == slider->position) {
        return;
    }

    slider->position = position;
    ::InvalidateRect(window, nullptr, FALSE);
    if (notify) {
        SliderNotifyParent(window, position);
    }
}

void SliderPaint(HWND window) {
    Slider* slider = SliderOf(window);
    if (slider == nullptr) {
        return;
    }

    PAINTSTRUCT paint{};
    const HDC dc = ::BeginPaint(window, &paint);

    RECT client{};
    ::GetClientRect(window, &client);

    // Repaint the whole control every time. The control answers WM_ERASEBKGND
    // with "handled" to stop flicker, which means nothing else erases it - and
    // without this the previous thumb stays on screen as a trail of circles
    // while dragging.
    ::FillRect(dc, &client, g_cardBrush);

    const int inset = kThumbRadius + 2;
    const int span = SliderSpan(client);
    const int centre = (client.top + client.bottom) / 2;
    const int range = slider->maximum - slider->minimum;
    const int filled =
        (range <= 0) ? 0 : (span * (slider->position - slider->minimum)) / range;
    const bool active = (::GetFocus() == window) || slider->hovering || slider->dragging;

    RECT groove{inset, centre - kGrooveHeight / 2, client.right - inset,
                centre + (kGrooveHeight + 1) / 2};
    FillRounded(dc, groove, kGrooveHeight, g_grooveBrush);

    if (filled > 0) {
        // Green for the part that is filled in, orange for the thumb: the two
        // colours the game uses for "set" and "grab me".
        HBRUSH fill = ::CreateSolidBrush(kGreen);
        RECT part{groove.left, groove.top, groove.left + filled, groove.bottom};
        FillRounded(dc, part, kGrooveHeight, fill);
        ::DeleteObject(fill);
    }

    const int thumbX = inset + filled;
    HBRUSH thumb = ::CreateSolidBrush(active ? kAccent : kTextDim);
    HBRUSH oldBrush = static_cast<HBRUSH>(::SelectObject(dc, thumb));
    HPEN oldPen = static_cast<HPEN>(::SelectObject(dc, ::GetStockObject(NULL_PEN)));
    ::Ellipse(dc, thumbX - kThumbRadius, centre - kThumbRadius, thumbX + kThumbRadius + 1,
              centre + kThumbRadius + 1);
    ::SelectObject(dc, oldPen);
    ::SelectObject(dc, oldBrush);
    ::DeleteObject(thumb);

    ::EndPaint(window, &paint);
}

LRESULT CALLBACK SliderProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    Slider* slider = SliderOf(window);

    switch (message) {
    case WM_NCCREATE: {
        auto* created = new Slider();
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created));
        return ::DefWindowProcW(window, message, wparam, lparam);
    }

    case WM_NCDESTROY:
        delete slider;
        ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return ::DefWindowProcW(window, message, wparam, lparam);

    case kMsgSliderSetRange:
        slider->minimum = static_cast<int>(wparam);
        slider->maximum = static_cast<int>(lparam);
        slider->step = (slider->maximum - slider->minimum) / 100;
        if (slider->step < 1) {
            slider->step = 1;
        }
        SliderMoveTo(window, slider->position, false);
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;

    case kMsgSliderSetPosition:
        // Notifying here as well, so the number box always agrees with the
        // slider however the slider was moved. Setting the position silently
        // leaves a stale number in the box, and the next focus change would then
        // push that stale value back over the slider.
        SliderMoveTo(window, static_cast<int>(wparam), true);
        return 0;

    case kMsgSliderGetPosition:
        return slider->position;

    case WM_GETDLGCODE:
        // The arrows are ours; without this the dialog would use them to move
        // the focus between controls.
        return DLGC_WANTARROWS;

    case WM_ERASEBKGND:
        return 1; // WM_PAINT covers the whole control

    case WM_PAINT:
        SliderPaint(window);
        return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        ::SetFocus(window);
        ::SetCapture(window);
        slider->dragging = true;
        SliderMoveTo(window, SliderPositionFromX(window, GET_X_LPARAM(lparam)), true);
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_MOUSEMOVE:
        if (!slider->hovering) {
            slider->hovering = true;
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = window;
            ::TrackMouseEvent(&track);
            ::InvalidateRect(window, nullptr, FALSE);
        }
        if (slider->dragging) {
            SliderMoveTo(window, SliderPositionFromX(window, GET_X_LPARAM(lparam)), true);
        }
        return 0;

    case WM_MOUSELEAVE:
        slider->hovering = false;
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_LBUTTONUP:
        if (slider->dragging) {
            slider->dragging = false;
            ::ReleaseCapture();
            ::InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_KEYDOWN: {
        const int page = (slider->maximum - slider->minimum) / 10;
        switch (wparam) {
        case VK_LEFT:
        case VK_DOWN:
            SliderMoveTo(window, slider->position - slider->step, true);
            return 0;
        case VK_RIGHT:
        case VK_UP:
            SliderMoveTo(window, slider->position + slider->step, true);
            return 0;
        case VK_PRIOR:
            SliderMoveTo(window, slider->position + page, true);
            return 0;
        case VK_NEXT:
            SliderMoveTo(window, slider->position - page, true);
            return 0;
        case VK_HOME:
            SliderMoveTo(window, slider->minimum, true);
            return 0;
        case VK_END:
            SliderMoveTo(window, slider->maximum, true);
            return 0;
        default:
            return 0;
        }
    }

    default:
        break;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

// -----------------------------------------------------------------------------
// The hotkey box.
//
// One key out of F1..F12. Drawn here rather than assembled from a combo box so
// it matches the rest of the window; clicking steps to the next key and the
// arrow keys do the same once it has focus. The change reaches the running game,
// because the hotkey travels in the same message as the values.
// -----------------------------------------------------------------------------
constexpr wchar_t kKeyBoxClass[] = L"MW2KeyBox";

struct KeyBox {
    int key = 0x75; // F6
    bool hovering = false;
};

KeyBox* KeyBoxOf(HWND window) {
    return reinterpret_cast<KeyBox*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
}

std::wstring KeyName(int key) {
    if (key >= 0x70 && key <= 0x7B) {
        return std::wstring(L"F") + std::to_wstring(key - 0x70 + 1);
    }
    return L"-";
}

int StepKey(int key, int delta) {
    int index = key - 0x70;
    index = (index + delta) % 12;
    if (index < 0) {
        index += 12;
    }
    return 0x70 + index;
}

void KeyBoxNotifyParent(HWND window, int key) {
    ::PostMessageW(::GetParent(window), kMsgKeyChanged,
                   static_cast<WPARAM>(::GetDlgCtrlID(window)), static_cast<LPARAM>(key));
}

void KeyBoxPaint(HWND window) {
    KeyBox* box = KeyBoxOf(window);
    if (box == nullptr) {
        return;
    }

    PAINTSTRUCT paint{};
    const HDC dc = ::BeginPaint(window, &paint);

    RECT client{};
    ::GetClientRect(window, &client);
    ::FillRect(dc, &client, g_cardBrush);

    const bool active = (::GetFocus() == window) || box->hovering;
    RECT body = client;
    FillRounded(dc, body, 6, g_grooveBrush);
    if (active) {
        FrameRounded(dc, body, 6, kAccent);
    }

    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, active ? kAccent : kText);
    RECT label = body;
    ::DrawTextW(dc, KeyName(box->key).c_str(), -1, &label,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);

    ::EndPaint(window, &paint);
}

LRESULT CALLBACK KeyBoxProc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    KeyBox* box = KeyBoxOf(window);

    switch (message) {
    case WM_NCCREATE:
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(new KeyBox()));
        return ::DefWindowProcW(window, message, wparam, lparam);

    case WM_NCDESTROY:
        delete box;
        ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        return ::DefWindowProcW(window, message, wparam, lparam);

    case kMsgKeyBoxSetKey:
        box->key = static_cast<int>(wparam);
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;

    case kMsgKeyBoxGetKey:
        return box->key;

    case WM_GETDLGCODE:
        return DLGC_WANTARROWS | DLGC_WANTCHARS;

    case WM_ERASEBKGND:
        return 1; // WM_PAINT covers the whole control

    case WM_PAINT:
        KeyBoxPaint(window);
        return 0;

    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_LBUTTONDOWN:
        ::SetFocus(window);
        box->key = StepKey(box->key, 1);
        KeyBoxNotifyParent(window, box->key);
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_MOUSEMOVE:
        if (!box->hovering) {
            box->hovering = true;
            TRACKMOUSEEVENT track{};
            track.cbSize = sizeof(track);
            track.dwFlags = TME_LEAVE;
            track.hwndTrack = window;
            ::TrackMouseEvent(&track);
            ::InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_MOUSELEAVE:
        box->hovering = false;
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_KEYDOWN:
        switch (wparam) {
        case VK_LEFT:
        case VK_UP:
            box->key = StepKey(box->key, -1);
            break;
        case VK_RIGHT:
        case VK_DOWN:
        case VK_SPACE:
            box->key = StepKey(box->key, 1);
            break;
        case VK_HOME:
            box->key = 0x70;
            break;
        case VK_END:
            box->key = 0x7B;
            break;
        default:
            return 0;
        }
        KeyBoxNotifyParent(window, box->key);
        ::InvalidateRect(window, nullptr, FALSE);
        return 0;

    default:
        break;
    }
    return ::DefWindowProcW(window, message, wparam, lparam);
}

// -----------------------------------------------------------------------------
// Owner-drawn controls.
// -----------------------------------------------------------------------------
void DrawCheckbox(HDC dc, const RECT& rect, const std::wstring& text, bool checked, bool focused,
                  bool disabled) {
    ::FillRect(dc, &rect, g_cardBrush);

    constexpr int kBox = 14;
    const int top = rect.top + ((rect.bottom - rect.top) - kBox) / 2;
    RECT box{rect.left, top, rect.left + kBox, top + kBox};

    if (checked) {
        HBRUSH fill = ::CreateSolidBrush(disabled ? kTextDim : kGreen);
        FillRounded(dc, box, 4, fill);
        ::DeleteObject(fill);

        // The tick, as two strokes, so it scales with the box.
        const int w = box.right - box.left;
        const int h = box.bottom - box.top;
        POINT tick[3] = {
            {box.left + w / 4, box.top + h / 2},
            {box.left + w / 2 - 1, box.top + (h * 3) / 4},
            {box.left + (w * 3) / 4 + 1, box.top + h / 4},
        };
        HPEN pen = ::CreatePen(PS_SOLID, 2, kBackground);
        HPEN oldPen = static_cast<HPEN>(::SelectObject(dc, pen));
        ::Polyline(dc, tick, 3);
        ::SelectObject(dc, oldPen);
        ::DeleteObject(pen);
    } else {
        FillRounded(dc, box, 4, g_grooveBrush);
        FrameRounded(dc, box, 4, focused ? kAccent : kEdge);
    }

    if (focused && checked) {
        RECT outline{box.left - 2, box.top - 2, box.right + 2, box.bottom + 2};
        FrameRounded(dc, outline, 6, kAccent);
    }

    ::SetBkMode(dc, TRANSPARENT);
    ::SetTextColor(dc, disabled ? kTextDim : kText);

    RECT label{box.right + 9, rect.top, rect.right, rect.bottom};
    ::DrawTextW(dc, text.c_str(), -1, &label, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
}

// A checkbox that opens a stack of rows underneath it. The triangle is the only
// thing telling the user there is more to this card, so it is drawn whether the
// card is open or closed: pointing down when the rows are showing, right when
// they are folded away. It takes the accent colour only while something is
// hidden, which keeps the window quiet once everything is unfolded.
void DrawExpandableCheckbox(HDC dc, const RECT& rect, const std::wstring& text, bool checked,
                            bool expanded, bool focused, bool disabled) {
    // The triangle lives in a column of its own. Both forms fill the same box -
    // one square, one tall - so they line up on the same left edge and neither
    // crowds the checkbox, which a wider flat triangle would.
    constexpr int kArrowBox = 18;
    constexpr int kArrow = 8;
    constexpr int kArrowInset = 2; // keeps a clear gap between arrow and box

    ::FillRect(dc, &rect, g_cardBrush);

    const int boxLeft = rect.left + kArrowInset;
    const int boxTop = rect.top + ((rect.bottom - rect.top) - kArrow) / 2;
    POINT triangle[3];
    if (expanded) {
        triangle[0] = {boxLeft, boxTop};
        triangle[1] = {boxLeft + kArrow, boxTop};
        triangle[2] = {boxLeft + kArrow / 2, boxTop + kArrow};
    } else {
        triangle[0] = {boxLeft, boxTop};
        triangle[1] = {boxLeft, boxTop + kArrow};
        triangle[2] = {boxLeft + kArrow, boxTop + kArrow / 2};
    }

    const COLORREF colour = (disabled || expanded) ? kTextDim : kAccent;
    HBRUSH fill = ::CreateSolidBrush(colour);
    HPEN pen = ::CreatePen(PS_SOLID, 1, colour);
    HBRUSH oldBrush = static_cast<HBRUSH>(::SelectObject(dc, fill));
    HPEN oldPen = static_cast<HPEN>(::SelectObject(dc, pen));
    ::Polygon(dc, triangle, 3);
    ::SelectObject(dc, oldBrush);
    ::SelectObject(dc, oldPen);
    ::DeleteObject(fill);
    ::DeleteObject(pen);

    // The same checkbox and label, shifted to make room for the triangle.
    RECT inner{rect.left + kArrowBox, rect.top, rect.right, rect.bottom};
    DrawCheckbox(dc, inner, text, checked, focused, disabled);
}

void DrawButton(HDC dc, const RECT& rect, const std::wstring& text, bool primary, bool pressed,
                bool focused, bool disabled) {
    ::FillRect(dc, &rect, g_backgroundBrush);

    const COLORREF face = primary ? (pressed ? kAccentPressed : kAccent)
                                  : (pressed ? kCardPressed : kCard);
    HBRUSH fill = ::CreateSolidBrush(disabled ? kCard : face);
    FillRounded(dc, rect, 8, fill);
    ::DeleteObject(fill);

    if (!primary) {
        FrameRounded(dc, rect, 8, focused ? kAccent : kEdge);
    } else if (focused) {
        RECT outline{rect.left - 2, rect.top - 2, rect.right + 2, rect.bottom + 2};
        FrameRounded(dc, outline, 10, kAccent);
    }

    ::SetBkMode(dc, TRANSPARENT);
    if (disabled) {
        ::SetTextColor(dc, kTextDim);
    } else {
        ::SetTextColor(dc, primary ? kBackground : kText);
    }

    RECT label = rect; // DrawText takes a non-const rect
    ::DrawTextW(dc, text.c_str(), -1, &label, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

// -----------------------------------------------------------------------------
// Window state.
// -----------------------------------------------------------------------------
int ClampInt(int value, int minimum, int maximum) {
    if (value < minimum) {
        return minimum;
    }
    if (value > maximum) {
        return maximum;
    }
    return value;
}

std::wstring FormatInt(int value) {
    return std::to_wstring(value);
}

std::wstring FormatFloat(double value) {
    wchar_t buffer[32] = {};
    ::swprintf_s(buffer, L"%g", value);
    return std::wstring(buffer);
}

// The config documents the hotkey in hex (0x75 = F6), so it is written that way.
std::wstring FormatHex(int value) {
    wchar_t buffer[16] = {};
    ::swprintf_s(buffer, L"0x%X", value);
    return std::wstring(buffer);
}

// The viewmodel offsets are shown with two decimals: they are small numbers, and
// hundredths is the step the sliders move in.
std::wstring FormatHundredths(int hundredths) {
    wchar_t buffer[32] = {};
    ::swprintf_s(buffer, L"%.2f", hundredths / 100.0);
    return std::wstring(buffer);
}

// A float back to hundredths, rounded away from zero so a negative value does not
// get truncated towards it and lose a hundredth every time the window is opened.
int ToHundredths(float value) {
    return static_cast<int>(value * 100.0f + (value >= 0.0f ? 0.5f : -0.5f));
}

void SetControlText(HWND dialog, int id, const std::wstring& text) {
    ::SetDlgItemTextW(dialog, id, text.c_str());
}

std::wstring ControlText(HWND dialog, int id) {
    wchar_t buffer[64] = {};
    ::GetDlgItemTextW(dialog, id, buffer, _countof(buffer));
    return std::wstring(buffer);
}

HWND SliderHandle(HWND dialog, int id) {
    return ::GetDlgItem(dialog, id);
}

int SliderPosition(HWND dialog, int id) {
    return static_cast<int>(
        ::SendMessageW(SliderHandle(dialog, id), kMsgSliderGetPosition, 0, 0));
}

void SetSliderRange(HWND dialog, int id, int minimum, int maximum) {
    ::SendMessageW(SliderHandle(dialog, id), kMsgSliderSetRange, static_cast<WPARAM>(minimum),
                   static_cast<LPARAM>(maximum));
}

void SetSliderPosition(HWND dialog, int id, int position) {
    ::SendMessageW(SliderHandle(dialog, id), kMsgSliderSetPosition, static_cast<WPARAM>(position),
                   0);
}

void SetKeyBoxKey(HWND dialog, int key) {
    ::SendMessageW(::GetDlgItem(dialog, kIdToggleKey), kMsgKeyBoxSetKey, static_cast<WPARAM>(key),
                   0);
}

int KeyBoxKey(HWND dialog) {
    return static_cast<int>(
        ::SendMessageW(::GetDlgItem(dialog, kIdToggleKey), kMsgKeyBoxGetKey, 0, 0));
}

// Grey out whatever belongs to a switched-off feature, so the window shows at a
// glance what is actually in effect.
void UpdateEnabledState(HWND dialog, State& state) {
    ::EnableWindow(SliderHandle(dialog, kIdFpsSlider), state.fpsOn);
    ::EnableWindow(::GetDlgItem(dialog, kIdFpsValue), state.fpsOn);

    ::EnableWindow(SliderHandle(dialog, kIdFovSlider), state.fovOn);
    ::EnableWindow(::GetDlgItem(dialog, kIdFovValue), state.fovOn);
    ::EnableWindow(::GetDlgItem(dialog, kIdFovClamp), state.fovOn);

    // The three viewmodel sliders belong to the one switch above them.
    ::EnableWindow(SliderHandle(dialog, kIdGunXSlider), state.moveGunOn);
    ::EnableWindow(::GetDlgItem(dialog, kIdGunXValue), state.moveGunOn);
    ::EnableWindow(SliderHandle(dialog, kIdGunYSlider), state.moveGunOn);
    ::EnableWindow(::GetDlgItem(dialog, kIdGunYValue), state.moveGunOn);
    ::EnableWindow(SliderHandle(dialog, kIdGunZSlider), state.moveGunOn);
    ::EnableWindow(::GetDlgItem(dialog, kIdGunZValue), state.moveGunOn);

    // The film tweak's sliders are only meaningful with the grade switched on.
    ::EnableWindow(SliderHandle(dialog, kIdFilmContrastSlider), state.filmTweakOn);
    ::EnableWindow(::GetDlgItem(dialog, kIdFilmContrastValue), state.filmTweakOn);
    ::EnableWindow(SliderHandle(dialog, kIdFilmBrightnessSlider), state.filmTweakOn);
    ::EnableWindow(::GetDlgItem(dialog, kIdFilmBrightnessValue), state.filmTweakOn);
    ::EnableWindow(SliderHandle(dialog, kIdFilmDesaturationSlider), state.filmTweakOn);
    ::EnableWindow(::GetDlgItem(dialog, kIdFilmDesaturationValue), state.filmTweakOn);
    ::EnableWindow(SliderHandle(dialog, kIdFilmLightTintSlider), state.filmTweakOn);
    ::EnableWindow(::GetDlgItem(dialog, kIdFilmLightTintValue), state.filmTweakOn);
    ::EnableWindow(SliderHandle(dialog, kIdFilmMediumTintSlider), state.filmTweakOn);
    ::EnableWindow(::GetDlgItem(dialog, kIdFilmMediumTintValue), state.filmTweakOn);
    ::EnableWindow(SliderHandle(dialog, kIdFilmDarkTintSlider), state.filmTweakOn);
    ::EnableWindow(::GetDlgItem(dialog, kIdFilmDarkTintValue), state.filmTweakOn);

    // Redraw the boxes, because a disabled owner-drawn button does not repaint
    // itself when the state changes.
    ::InvalidateRect(::GetDlgItem(dialog, kIdFpsEnable), nullptr, TRUE);
    ::InvalidateRect(::GetDlgItem(dialog, kIdFovEnable), nullptr, TRUE);
    ::InvalidateRect(::GetDlgItem(dialog, kIdFovClamp), nullptr, TRUE);
}

// -----------------------------------------------------------------------------
// Collapsible cards.
//
// The viewmodel and the film tweak each carry a switch and a stack of rows, and
// the rows mean nothing while the switch is off. Rather than leave them greyed
// out, they are hidden and everything below them is pulled up, so the window is
// only as tall as the settings it is actually showing.
//
// Nothing here repeats the template's coordinates. The dialog is measured once
// at start-up - every control's position, converted back into dialog units - and
// each change is applied as an offset from that measurement. The two stay in
// step when the template is edited, and clicking the switches any number of
// times cannot drift, because the offsets are always taken from the original.
// -----------------------------------------------------------------------------

// The gap the template leaves between a card's last control and its bottom edge.
// A collapsed card keeps the same gap under its switch.
constexpr int kCardGap = 6;

struct PlacedControl {
    int id = 0;
    RECT units{};  // where the template put it
    RECT pixels{}; // where that is in the window
};

std::vector<PlacedControl> g_placed;
int g_unitX = 0; // pixels per four horizontal dialog units
int g_unitY = 0; // pixels per eight vertical dialog units

int UnitsToY(int units) {
    return ::MulDiv(units, g_unitY, 8);
}

int YToUnits(int pixels) {
    return ::MulDiv(pixels, 8, g_unitY);
}

int XToUnits(int pixels) {
    return ::MulDiv(pixels, 4, g_unitX);
}

const PlacedControl* FindPlaced(int id) {
    for (const PlacedControl& placed : g_placed) {
        if (placed.id == id) {
            return &placed;
        }
    }
    return nullptr;
}

// The rows that only exist while their card is switched on.
const int kGunRows[] = {kIdGunXLabel, kIdGunXSlider, kIdGunXValue, kIdGunYLabel,
                        kIdGunYSlider, kIdGunYValue, kIdGunZLabel, kIdGunZSlider,
                        kIdGunZValue};

const int kFilmRows[] = {
    kIdFilmContrastLabel,     kIdFilmContrastSlider,     kIdFilmContrastValue,
    kIdFilmBrightnessLabel,   kIdFilmBrightnessSlider,   kIdFilmBrightnessValue,
    kIdFilmDesaturationLabel, kIdFilmDesaturationSlider, kIdFilmDesaturationValue,
    kIdFilmLightTintLabel,    kIdFilmLightTintSlider,    kIdFilmLightTintValue,
    kIdFilmMediumTintLabel,   kIdFilmMediumTintSlider,   kIdFilmMediumTintValue,
    kIdFilmDarkTintLabel,     kIdFilmDarkTintSlider,     kIdFilmDarkTintValue,
};

void ShowRows(HWND dialog, const int* ids, int count, bool visible) {
    for (int i = 0; i < count; ++i) {
        ::ShowWindow(::GetDlgItem(dialog, ids[i]), visible ? SW_SHOW : SW_HIDE);
    }
}

// Measure the template's layout, before anything has been moved. Called once.
void SnapshotLayout(HWND dialog) {
    g_placed.clear();

    RECT base{0, 0, 4, 8};
    ::MapDialogRect(dialog, &base);
    g_unitX = base.right;
    g_unitY = base.bottom;

    for (HWND child = ::GetWindow(dialog, GW_CHILD); child != nullptr;
         child = ::GetWindow(child, GW_HWNDNEXT)) {
        const int id = ::GetDlgCtrlID(child);
        // An unlabelled static has id -1, and none of those ever moves: they all
        // sit in the two cards that cannot collapse.
        if (id <= 0) {
            continue;
        }

        RECT window{};
        ::GetWindowRect(child, &window);
        POINT corner{window.left, window.top};
        ::ScreenToClient(dialog, &corner);

        PlacedControl placed;
        placed.id = id;
        placed.pixels = {corner.x, corner.y, corner.x + (window.right - window.left),
                         corner.y + (window.bottom - window.top)};
        placed.units = {XToUnits(placed.pixels.left), YToUnits(placed.pixels.top),
                        XToUnits(placed.pixels.right), YToUnits(placed.pixels.bottom)};
        g_placed.push_back(placed);
    }
}

// Put the window back together for the two switches' current state. Cheap enough
// to call on every toggle: the work is a few dozen SetWindowPos calls.
void ApplyLayout(HWND dialog, const State& state) {
    const PlacedControl* gunSwitch = FindPlaced(kIdMoveGunEnable);
    const PlacedControl* gunFirstRow = FindPlaced(kIdGunXSlider);
    const PlacedControl* gunLastRow = FindPlaced(kIdGunZSlider);
    const PlacedControl* filmSwitch = FindPlaced(kIdFilmTweakEnable);
    const PlacedControl* filmFirstRow = FindPlaced(kIdFilmContrastSlider);
    const PlacedControl* filmLastRow = FindPlaced(kIdFilmDarkTintSlider);
    if (gunSwitch == nullptr || gunFirstRow == nullptr || gunLastRow == nullptr ||
        filmSwitch == nullptr || filmFirstRow == nullptr || filmLastRow == nullptr) {
        return;
    }

    const bool gunOpen = state.moveGunOn;
    const bool filmOpen = state.filmTweakOn;

    // Both cards hang their rows under their switch, so one rule covers them:
    // closing a card takes away exactly the height those rows occupy, which is
    // the distance from the switch's bottom edge to the last row's. Measured,
    // not written down, so the template can be re-spaced without touching this.
    const int gunDelta = gunOpen ? 0 : gunSwitch->units.bottom - gunLastRow->units.bottom;
    const int filmDelta = filmOpen ? 0 : filmSwitch->units.bottom - filmLastRow->units.bottom;

    for (const PlacedControl& placed : g_placed) {
        // A control moves with a card if it sits at or below the first row that
        // card gives up. The switch is the anchor for neither: it is the thing
        // that opens the card, so it stays exactly where its title put it and
        // the rows come and go underneath it.
        int delta = 0;
        if (placed.units.top >= gunFirstRow->units.top) {
            delta += gunDelta;
        }
        if (placed.units.top >= filmFirstRow->units.top) {
            delta += filmDelta;
        }
        // The rows of a closed card collect its shift as well. That is
        // harmless - they are hidden whenever their own shift is not zero, and
        // it is zero whenever they are showing, which is when their position
        // matters.
        //
        // Always an absolute position, never a nudge: opening a card again makes
        // its offset zero, and skipping those controls would leave them where
        // the collapse had put them.
        ::SetWindowPos(::GetDlgItem(dialog, placed.id), nullptr, placed.pixels.left,
                       placed.pixels.top + UnitsToY(delta), 0, 0,
                       SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    ShowRows(dialog, kGunRows, ARRAYSIZE(kGunRows), gunOpen);
    ShowRows(dialog, kFilmRows, ARRAYSIZE(kFilmRows), filmOpen);

    // The cards. A collapsed one ends just under its switch, and the cards below
    // it follow the same distance down.
    RECT cards[kCardCount];
    for (int i = 0; i < kCardCount; ++i) {
        cards[i] = kCardUnits[i];
    }
    if (!gunOpen) {
        cards[2].bottom = gunSwitch->units.bottom + kCardGap;
    }
    cards[3].top += gunDelta;
    cards[3].bottom += gunDelta;
    cards[4].top += gunDelta;
    cards[4].bottom += gunDelta;
    if (!filmOpen) {
        cards[3].bottom = filmSwitch->units.bottom + kCardGap + gunDelta;
    }
    cards[4].top += filmDelta;
    cards[4].bottom += filmDelta;
    for (int i = 0; i < kCardCount; ++i) {
        g_cards[i] = cards[i];
        ::MapDialogRect(dialog, &g_cards[i]);
    }

    // And the window itself, so its bottom edge follows instead of leaving a
    // field of empty background under the buttons. The frame is whatever the
    // window manager added around the client area.
    RECT window{};
    RECT client{};
    ::GetWindowRect(dialog, &window);
    ::GetClientRect(dialog, &client);
    const int frame = (window.bottom - window.top) - client.bottom;
    ::SetWindowPos(dialog, nullptr, 0, 0, window.right - window.left,
                   UnitsToY(kDialogHeight + gunDelta + filmDelta) + frame,
                   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);

    ::InvalidateRect(dialog, nullptr, TRUE);
}

// Put the window in the middle of the monitor it landed on. The template is the
// fully expanded layout, and the dialog manager centres against that before the
// saved values are read, so a window with both cards folded away would otherwise
// sit well above centre with a gap underneath it.
//
// Only done once, at start-up: re-centring on every toggle would make the window
// jump about while the user is clicking it.
void CentreOnMonitor(HWND dialog) {
    RECT window{};
    ::GetWindowRect(dialog, &window);

    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!::GetMonitorInfoW(::MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &info)) {
        return;
    }

    const int width = window.right - window.left;
    const int height = window.bottom - window.top;
    const int x = info.rcWork.left + ((info.rcWork.right - info.rcWork.left) - width) / 2;
    const int y = info.rcWork.top + ((info.rcWork.bottom - info.rcWork.top) - height) / 2;
    ::SetWindowPos(dialog, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void SyncControls(HWND dialog, State& state) {
    const ipc::Values& values = state.values;

    state.fpsOn = values.fpsEnabled != 0;
    SetSliderRange(dialog, kIdFpsSlider, kFpsMin, kFpsMax);
    SetSliderPosition(dialog, kIdFpsSlider, ClampInt(values.fpsValue, kFpsMin, kFpsMax));
    SetControlText(dialog, kIdFpsValue, FormatInt(ClampInt(values.fpsValue, kFpsMin, kFpsMax)));

    state.fovOn = values.fovEnabled != 0;
    SetSliderRange(dialog, kIdFovSlider, kFovMin, kFovMax);
    const int fov = ClampInt(static_cast<int>(values.fovValue + 0.5f), kFovMin, kFovMax);
    SetSliderPosition(dialog, kIdFovSlider, fov);
    SetControlText(dialog, kIdFovValue, FormatInt(fov));

    state.clampOn = values.fovClampEnabled != 0;

    state.musicOn = values.musicEnabled != 0;
    state.fullbrightOn = values.fullbrightEnabled != 0;
    state.hudOn = values.hudEnabled != 0;
    state.gunOn = values.gunEnabled != 0;
    state.fogOn = values.fogEnabled != 0;
    state.filmTweakOn = values.filmTweakEnabled != 0;

    // The viewmodel offsets have their own flag. Reading gunEnabled here instead
    // meant the tick mirrored the "hide the weapon model" switch, so a
    // [cg_gun_x] section switched on in the file came back unticked - and the
    // next Apply then wrote that unticked state back over all three sections.
    state.moveGunOn = values.moveGunEnabled != 0;
    state.gunX = ClampInt(ToHundredths(values.gunX), kGunMin, kGunMax);
    state.gunY = ClampInt(ToHundredths(values.gunY), kGunMin, kGunMax);
    state.gunZ = ClampInt(ToHundredths(values.gunZ), kGunMin, kGunMax);
    SetSliderRange(dialog, kIdGunXSlider, kGunMin, kGunMax);
    SetSliderRange(dialog, kIdGunYSlider, kGunMin, kGunMax);
    SetSliderRange(dialog, kIdGunZSlider, kGunMin, kGunMax);
    SetSliderPosition(dialog, kIdGunXSlider, state.gunX);
    SetSliderPosition(dialog, kIdGunYSlider, state.gunY);
    SetSliderPosition(dialog, kIdGunZSlider, state.gunZ);
    SetControlText(dialog, kIdGunXValue, FormatHundredths(state.gunX));
    SetControlText(dialog, kIdGunYValue, FormatHundredths(state.gunY));
    SetControlText(dialog, kIdGunZValue, FormatHundredths(state.gunZ));

    state.filmContrast = ClampInt(ToHundredths(values.filmContrast), kContrastMin, kContrastMax);
    state.filmBrightness =
        ClampInt(ToHundredths(values.filmBrightness), kBrightnessMin, kBrightnessMax);
    state.filmDesaturation =
        ClampInt(ToHundredths(values.filmDesaturation), kDesaturationMin, kDesaturationMax);
    SetSliderRange(dialog, kIdFilmContrastSlider, kContrastMin, kContrastMax);
    SetSliderRange(dialog, kIdFilmBrightnessSlider, kBrightnessMin, kBrightnessMax);
    SetSliderRange(dialog, kIdFilmDesaturationSlider, kDesaturationMin, kDesaturationMax);
    SetSliderPosition(dialog, kIdFilmContrastSlider, state.filmContrast);
    SetSliderPosition(dialog, kIdFilmBrightnessSlider, state.filmBrightness);
    SetSliderPosition(dialog, kIdFilmDesaturationSlider, state.filmDesaturation);
    SetControlText(dialog, kIdFilmContrastValue, FormatHundredths(state.filmContrast));
    SetControlText(dialog, kIdFilmBrightnessValue, FormatHundredths(state.filmBrightness));
    SetControlText(dialog, kIdFilmDesaturationValue, FormatHundredths(state.filmDesaturation));

    state.filmLightTint = ClampInt(ToHundredths(values.filmLightTint), kTintMin, kTintMax);
    state.filmMediumTint = ClampInt(ToHundredths(values.filmMediumTint), kTintMin, kTintMax);
    state.filmDarkTint = ClampInt(ToHundredths(values.filmDarkTint), kTintMin, kTintMax);
    SetSliderRange(dialog, kIdFilmLightTintSlider, kTintMin, kTintMax);
    SetSliderRange(dialog, kIdFilmMediumTintSlider, kTintMin, kTintMax);
    SetSliderRange(dialog, kIdFilmDarkTintSlider, kTintMin, kTintMax);
    SetSliderPosition(dialog, kIdFilmLightTintSlider, state.filmLightTint);
    SetSliderPosition(dialog, kIdFilmMediumTintSlider, state.filmMediumTint);
    SetSliderPosition(dialog, kIdFilmDarkTintSlider, state.filmDarkTint);
    SetControlText(dialog, kIdFilmLightTintValue, FormatHundredths(state.filmLightTint));
    SetControlText(dialog, kIdFilmMediumTintValue, FormatHundredths(state.filmMediumTint));
    SetControlText(dialog, kIdFilmDarkTintValue, FormatHundredths(state.filmDarkTint));

    SetKeyBoxKey(dialog, values.toggleKey);

    UpdateEnabledState(dialog, state);
}

void LoadValues(State& state) {
    state.values = ipc::MakeDefault();

    const std::wstring& path = state.iniPath;
    state.values.fpsEnabled = app::IniInt(path, L"fps", L"enabled", state.values.fpsEnabled) != 0;
    state.values.fpsValue = app::IniInt(path, L"fps", L"value", state.values.fpsValue);

    state.values.fovEnabled = app::IniInt(path, L"fov", L"enabled", state.values.fovEnabled) != 0;
    state.values.fovValue = app::IniFloat(path, L"fov", L"value", state.values.fovValue);

    // The clamp doubles as its own switch: a value of 0 tells the DLL to leave
    // the engine's own limit alone.
    const float clampMax = app::IniFloat(path, L"fov", L"max", 0.0f);
    state.values.fovClampEnabled = clampMax > 0.0f;

    // The on-screen counters have no controls yet, but they are still settings:
    // read them so that pushing the values back does not switch off something
    // the config file turned on.
    state.values.counterEnabled =
        app::IniInt(path, L"drawfps", L"enabled", state.values.counterEnabled) != 0;
    state.values.counterMode = app::IniInt(path, L"drawfps", L"value", state.values.counterMode);
    state.values.netFpsEnabled =
        app::IniInt(path, L"netfps", L"enabled", state.values.netFpsEnabled) != 0;

    // 0x75 is F6. Read as hex, which is how the config writes it.
    state.values.toggleKey = app::IniInt(path, L"general", L"toggleKey", 0x75);

    state.values.sensitivityEnabled =
        app::IniInt(path, L"sensitivity", L"enabled", state.values.sensitivityEnabled) != 0;
    state.values.sensitivityValue = app::IniFloat(path, L"sensitivity", L"value",
                                                  state.values.sensitivityValue);

    // The three viewmodel offsets are one setting, so [cg_gun_x] carries the
    // enabled flag for all of them and each section carries its own value.
    state.values.moveGunEnabled =
        app::IniInt(path, L"cg_gun_x", L"enabled", state.values.moveGunEnabled) != 0;
    state.values.gunX = app::IniFloat(path, L"cg_gun_x", L"value", state.values.gunX);
    state.values.gunY = app::IniFloat(path, L"cg_gun_y", L"value", state.values.gunY);
    state.values.gunZ = app::IniFloat(path, L"cg_gun_z", L"value", state.values.gunZ);

    // The film tweak and its three scalars.
    state.values.filmTweakEnabled =
        app::IniInt(path, L"r_filmTweakEnable", L"enabled", state.values.filmTweakEnabled) != 0;
    state.values.filmContrast =
        app::IniFloat(path, L"r_filmTweakContrast", L"value", state.values.filmContrast);
    state.values.filmBrightness =
        app::IniFloat(path, L"r_filmTweakBrightness", L"value", state.values.filmBrightness);
    state.values.filmDesaturation =
        app::IniFloat(path, L"r_filmTweakDesaturation", L"value", state.values.filmDesaturation);
    state.values.filmLightTint =
        app::IniFloat(path, L"r_filmTweakLightTint", L"value", state.values.filmLightTint);
    state.values.filmMediumTint =
        app::IniFloat(path, L"r_filmTweakMediumTint", L"value", state.values.filmMediumTint);
    state.values.filmDarkTint =
        app::IniFloat(path, L"r_filmTweakDarkTint", L"value", state.values.filmDarkTint);

    // Every switch that has a control in the window. These were missing, which
    // meant a section switched on in the file showed as unticked here - and the
    // next Apply then wrote the unticked state back over it, switching the
    // feature off.
    state.values.musicEnabled =
        app::IniInt(path, L"music", L"enabled", state.values.musicEnabled) != 0;
    state.values.fullbrightEnabled =
        app::IniInt(path, L"r_fullbright", L"enabled", state.values.fullbrightEnabled) != 0;
    state.values.hudEnabled =
        app::IniInt(path, L"cg_draw2D", L"enabled", state.values.hudEnabled) != 0;
    state.values.gunEnabled =
        app::IniInt(path, L"cg_drawGun", L"enabled", state.values.gunEnabled) != 0;
    state.values.fogEnabled = app::IniInt(path, L"r_fog", L"enabled", state.values.fogEnabled) != 0;

    // The one setting here that is not a game cvar at all.
    state.closeWithGame = app::IniInt(path, L"general", L"closeWithGame", 1) != 0;
}

void ReadControls(HWND dialog, State& state) {
    ipc::Values& values = state.values;

    values.fpsEnabled = state.fpsOn ? 1 : 0;
    values.fpsValue = SliderPosition(dialog, kIdFpsSlider);

    values.fovEnabled = state.fovOn ? 1 : 0;
    values.fovValue = static_cast<float>(SliderPosition(dialog, kIdFovSlider));

    values.fovClampEnabled = state.clampOn ? 1 : 0;
    values.fovClampValue = kFovClampValue;

    values.musicEnabled = state.musicOn ? 1 : 0;
    values.fullbrightEnabled = state.fullbrightOn ? 1 : 0;
    values.hudEnabled = state.hudOn ? 1 : 0;
    values.gunEnabled = state.gunOn ? 1 : 0;
    values.fogEnabled = state.fogOn ? 1 : 0;
    values.filmTweakEnabled = state.filmTweakOn ? 1 : 0;

    // One switch, three values: the window writes the same enabled flag to all
    // three sections, because moving the viewmodel in one axis alone is not a
    // thing anyone wants.
    values.moveGunEnabled = state.moveGunOn ? 1 : 0;
    values.gunX = static_cast<float>(SliderPosition(dialog, kIdGunXSlider)) / 100.0f;
    values.gunY = static_cast<float>(SliderPosition(dialog, kIdGunYSlider)) / 100.0f;
    values.gunZ = static_cast<float>(SliderPosition(dialog, kIdGunZSlider)) / 100.0f;

    // The film tweak's scalars share its switch.
    values.filmContrast =
        static_cast<float>(SliderPosition(dialog, kIdFilmContrastSlider)) / 100.0f;
    values.filmBrightness =
        static_cast<float>(SliderPosition(dialog, kIdFilmBrightnessSlider)) / 100.0f;
    values.filmDesaturation =
        static_cast<float>(SliderPosition(dialog, kIdFilmDesaturationSlider)) / 100.0f;

    // The three tints are colour dvars in the game - four floats each - but the
    // engine writes the same bytes to every offset the section lists, so one
    // number here becomes a neutral grey tint. See src/features.cpp.
    values.filmLightTint =
        static_cast<float>(SliderPosition(dialog, kIdFilmLightTintSlider)) / 100.0f;
    values.filmMediumTint =
        static_cast<float>(SliderPosition(dialog, kIdFilmMediumTintSlider)) / 100.0f;
    values.filmDarkTint =
        static_cast<float>(SliderPosition(dialog, kIdFilmDarkTintSlider)) / 100.0f;

    values.toggleKey = KeyBoxKey(dialog);

    // sensitivityEnabled and sensitivityValue are deliberately left exactly as
    // they were read from the file. This window has no controls for them, so
    // Apply must not overwrite them with a default - and the [sensitivity]
    // section keeps whatever value it already had.
}

bool WriteValuesTo(const std::wstring& path, const ipc::Values& values) {
    bool ok = true;

    auto set = [&path, &ok](const wchar_t* section, const wchar_t* key, const std::wstring& value) {
        ok = app::SetIniValue(path, section, key, value) && ok;
    };

    set(L"fps", L"enabled", values.fpsEnabled ? L"1" : L"0");
    set(L"fps", L"value", std::to_wstring(values.fpsValue));

    set(L"fov", L"enabled", values.fovEnabled ? L"1" : L"0");
    set(L"fov", L"value", FormatFloat(values.fovValue));
    set(L"fov", L"max", values.fovClampEnabled ? FormatFloat(values.fovClampValue) : L"0");

    set(L"music", L"enabled", values.musicEnabled ? L"1" : L"0");
    set(L"r_fullbright", L"enabled", values.fullbrightEnabled ? L"1" : L"0");
    set(L"cg_draw2D", L"enabled", values.hudEnabled ? L"1" : L"0");
    set(L"cg_drawGun", L"enabled", values.gunEnabled ? L"1" : L"0");
    set(L"r_fog", L"enabled", values.fogEnabled ? L"1" : L"0");
    set(L"r_filmTweakEnable", L"enabled", values.filmTweakEnabled ? L"1" : L"0");

    // The viewmodel offsets: three sections, one shared enabled flag.
    set(L"cg_gun_x", L"enabled", values.moveGunEnabled ? L"1" : L"0");
    set(L"cg_gun_x", L"value", FormatFloat(values.gunX));
    set(L"cg_gun_y", L"enabled", values.moveGunEnabled ? L"1" : L"0");
    set(L"cg_gun_y", L"value", FormatFloat(values.gunY));
    set(L"cg_gun_z", L"enabled", values.moveGunEnabled ? L"1" : L"0");
    set(L"cg_gun_z", L"value", FormatFloat(values.gunZ));

    // The film tweak's scalars: written whether or not the grade is on, so the
    // values are kept for next time - the enabled flag is what decides whether
    // they are applied.
    set(L"r_filmTweakContrast", L"enabled", values.filmTweakEnabled ? L"1" : L"0");
    set(L"r_filmTweakContrast", L"value", FormatFloat(values.filmContrast));
    set(L"r_filmTweakBrightness", L"enabled", values.filmTweakEnabled ? L"1" : L"0");
    set(L"r_filmTweakBrightness", L"value", FormatFloat(values.filmBrightness));
    set(L"r_filmTweakDesaturation", L"enabled", values.filmTweakEnabled ? L"1" : L"0");
    set(L"r_filmTweakDesaturation", L"value", FormatFloat(values.filmDesaturation));

    // The tints go into the same section pattern; each section names the three
    // colour components of its dvar in valueOffsets.
    set(L"r_filmTweakLightTint", L"enabled", values.filmTweakEnabled ? L"1" : L"0");
    set(L"r_filmTweakLightTint", L"value", FormatFloat(values.filmLightTint));
    set(L"r_filmTweakMediumTint", L"enabled", values.filmTweakEnabled ? L"1" : L"0");
    set(L"r_filmTweakMediumTint", L"value", FormatFloat(values.filmMediumTint));
    set(L"r_filmTweakDarkTint", L"enabled", values.filmTweakEnabled ? L"1" : L"0");
    set(L"r_filmTweakDarkTint", L"value", FormatFloat(values.filmDarkTint));

    set(L"general", L"toggleKey", FormatHex(values.toggleKey));

    set(L"sensitivity", L"enabled", values.sensitivityEnabled ? L"1" : L"0");
    set(L"sensitivity", L"value", FormatFloat(values.sensitivityValue));

    // The counters are deliberately not written: they have no controls, so the
    // file stays the place where they are set.
    return ok;
}

bool SaveValues(State& state, std::wstring& error) {
    if (!WriteValuesTo(state.iniPath, state.values)) {
        error = L"Could not update " + state.iniPath;
        return false;
    }

    // closeWithGame is a launcher setting rather than a game cvar, so it is not
    // part of ipc::Values and is written here instead.
    const std::wstring closeWithGame = state.closeWithGame ? L"1" : L"0";
    app::SetIniValue(state.iniPath, L"general", L"closeWithGame", closeWithGame);

    // Keep the copy next to the EXE in step as well. It is the file the user
    // sees, and it is copied over the working copy on every start - so a change
    // that only reached the working copy would be undone by the next run.
    if (::GetFileAttributesW(state.nextToExeIniPath.c_str()) != INVALID_FILE_ATTRIBUTES) {
        WriteValuesTo(state.nextToExeIniPath, state.values);
        app::SetIniValue(state.nextToExeIniPath, L"general", L"closeWithGame", closeWithGame);
    }
    return true;
}

void PostStatus(State& state, const std::wstring& text) {
    ::EnterCriticalSection(&state.lock);
    state.status = text;
    ::LeaveCriticalSection(&state.lock);
    ::PostMessageW(state.dialog, kMsgStatus, 0, 0);
}

// -----------------------------------------------------------------------------
// Background work: find the game, inject, report, and close the window when the
// game exits. Kept off the window's thread so the interface never freezes while
// waiting for a game to appear or for an injection to finish.
// -----------------------------------------------------------------------------
DWORD WINAPI WorkerProc(LPVOID parameter) {
    auto* state = static_cast<State*>(parameter);
    State& s = *state;

    PostStatus(s, L"Looking for the game...");

    app::Game game;
    for (int attempt = 0; attempt < 120; ++attempt) { // up to a minute
        if (::WaitForSingleObject(s.stopEvent, 0) == WAIT_OBJECT_0) {
            return 0;
        }
        if (app::FindGame(game)) {
            break;
        }
        ::Sleep(500);
    }

    if (game.pid == 0) {
        PostStatus(s, L"The game is not running. Your settings are saved to unlocker.ini and "
                      L"will be applied when the unlocker is injected.");
        return 0;
    }

    if (app::Is32Bit(game.pid)) {
        PostStatus(s, game.name + L" is a 32-bit client, so this 64-bit unlocker cannot load "
                                L"into it. Use iw4mp.exe or iw4sp.exe.");
        return 0;
    }

    // Where the game lives, and which client it is: between them that is the path
    // of its own settings file.
    s.gameName = game.name;
    std::wstring imagePath;
    if (app::ProcessPath(game.pid, imagePath)) {
        const size_t slash = imagePath.find_last_of(L"\\/");
        if (slash != std::wstring::npos) {
            s.gameDirectory = imagePath.substr(0, slash);
        }
    }

    if (app::IsUnlockerLoaded(game.pid)) {
        PostStatus(s, std::wstring(L"The unlocker is already injected into ") + game.name +
                          L". Changes still apply live; F6 in game toggles it off and on.");
    } else {
        PostStatus(s, std::wstring(L"Injecting into ") + game.name + L"...");
        std::wstring error;
        if (!app::Inject(game.pid, s.dllPath, error)) {
            PostStatus(s, std::wstring(L"Injection failed: ") + error +
                              L"  (if the game runs as administrator, so must this window)");
            return 0;
        }
    }

    const HANDLE process = ::OpenProcess(SYNCHRONIZE, FALSE, game.pid);

    for (;;) {
        if (::WaitForSingleObject(s.stopEvent, 0) == WAIT_OBJECT_0) {
            break;
        }

        // The DLL creates its control window only after delayMs, so this can take
        // a few seconds. Until it appears the window can save settings but cannot
        // apply them to the running game.
        if (app::LiveChannelPresent()) {
            PostStatus(s, std::wstring(L"Connected to ") + game.name +
                              L". Apply changes the game immediately.");
            break;
        }

        if (::WaitForSingleObject(s.stopEvent, 250) == WAIT_OBJECT_0) {
            break;
        }
    }

    // Once connected, stay until the game goes away.
    if (process != nullptr) {
        for (;;) {
            if (::WaitForSingleObject(s.stopEvent, 0) == WAIT_OBJECT_0) {
                break;
            }
            if (::WaitForSingleObject(process, 250) == WAIT_OBJECT_0) {
                std::wstring status = game.name + L" has exited.";

                // The game saves its own settings over its config file as it
                // shuts down, which undoes a sensitivity line written while it was
                // running - so it is written again now that the file is quiet.
                // Re-read from the config rather than from the window, which may
                // have been closed or changed since.
                if (!s.gameDirectory.empty() &&
                    app::IniInt(s.iniPath, L"sensitivity", L"enabled", 0) != 0) {
                    const float value = app::IniFloat(s.iniPath, L"sensitivity", L"value", 5.0f);
                    const std::wstring path = app::GameConfigPath(s.gameDirectory, s.gameName);
                    if (app::SetGameConfigValue(path, L"sensitivity", FormatFloat(value))) {
                        status += L"  Sensitivity written again, since the game overwrites its "
                                  L"settings file on exit.";
                    }
                }

                PostStatus(s, status);
                // Read now rather than when the wait started: the box is in the
                // window, so it can be ticked while the game is already running.
                if (app::IniInt(s.iniPath, L"general", L"closeWithGame", 1) != 0) {
                    ::PostMessageW(s.dialog, WM_CLOSE, 0, 0);
                }
                break;
            }
        }
        ::CloseHandle(process);
    }

    return 0;
}

void OnApply(HWND dialog, State& state) {
    ReadControls(dialog, state);

    std::wstring error;
    if (!SaveValues(state, error)) {
        SetControlText(dialog, kIdStatus, error);
        return;
    }

    std::wstring status;
    if (app::PushLive(state.values)) {
        status = L"Saved, and applied to the running game.";
    } else if (app::LiveChannelPresent()) {
        status = L"Saved, but the game did not answer. See "
                 L"%LOCALAPPDATA%\\MW2Unlocker\\mw2_unlocker.log.";
    } else {
        status = L"Saved to unlocker.ini. It applies a few seconds after the unlocker is "
                 L"injected, or on the next launch.";
    }

    // Sensitivity is live like everything else - the mouse-look code reads the
    // cvar value every frame - but the game's own settings file is written too,
    // because that is what keeps the value set on a launch without the unlocker.
    // The game reloads it at startup and saves its own copy over it on exit.
    if (state.values.sensitivityEnabled != 0 && !state.gameDirectory.empty()) {
        const bool multiplayer = _wcsicmp(state.gameName.c_str(), L"iw4mp.exe") == 0;
        const std::wstring path = app::GameConfigPath(state.gameDirectory, state.gameName);

        if (app::SetGameConfigValue(path, L"sensitivity",
                                    FormatFloat(state.values.sensitivityValue))) {
            status += std::wstring(L"  Also written to players\\") +
                      (multiplayer ? L"config_mp.cfg" : L"config.cfg") +
                      L", so the value stays set when the unlocker is not running."
                      L"  The game saves its own settings over that file as it closes; the "
                      L"unlocker writes the line again afterwards.";
        } else {
            status += L"  ! The game's own settings file could not be updated.";
        }
    }

    SetControlText(dialog, kIdStatus, status);
}

INT_PTR CALLBACK DialogProc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* state = reinterpret_cast<State*>(::GetWindowLongPtrW(dialog, DWLP_USER));

    switch (message) {
    case WM_INITDIALOG: {
        state = reinterpret_cast<State*>(lparam);
        ::SetWindowLongPtrW(dialog, DWLP_USER, lparam);
        state->dialog = dialog;

        // Dark caption bar. Ignored on older Windows, which is fine: the rest of
        // the window still paints itself.
        BOOL dark = TRUE;
        ::DwmSetWindowAttribute(dialog, 20 /* DWMWA_USE_IMMERSIVE_DARK_MODE */, &dark,
                                sizeof(dark));

        // Dark edit boxes as well, via the theme Explorer itself uses, and via
        // the colours the edit control asks its parent for.
        ::SetWindowTheme(::GetDlgItem(dialog, kIdFpsValue), L"DarkMode_CFD", nullptr);
        ::SetWindowTheme(::GetDlgItem(dialog, kIdFovValue), L"DarkMode_CFD", nullptr);
        ::SetWindowTheme(::GetDlgItem(dialog, kIdGunXValue), L"DarkMode_CFD", nullptr);
        ::SetWindowTheme(::GetDlgItem(dialog, kIdGunYValue), L"DarkMode_CFD", nullptr);
        ::SetWindowTheme(::GetDlgItem(dialog, kIdGunZValue), L"DarkMode_CFD", nullptr);
        ::SetWindowTheme(::GetDlgItem(dialog, kIdFilmContrastValue), L"DarkMode_CFD", nullptr);
        ::SetWindowTheme(::GetDlgItem(dialog, kIdFilmBrightnessValue), L"DarkMode_CFD", nullptr);
        ::SetWindowTheme(::GetDlgItem(dialog, kIdFilmDesaturationValue), L"DarkMode_CFD", nullptr);
        ::SetWindowTheme(::GetDlgItem(dialog, kIdFilmLightTintValue), L"DarkMode_CFD", nullptr);
        ::SetWindowTheme(::GetDlgItem(dialog, kIdFilmMediumTintValue), L"DarkMode_CFD", nullptr);
        ::SetWindowTheme(::GetDlgItem(dialog, kIdFilmDarkTintValue), L"DarkMode_CFD", nullptr);

        // The cards are painted by the window itself and the two collapsible ones
        // move, so the template is measured here and the rectangles are worked out
        // in ApplyLayout, once the saved values have been read.
        //
        // There is no mouse sensitivity card, and that is deliberate: writing the
        // cvar was measured not to change the aim in game, so a slider for it
        // promised something that did not happen. The [sensitivity] section is
        // still in the config and still applied, so anyone who wants to try it
        // again can set it by hand without touching any code.
        SnapshotLayout(dialog);

        const HFONT base = reinterpret_cast<HFONT>(::SendMessageW(dialog, WM_GETFONT, 0, 0));
        LOGFONTW logFont{};
        if (base != nullptr && ::GetObjectW(base, sizeof(logFont), &logFont) == sizeof(logFont)) {
            logFont.lfWeight = FW_SEMIBOLD;
            g_headerFont = ::CreateFontIndirectW(&logFont);
        }
        if (g_headerFont != nullptr) {
            // Every card title, including the three that were split out of the
            // old catch-all card.
            ::SendDlgItemMessageW(dialog, kIdFpsHeader, WM_SETFONT,
                                  reinterpret_cast<WPARAM>(g_headerFont), TRUE);
            ::SendDlgItemMessageW(dialog, kIdFovHeader, WM_SETFONT,
                                  reinterpret_cast<WPARAM>(g_headerFont), TRUE);
            ::SendDlgItemMessageW(dialog, kIdViewModelHeader, WM_SETFONT,
                                  reinterpret_cast<WPARAM>(g_headerFont), TRUE);
            ::SendDlgItemMessageW(dialog, kIdFilmHeader, WM_SETFONT,
                                  reinterpret_cast<WPARAM>(g_headerFont), TRUE);
            ::SendDlgItemMessageW(dialog, kIdOtherHeader, WM_SETFONT,
                                  reinterpret_cast<WPARAM>(g_headerFont), TRUE);
        }

        SyncControls(dialog, *state);

        // Now that the saved values are in: fold away whatever is switched off,
        // and size the window around what is left. Doing it before the window is
        // shown means it never appears at the wrong height.
        ApplyLayout(dialog, *state);
        CentreOnMonitor(dialog);
        SetControlText(dialog, kIdStatus, L"Looking for the game...");

        state->worker = ::CreateThread(nullptr, 0, &WorkerProc, state, 0, nullptr);
        if (state->worker == nullptr) {
            SetControlText(dialog, kIdStatus, L"Could not start the background worker.");
        }
        return TRUE;
    }

    case WM_ERASEBKGND: {
        RECT client{};
        ::GetClientRect(dialog, &client);
        ::FillRect(reinterpret_cast<HDC>(wparam), &client, g_backgroundBrush);
        return TRUE;
    }

    case WM_PAINT: {
        PAINTSTRUCT paint{};
        const HDC dc = ::BeginPaint(dialog, &paint);
        for (const RECT& card : g_cards) {
            FillRounded(dc, card, 10, g_cardBrush);
        }
        ::EndPaint(dialog, &paint);
        return TRUE;
    }

    case WM_CTLCOLORSTATIC: {
        const HDC dc = reinterpret_cast<HDC>(wparam);
        const HWND control = reinterpret_cast<HWND>(lparam);
        const int id = ::GetDlgCtrlID(control);
        // Every card title takes the accent colour. The list used to name the
        // old catch-all card's title instead of the three that were split out of
        // it, so those three painted in the same dim grey as body text.
        const bool header = (id == kIdFpsHeader || id == kIdFovHeader ||
                             id == kIdViewModelHeader || id == kIdFilmHeader ||
                             id == kIdOtherHeader);
        ::SetBkMode(dc, TRANSPARENT);
        ::SetTextColor(dc, header ? kAccent : kTextDim);
        return reinterpret_cast<INT_PTR>(g_cardBrush);
    }

    case WM_CTLCOLOREDIT: {
        const HDC dc = reinterpret_cast<HDC>(wparam);
        ::SetTextColor(dc, kText);
        ::SetBkColor(dc, kGroove);
        return reinterpret_cast<INT_PTR>(g_grooveBrush);
    }

    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        const bool focused = (item->itemState & ODS_FOCUS) != 0;
        const bool disabled = (item->itemState & ODS_DISABLED) != 0;

        switch (item->CtlID) {
        case kIdFpsEnable:
            DrawCheckbox(item->hDC, item->rcItem, L"Cap the frame rate",
                         IsChecked(*state, kIdFpsEnable), focused, disabled);
            return TRUE;
        case kIdFovEnable:
            DrawCheckbox(item->hDC, item->rcItem, L"Change the field of view",
                         IsChecked(*state, kIdFovEnable), focused, disabled);
            return TRUE;
        case kIdFovClamp:
            DrawCheckbox(item->hDC, item->rcItem, L"Raise the engine's 80-degree clamp",
                         IsChecked(*state, kIdFovClamp), focused, disabled);
            return TRUE;
        case kIdMusicEnable:
            DrawCheckbox(item->hDC, item->rcItem, L"Mute the music",
                         IsChecked(*state, kIdMusicEnable), focused, disabled);
            return TRUE;
        case kIdFullbrightEnable:
            DrawCheckbox(item->hDC, item->rcItem, L"Fullbright world",
                         IsChecked(*state, kIdFullbrightEnable), focused, disabled);
            return TRUE;
        case kIdHudEnable:
            DrawCheckbox(item->hDC, item->rcItem, L"Hide the HUD",
                         IsChecked(*state, kIdHudEnable), focused, disabled);
            return TRUE;
        case kIdGunEnable:
            DrawCheckbox(item->hDC, item->rcItem, L"Hide the weapon model",
                         IsChecked(*state, kIdGunEnable), focused, disabled);
            return TRUE;
        case kIdFogEnable:
            DrawCheckbox(item->hDC, item->rcItem, L"Disable the fog",
                         IsChecked(*state, kIdFogEnable), focused, disabled);
            return TRUE;
        case kIdFilmTweakEnable:
            // The two expandable cards draw a triangle as well: without it
            // there is nothing to say the rows exist at all.
            DrawExpandableCheckbox(item->hDC, item->rcItem, L"Enable the film tweak",
                                   IsChecked(*state, kIdFilmTweakEnable), state->filmTweakOn,
                                   focused, disabled);
            return TRUE;
        case kIdMoveGunEnable:
            DrawExpandableCheckbox(item->hDC, item->rcItem, L"Move the viewmodel",
                                   IsChecked(*state, kIdMoveGunEnable), state->moveGunOn,
                                   focused, disabled);
            return TRUE;
        case kIdCloseWithGame:
            DrawCheckbox(item->hDC, item->rcItem, L"Close with the game",
                         IsChecked(*state, kIdCloseWithGame), focused, disabled);
            return TRUE;
        case kIdApply:
            DrawButton(item->hDC, item->rcItem, L"Apply & save", true,
                       (item->itemState & ODS_SELECTED) != 0, focused, disabled);
            return TRUE;
        case IDCANCEL:
            DrawButton(item->hDC, item->rcItem, L"Close", false,
                       (item->itemState & ODS_SELECTED) != 0, focused, disabled);
            return TRUE;
        default:
            return FALSE;
        }
    }

    case kMsgSliderChanged: {
        // A slider moved: keep its number box in step. Nothing is written until
        // Apply, so a drag never leaves the game half-changed.
        const int id = static_cast<int>(wparam);
        const int position = static_cast<int>(lparam);
        // The viewmodel offsets and the film tweak's scalars are decimals and
        // keep their boxes in hundredths; the frame cap and the field of view are
        // whole numbers.
        const int box = id == kIdGunXSlider           ? kIdGunXValue
                        : id == kIdGunYSlider         ? kIdGunYValue
                        : id == kIdGunZSlider         ? kIdGunZValue
                        : id == kIdFilmContrastSlider ? kIdFilmContrastValue
                        : id == kIdFilmBrightnessSlider   ? kIdFilmBrightnessValue
                        : id == kIdFilmDesaturationSlider ? kIdFilmDesaturationValue
                        : id == kIdFilmLightTintSlider ? kIdFilmLightTintValue
                        : id == kIdFilmMediumTintSlider ? kIdFilmMediumTintValue
                        : id == kIdFilmDarkTintSlider ? kIdFilmDarkTintValue
                        : (id == kIdFpsSlider ? kIdFpsValue : kIdFovValue);
        const bool decimal = id == kIdGunXSlider || id == kIdGunYSlider || id == kIdGunZSlider ||
                             id == kIdFilmContrastSlider || id == kIdFilmBrightnessSlider ||
                             id == kIdFilmDesaturationSlider || id == kIdFilmLightTintSlider ||
                             id == kIdFilmMediumTintSlider || id == kIdFilmDarkTintSlider;
        SetControlText(dialog, box, decimal ? FormatHundredths(position) : FormatInt(position));
        return TRUE;
    }

    case kMsgKeyChanged: {
        // The hotkey box stepped to a new key. It only takes effect on Apply,
        // like everything else in this window.
        if (state == nullptr) {
            return TRUE;
        }
        state->values.toggleKey = static_cast<int>(lparam);
        return TRUE;
    }

    case WM_COMMAND: {
        if (state == nullptr) {
            return FALSE;
        }
        const int id = LOWORD(wparam);
        const int notification = HIWORD(wparam);

        if (id == kIdFpsEnable || id == kIdFovEnable || id == kIdFovClamp ||
            id == kIdMusicEnable || id == kIdFullbrightEnable || id == kIdHudEnable ||
            id == kIdGunEnable || id == kIdFogEnable || id == kIdFilmTweakEnable ||
            id == kIdMoveGunEnable || id == kIdCloseWithGame) {
            if (notification == BN_CLICKED) {
                bool* field = CheckState(*state, id);
                if (field != nullptr) {
                    *field = !*field;
                }
                UpdateEnabledState(dialog, *state);
                if (id == kIdMoveGunEnable || id == kIdFilmTweakEnable) {
                    // One of the two collapsible cards: show or hide its rows and
                    // resize the window around whatever is left.
                    ApplyLayout(dialog, *state);
                }
                ::InvalidateRect(::GetDlgItem(dialog, id), nullptr, TRUE);
            }
            return TRUE;
        }

        if (id == kIdFpsValue || id == kIdFovValue || id == kIdGunXValue ||
            id == kIdGunYValue || id == kIdGunZValue || id == kIdFilmContrastValue ||
            id == kIdFilmBrightnessValue || id == kIdFilmDesaturationValue ||
            id == kIdFilmLightTintValue || id == kIdFilmMediumTintValue ||
            id == kIdFilmDarkTintValue) {
            if (notification == EN_KILLFOCUS) {
                const std::wstring typed = ControlText(dialog, id);
                if (id != kIdFpsValue && id != kIdFovValue) {
                    // Typed as a decimal - "1.25" - and folded into hundredths.
                    // Every one of these has its own range, so the slider it
                    // belongs to is looked up along with the bounds.
                    int slider = kIdGunXSlider;
                    int low = kGunMin;
                    int high = kGunMax;
                    switch (id) {
                    case kIdGunYValue: slider = kIdGunYSlider; break;
                    case kIdGunZValue: slider = kIdGunZSlider; break;
                    case kIdFilmContrastValue:
                        slider = kIdFilmContrastSlider;
                        low = kContrastMin;
                        high = kContrastMax;
                        break;
                    case kIdFilmBrightnessValue:
                        slider = kIdFilmBrightnessSlider;
                        low = kBrightnessMin;
                        high = kBrightnessMax;
                        break;
                    case kIdFilmDesaturationValue:
                        slider = kIdFilmDesaturationSlider;
                        low = kDesaturationMin;
                        high = kDesaturationMax;
                        break;
                    case kIdFilmLightTintValue:
                        slider = kIdFilmLightTintSlider;
                        low = kTintMin;
                        high = kTintMax;
                        break;
                    case kIdFilmMediumTintValue:
                        slider = kIdFilmMediumTintSlider;
                        low = kTintMin;
                        high = kTintMax;
                        break;
                    case kIdFilmDarkTintValue:
                        slider = kIdFilmDarkTintSlider;
                        low = kTintMin;
                        high = kTintMax;
                        break;
                    default:
                        break;
                    }
                    const int clamped = ClampInt(
                        ToHundredths(static_cast<float>(::wcstod(typed.c_str(), nullptr))), low,
                        high);
                    SetSliderPosition(dialog, slider, clamped);
                    SetControlText(dialog, id, FormatHundredths(clamped));
                } else {
                    // Typed as a whole number, then clamped to the slider's range
                    // so the two never disagree.
                    const bool isFps = (id == kIdFpsValue);
                    const int clamped =
                        ClampInt(static_cast<int>(::wcstol(typed.c_str(), nullptr, 10)),
                                 isFps ? kFpsMin : kFovMin, isFps ? kFpsMax : kFovMax);
                    SetSliderPosition(dialog, isFps ? kIdFpsSlider : kIdFovSlider, clamped);
                    SetControlText(dialog, id, FormatInt(clamped));
                }
            }
            return TRUE;
        }

        if (id == kIdApply && notification == BN_CLICKED) {
            OnApply(dialog, *state);
            return TRUE;
        }

        if (id == IDCANCEL && notification == BN_CLICKED) {
            ::PostMessageW(dialog, WM_CLOSE, 0, 0);
            return TRUE;
        }
        return FALSE;
    }

    case kMsgStatus: {
        if (state == nullptr) {
            return TRUE;
        }
        ::EnterCriticalSection(&state->lock);
        const std::wstring text = state->status;
        ::LeaveCriticalSection(&state->lock);
        SetControlText(dialog, kIdStatus, text);
        return TRUE;
    }

    case WM_CLOSE:
        ::EndDialog(dialog, 0);
        return TRUE;

    default:
        break;
    }
    return FALSE;
}

bool RegisterSliderClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &SliderProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kSliderClass;

    if (::RegisterClassExW(&windowClass) != 0) {
        return true;
    }
    return ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool RegisterKeyBoxClass(HINSTANCE instance) {
    WNDCLASSEXW windowClass{};
    windowClass.cbSize = sizeof(windowClass);
    windowClass.style = CS_HREDRAW | CS_VREDRAW;
    windowClass.lpfnWndProc = &KeyBoxProc;
    windowClass.hInstance = instance;
    windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
    windowClass.lpszClassName = kKeyBoxClass;

    if (::RegisterClassExW(&windowClass) != 0) {
        return true;
    }
    return ::GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

} // namespace

int gui::Run(HINSTANCE instance) {
    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
    ::InitCommonControlsEx(&controls);

    if (!RegisterSliderClass(instance) || !RegisterKeyBoxClass(instance)) {
        ::MessageBoxW(nullptr, L"The window could not be created.", L"MW2 Unlocker",
                      MB_ICONERROR | MB_OK);
        return 1;
    }

    g_backgroundBrush = ::CreateSolidBrush(kBackground);
    g_cardBrush = ::CreateSolidBrush(kCard);
    g_grooveBrush = ::CreateSolidBrush(kGroove);

    auto* state = new State();
    ::InitializeCriticalSection(&state->lock);
    state->stopEvent = ::CreateEventW(nullptr, TRUE, FALSE, nullptr);
    state->iniPath = app::WorkIniPath();
    state->nextToExeIniPath = app::NextToExeIniPath();
    state->dllPath = app::WorkDllPath();
    LoadValues(*state);

    ::DialogBoxParamW(instance, MAKEINTRESOURCEW(kDialogMain), nullptr, &DialogProc,
                      reinterpret_cast<LPARAM>(state));

    // The window has gone. Stop the worker before freeing anything it can still
    // reach.
    ::SetEvent(state->stopEvent);

    bool workerStopped = true;
    if (state->worker != nullptr) {
        workerStopped = ::WaitForSingleObject(state->worker, 2000) == WAIT_OBJECT_0;
        ::CloseHandle(state->worker);
    }

    if (workerStopped) {
        ::CloseHandle(state->stopEvent);
        ::DeleteCriticalSection(&state->lock);
        delete state;
    } else {
        // Still inside the injection call, which can block for several seconds.
        // Leaking this one small block is the safe option: freeing it here would
        // be a use-after-free.
        ::OutputDebugStringW(L"MW2Unlocker: worker still busy at exit; state intentionally leaked\n");
    }

    if (g_headerFont != nullptr) {
        ::DeleteObject(g_headerFont);
    }
    ::DeleteObject(g_grooveBrush);
    ::DeleteObject(g_cardBrush);
    ::DeleteObject(g_backgroundBrush);
    return 0;
}
