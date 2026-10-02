# Emits the resource script that goes into the launcher EXE: the unlocker DLL and
# the default config as embedded payloads, plus the control window's layout.
#
# Invoked from CMakeLists.txt via `cmake -P` with:
#   -DOUT=<generated .rc path>  -DDLL=<mw2_unlocker.dll path>  -DINI=<unlocker.ini path>
#
# The numeric ids below MUST stay in sync:
#   101 = the unlocker DLL        (launcher/main.cpp, kResourceDll)
#   102 = the default config      (launcher/main.cpp, kResourceIni)
#   103 = the control dialog      (launcher/gui.cpp,   kDialogMain)
#  1001..1094 = its controls      (launcher/gui.cpp,   kId*)
#
# The layout is here rather than built with CreateWindowEx calls because a dialog
# template gives the right font, tab order and focus handling for free. The
# colours are not: every control that would draw itself in system colours is
# owner-drawn in gui.cpp, and the two sliders plus the hotkey box are custom
# controls of the launcher's own, because the stock trackbar cannot be recoloured
# at all. The window itself paints the five cards it is divided into: the frame
# cap, the field of view, the viewmodel offsets, the film tweak, and one
# catch-all card for the switches, the hotkey and the close-with-game box.
#
# The rows are spaced as tightly as the font allows. The frame cap and the field
# of view each hold four rows - a title with its number box, a line of
# explanation, the slider, the switch - and each card's height is that of its own
# last row plus the same margin the other cards use. Nothing is left over.
#
# Above the first card is the one line of advice: some of these settings are only
# read by the game when a match is set up, so they do nothing until the next one
# starts. That is why every row below sits one line lower than it otherwise
# would. It is control 1083 and the launcher gives it a smaller font than the rest
# of the window, so it reads as a note rather than as one more setting - and being
# set smaller, it needs less room above it than a full-size line would.
#
# The viewmodel card's sliders are 74 units wide rather than the 104 the card has
# to spare, so that they stop short of the number boxes at 114. A slider that runs
# under its own box cannot be dragged to its upper end.
#
# There is no mouse sensitivity card, and no control for the field of view's
# clamp either. Writing the sensitivity cvar was measured not to change the aim in
# game, and the clamp turned out to need its own care, so both are file-only:
# their sections are still in the config and still applied, they simply have no
# sliders. The status line and the build number (1082) are file-adjacent notes of
# the same kind - text on the window background rather than inside a card, which
# is why the launcher gives those three controls the background brush.
#
# The switches in "Other Settings" are not sliders: each is a cvar set to a fixed
# value (music off, fullbright on, HUD off, weapon model off, fog off), so there
# is nothing in them to drag. They are laid out in two columns because a single
# column of them would make the window needlessly tall. The cards that do carry
# sliders - the viewmodel and the film tweak - use one row per value, with its
# number beside it so an exact figure can be typed.
#
# Both of those cards hold two settings side by side, each column with its own
# switch, because a single column of every value would make the window taller
# than a 1080p screen:
#
#   * the viewmodel card has the three offsets on the left and the HUD safe area
#     on the right: the safe area's "adjusted" pair, which is the pair that moves
#     the HUD, with compassSize under them. Three rows against the viewmodel's
#     three, so neither column is the taller.
#   * the film tweak card has the grade on the left and the glow on the right,
#     with blur at the foot of the glow column.
#
# Black level has no control at all: its value feeds a path that runs when the
# renderer is set up rather than every frame, so a slider for it promised
# something it could not deliver. Its section is still in the config.
#
# The dialog is 527 units tall while its last row ends at 521. Those 6 spare units
# are the margin under the buttons: with the 8 the template first carried, the
# button row's bottom edge landed a few pixels outside the client area, because the
# frame is a little taller than the difference between the window and the client
# rectangles at the moment ApplyLayout measures it. Six units is about twelve
# pixels on screen, which is what the harness's fit check is looking for.
#
# The top strip carries the note at its left and the profile box at its right: a
# dropdown of the three profile slots. Choosing one loads it, and Apply & save is
# what stores the window's values in the slot it is on - so there is no second
# save or load button to get out of step with it. The box is the only stock
# control in the window; everything else is drawn by the launcher, which is why it
# is given the dark theme and its own row drawing rather than left to the system
# colours.
#
# CBS_HASSTRINGS is not decoration on that combo. An owner-drawn fixed combo
# without it stores the lParam of each CB_ADDSTRING instead of a copy of the text,
# so the rows have to be drawn from itemData - and drawing them from CB_GETLBTEXT,
# which is the obvious way, reads whatever the pointer happened to point at. Half
# of the names came out as characters from a code page nobody asked for.
#
# The frame cap and the field of view sit side by side at the top of the window -
# two half-width cards rather than two stacked ones - which is one whole card's
# height off the dialog. Each keeps its own title, number box, label, slider and
# switch; the slider is shortened to the half width, and the titles are sized so
# they do not run into the number boxes beside them.
#
# "Server" is the third card that folds: its switch is directly under the title and
# the two testing rows hang below it, so the card is one switch high until it is
# turned on. Unlike the other two, that switch is also a real gate - the timescale
# and the gravity are written only while it is on - because there is nothing else
# about either of them to decide whether to write.
#
# "Other Settings" carries the crosshair switch at the foot of its right-hand
# column, which is what keeps the two columns even - three switches each - and lets
# the card end one row higher than it would with a fourth on the left. "Close with
# the game" sits at the right-hand end of the hotkey's own row rather than at the
# foot of that column.
#
# The two testing settings have a card of their own, "Server", below it - a label,
# a slider and a number box each. Timescale's slider counts hundredths, gravity's
# counts whole units - an engine unit of gravity is a large number, so hundredths
# of one would be a range nothing could use.
#
# On the button row at the foot, under the build number, is "Check for update":
# its own row rather than beside the buttons, because what it does is not part of
# the window's job of writing settings. Unlike every other control here, its
# width in this template is only a starting point: the label is replaced as the
# check reports back, so the window sizes the box to whatever it says - see
# FitCheckButton in gui.cpp. 90 units is what "Check for update" needs. Only the
# width moves; the left edge stays on the column the build number uses, so the
# button grows and shrinks to the right.
#
# The status line at the bottom is inset from the left so the launcher can paint a
# small indicator dot beside it - green once the unlocker is injected, amber while
# it is still looking, red when something failed. Neither the dot nor the box
# around them is a control: the window draws both, in its WM_PAINT handler, with
# the box following the status line the way the dot does. The status control's own
# rectangle sits inside that box, and it takes the card brush so it blends into
# it rather than punching a hole.
#
# This template is the *fully expanded* layout: the viewmodel, film tweak and
# server cards are laid out with all of their rows showing. The launcher folds the rows
# of a card away when its switches are off and shrinks the window to match,
# working from this template's own measurements rather than from a second copy of
# the numbers - see the collapsible-cards section of gui.cpp. So the row labels
# carry ids (1046..1088) even though nothing reads their text: -1 would not be
# findable at run time, and the labels have to vanish along with the row they
# belong to.

if(NOT OUT OR NOT DLL OR NOT INI)
    message(FATAL_ERROR "generate_rc.cmake requires -DOUT, -DDLL and -DINI")
endif()

if(NOT EXISTS "${DLL}")
    message(FATAL_ERROR "generate_rc.cmake: DLL not found: ${DLL}")
endif()

if(NOT EXISTS "${INI}")
    message(FATAL_ERROR "generate_rc.cmake: config not found: ${INI}")
endif()

file(TO_NATIVE_PATH "${DLL}" dll_native)
file(TO_NATIVE_PATH "${INI}" ini_native)

file(WRITE "${OUT}"
"// Generated by launcher/generate_rc.cmake - do not edit.
// Payload embedded into MW2Unlocker.exe, plus the control window layout.

#include <windows.h>

101 RCDATA \"${dll_native}\"
102 RCDATA \"${ini_native}\"

103 DIALOGEX 0, 0, 300, 539
STYLE DS_SETFONT | DS_MODALFRAME | DS_FIXEDSYS | DS_CENTER | WS_POPUP | WS_CAPTION | WS_SYSMENU
CAPTION \"MW2 Unlocker\"
FONT 9, \"MS Shell Dlg\", 400, 0, 0x1
BEGIN
    LTEXT           \"Some settings take effect only after you rejoin a match.\", 1083, 20, 3, 175, 10
    COMBOBOX        1092, 200, 1, 90, 54, CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP

    LTEXT           \"Frame rate cap\", 1013, 20, 24, 66, 11
    EDITTEXT        1003, 92, 22, 46, 14, ES_AUTOHSCROLL | ES_NUMBER
    LTEXT           \"0 is uncapped.\", -1, 20, 36, 115, 9
    CONTROL         \"\", 1002, \"MW2Slider\", WS_TABSTOP, 20, 45, 115, 14
    CONTROL         \"Cap the frame rate\", 1001, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 20, 60, 120, 11

    LTEXT           \"Field of view\", 1014, 165, 24, 66, 11
    EDITTEXT        1006, 237, 22, 46, 14, ES_AUTOHSCROLL
    LTEXT           \"Degrees\", -1, 165, 36, 115, 9
    CONTROL         \"\", 1005, \"MW2Slider\", WS_TABSTOP, 165, 45, 115, 14
    CONTROL         \"Change the field of view\", 1004, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 165, 60, 125, 11

    LTEXT           \"Viewmodel and safe area\", 1024, 20, 87, 170, 11
    CONTROL         \"Move the viewmodel\", 1031, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 20, 102, 128, 11
    CONTROL         \"Adjust the safe area\", 1072, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 160, 102, 130, 11
    LTEXT           \"X\", 1046, 20, 119, 14, 10
    CONTROL         \"\", 1025, \"MW2Slider\", WS_TABSTOP, 36, 118, 74, 14
    EDITTEXT        1026, 114, 118, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Y\", 1047, 20, 135, 14, 10
    CONTROL         \"\", 1027, \"MW2Slider\", WS_TABSTOP, 36, 134, 74, 14
    EDITTEXT        1028, 114, 134, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Z\", 1048, 20, 151, 14, 10
    CONTROL         \"\", 1029, \"MW2Slider\", WS_TABSTOP, 36, 150, 74, 14
    EDITTEXT        1030, 114, 150, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Horiz. adj.\", 1073, 160, 119, 42, 10
    CONTROL         \"\", 1074, \"MW2Slider\", WS_TABSTOP, 204, 118, 48, 14
    EDITTEXT        1075, 254, 118, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Vert. adj.\", 1076, 160, 135, 42, 10
    CONTROL         \"\", 1077, \"MW2Slider\", WS_TABSTOP, 204, 134, 48, 14
    EDITTEXT        1078, 254, 134, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Compass\", 1079, 160, 151, 42, 10
    CONTROL         \"\", 1080, \"MW2Slider\", WS_TABSTOP, 204, 150, 48, 14
    EDITTEXT        1081, 254, 150, 34, 14, ES_AUTOHSCROLL

    LTEXT           \"Film tweak and glow\", 1017, 20, 181, 170, 11
    CONTROL         \"Enable the film tweak\", 1023, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 20, 195, 128, 11
    CONTROL         \"Tweak the glow\", 1056, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 160, 195, 130, 11
    LTEXT           \"Contrast\", 1049, 20, 211, 36, 10
    CONTROL         \"\", 1034, \"MW2Slider\", WS_TABSTOP, 58, 209, 52, 14
    EDITTEXT        1035, 114, 209, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Bright\", 1050, 20, 225, 36, 10
    CONTROL         \"\", 1036, \"MW2Slider\", WS_TABSTOP, 58, 223, 52, 14
    EDITTEXT        1037, 114, 223, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Desat\", 1051, 20, 239, 36, 10
    CONTROL         \"\", 1038, \"MW2Slider\", WS_TABSTOP, 58, 237, 52, 14
    EDITTEXT        1039, 114, 237, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Light\", 1052, 20, 253, 36, 10
    CONTROL         \"\", 1040, \"MW2Slider\", WS_TABSTOP, 58, 251, 52, 14
    EDITTEXT        1041, 114, 251, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Medium\", 1053, 20, 267, 36, 10
    CONTROL         \"\", 1042, \"MW2Slider\", WS_TABSTOP, 58, 265, 52, 14
    EDITTEXT        1043, 114, 265, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Dark\", 1054, 20, 281, 36, 10
    CONTROL         \"\", 1044, \"MW2Slider\", WS_TABSTOP, 58, 279, 52, 14
    EDITTEXT        1045, 114, 279, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Radius\", 1057, 160, 211, 42, 10
    CONTROL         \"\", 1058, \"MW2Slider\", WS_TABSTOP, 204, 209, 48, 14
    EDITTEXT        1059, 254, 209, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Intensity\", 1060, 160, 225, 42, 10
    CONTROL         \"\", 1061, \"MW2Slider\", WS_TABSTOP, 204, 223, 48, 14
    EDITTEXT        1062, 254, 223, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Cutoff\", 1063, 160, 239, 42, 10
    CONTROL         \"\", 1064, \"MW2Slider\", WS_TABSTOP, 204, 237, 48, 14
    EDITTEXT        1065, 254, 237, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Desat\", 1066, 160, 253, 42, 10
    CONTROL         \"\", 1067, \"MW2Slider\", WS_TABSTOP, 204, 251, 48, 14
    EDITTEXT        1068, 254, 251, 34, 14, ES_AUTOHSCROLL
    LTEXT           \"Blur\", 1069, 160, 267, 42, 10
    CONTROL         \"\", 1070, \"MW2Slider\", WS_TABSTOP, 204, 265, 48, 14
    EDITTEXT        1071, 254, 265, 34, 14, ES_AUTOHSCROLL

    LTEXT           \"Other Settings\", 1032, 20, 310, 140, 11
    LTEXT           \"Toggle key\", 1055, 20, 325, 58, 11
    CONTROL         \"\", 1016, \"MW2KeyBox\", WS_TABSTOP, 80, 322, 44, 15
    CONTROL         \"Close with the game\", 1033, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 156, 322, 134, 11
    CONTROL         \"Mute the music\", 1018, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 20, 350, 130, 11
    CONTROL         \"Fullbright world\", 1019, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 20, 364, 130, 11
    CONTROL         \"Hide the HUD\", 1020, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 20, 378, 130, 11
    CONTROL         \"Disable the fog\", 1022, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 156, 350, 124, 11
    CONTROL         \"Hide the weapon model\", 1021, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 156, 364, 124, 11
    CONTROL         \"Hide the crosshair\", 1084, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 156, 378, 130, 11

    LTEXT           \"Server\", 1091, 20, 406, 140, 11
    CONTROL         \"Tweak the server\", 1093, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 20, 420, 140, 11
    LTEXT           \"Timescale\", 1085, 20, 434, 60, 10
    CONTROL         \"\", 1086, \"MW2Slider\", WS_TABSTOP, 84, 433, 152, 14
    EDITTEXT        1087, 240, 433, 46, 14, ES_AUTOHSCROLL
    LTEXT           \"Gravity\", 1088, 20, 448, 60, 10
    CONTROL         \"\", 1089, \"MW2Slider\", WS_TABSTOP, 84, 447, 152, 14
    EDITTEXT        1090, 240, 447, 46, 14, ES_AUTOHSCROLL

    LTEXT           \"\", 1011, 26, 479, 262, 18

    LTEXT           \"\", 1082, 20, 507, 106, 13
    CONTROL         \"Check for update\", 1094, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 20, 522, 90, 11

    CONTROL         \"Apply & save\", 1012, \"Button\", BS_OWNERDRAW | BS_DEFPUSHBUTTON | WS_TABSTOP, 132, 505, 78, 16
    CONTROL         \"Close\", IDCANCEL, \"Button\", BS_OWNERDRAW | WS_TABSTOP, 216, 505, 74, 16
END
")
