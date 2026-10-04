# Smoke test for the launcher window.
#
# Starts the EXE, proves the dialog resource really produced a window, reads the
# class, id, text, top edge and visibility of every control back out of it, and
# optionally clicks controls to exercise the rest of the window: "Apply && save"
# covers the whole write path (controls -> values -> both copies of
# unlocker.ini), and -Click covers the viewmodel and film tweak switches, which
# hide their rows and resize the window. Nobody has to look at the screen.
#
# Usage:
#   powershell -NoProfile -File smokegui.ps1 -Exe build\Release\MW2Unlocker.exe
#   powershell -NoProfile -File smokegui.ps1 -Exe ... -WaitSeconds 66 -ClickApply
#   powershell -NoProfile -File smokegui.ps1 -Exe ... -Click 1031,1023
#   powershell -NoProfile -File smokegui.ps1 -Exe ... -CheckDot
#   powershell -NoProfile -File smokegui.ps1 -Exe ... -CheckNote
#   powershell -NoProfile -File smokegui.ps1 -Exe ... -CheckNoteColour
#   powershell -NoProfile -File smokegui.ps1 -Exe ... -CheckUpdate
#   powershell -NoProfile -File smokegui.ps1 -Exe ... -SelectProfile 2 -ClickApply
#   powershell -NoProfile -File smokegui.ps1 -Exe ... -SetEdit "1097=0.5" -ClickApply
#
# The top edges are the interesting part of the listing: the collapsible cards
# pull everything below them up, so a diff of two runs says whether the layout
# actually moved.

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    # How long to leave the window open before reading its controls back. The
    # worker waits a minute for the game before giving up, so a value above that
    # also exercises the worker -> window status path.
    [int]$WaitSeconds = 3,
    # Press the Apply button from outside the process and report what happened.
    [switch]$ClickApply,
    # Click these controls (by id, comma separated) the way a user would, in
    # order, reporting the window's size after each one. A string rather than an
    # int[] so it can be passed from a cmd prompt as well as from PowerShell:
    # cmd hands over "1031,1023" and PowerShell's number conversion would read
    # that as one gigantic id.
    [string]$Click = '',
    # Choose this profile slot (1..3) in the box on the top strip, the way a user
    # picking it from the dropdown would: the selection is set and then the
    # notification the window acts on is sent, because CB_SETCURSEL on its own is
    # silent.
    [int]$SelectProfile = 0,
    # Read the status indicator back out of the window by sampling its pixels.
    # It is not a control - the window paints it - so this is the only way to
    # prove it is drawn at all.
    [switch]$CheckDot,
    # Print the sliders' positions, before and after any -Click. The sliders keep
    # their value in the control itself, so this is the only way to see whether a
    # switch threw one of them somewhere - which is invisible in a listing of
    # control rectangles.
    [switch]$DumpSliders,
    # Check that the note above the first card really did get the smaller font.
    # The control listing cannot show a font, so the two handles are compared.
    [switch]$CheckNote,
    # Sample the note's box for the pixels it paints itself. It draws its own text
    # - one word of it in the rejoin colour - and a text colour cannot be read back
    # off a window, so the pixels are the only evidence.
    [switch]$CheckNoteColour,
    # Post the registered message the DLL posts when its next-profile key is
    # pressed, so the launcher's half of that key can be driven from outside. The
    # DLL's half - polling the key inside the game - cannot be reached from here.
    [switch]$SendNextProfile,
    # Type into number boxes, as "id=text,id=text", then tell the window the box
    # lost the focus - which is what a user's tab or click away sends, and the only
    # moment the window reads an edit back. Pair it with -ClickApply to see the
    # typed numbers come out in the config.
    [string]$SetEdit = '',
    # Click "Check for update" and read its label back. The check runs on its
    # own thread and only replaces the label when GitHub has answered, so this
    # waits for it to stop saying "Checking" - the label is the whole result.
    [switch]$CheckUpdate
)

$ErrorActionPreference = 'Stop'

Add-Type -Namespace W -Name U -MemberDefinition @'
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool EnumWindows(EnumProc callback, System.IntPtr parameter);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool EnumChildWindows(System.IntPtr parent, EnumProc callback, System.IntPtr parameter);
public delegate bool EnumProc(System.IntPtr window, System.IntPtr parameter);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern uint GetWindowThreadProcessId(System.IntPtr window, out uint pid);
[System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern int GetWindowText(System.IntPtr window, System.Text.StringBuilder text, int count);
[System.Runtime.InteropServices.DllImport("user32.dll", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern int GetClassName(System.IntPtr window, System.Text.StringBuilder text, int count);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern int GetDlgCtrlID(System.IntPtr window);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool IsWindowVisible(System.IntPtr window);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool PostMessage(System.IntPtr window, uint message, System.IntPtr wparam, System.IntPtr lparam);
[System.Runtime.InteropServices.DllImport("user32.dll", EntryPoint = "RegisterWindowMessageW", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern uint RegisterWindowMessage(string name);
[System.Runtime.InteropServices.DllImport("user32.dll", EntryPoint = "SendMessageW")]
public static extern System.IntPtr SendMessage(System.IntPtr window, uint message, System.IntPtr wparam, System.IntPtr lparam);
[System.Runtime.InteropServices.DllImport("user32.dll", EntryPoint = "SendMessageW", CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern System.IntPtr SendMessageText(System.IntPtr window, uint message, System.IntPtr wparam, string lparam);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetWindowRect(System.IntPtr window, out RECT rect);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetClientRect(System.IntPtr window, out RECT rect);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool ScreenToClient(System.IntPtr window, ref POINT point);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct POINT { public int x; public int y; }
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern System.IntPtr GetDC(System.IntPtr window);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern int ReleaseDC(System.IntPtr window, System.IntPtr dc);
[System.Runtime.InteropServices.DllImport("gdi32.dll")]
public static extern uint GetPixel(System.IntPtr dc, int x, int y);
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct RECT { public int left; public int top; public int right; public int bottom; }
'@

# Control ids, matching launcher/gui.cpp and launcher/generate_rc.cmake.
$KIdFpsSlider = 1002
$KIdFovClamp = 1007
$KIdStatus = 1011
$KIdApply = 1012
$KIdSensEnable = 1018
$KIdSensSlider = 1019
$KIdToggleKey = 1016
# The note above the first card, which is given a smaller font than the rest.
$KIdNote = 1083
# The profile box on the top strip.
$KIdProfileCombo = 1092
# "Check for update", on its own row under the build number.
$KIdCheckUpdates = 1094

# WM_COMMAND, and the messages that drive the profile box: read it back, set the
# selection, then tell the window it changed.
$WmCommand = 0x0111
$CbGetCount = 0x0146
$CbGetCurSel = 0x0147
$CbGetLbText = 0x0149
$CbSetCurSel = 0x014E
$CbnSelChange = 1

# WM_GETFONT, to read a control's font back out of it, and WM_GETTEXT, to read a
# control's text the one way that crosses a process boundary.
$WmGetFont = 0x0031
$WmGetText = 0x000D

# Control messages used to drive the window from outside the process.
$BmClick = 0x00F5
$WmClose = 0x0010
$WmLButtonDown = 0x0201
$WmSetText = 0x000C
# EN_KILLFOCUS: the window folds a typed number into its state on this one.
$EnKillFocus = 0x0200

# The slider is a custom control of the launcher's own, so it has its own
# message for "set the position" instead of the trackbar's TBM_SETPOS
# (launcher/gui.cpp, kMsgSliderSetPosition = WM_APP + 11). Reading one back is
# the matching WM_APP + 12.
$SliderSetPosition = 0x8000 + 11
$SliderGetPosition = 0x8000 + 12

# The sliders worth watching: the frame cap, the three in the viewmodel card's
# right-hand column (the two safe-area ones and the compass under them), the two
# testing ones, and the sprint scale in Other Settings.
$SliderIds = @(1002, 1074, 1077, 1080, 1086, 1089, 1108)

function Get-Text([IntPtr]$window) {
    # WM_GETTEXT, not GetWindowText. GetWindowText reads the *window name* of a
    # window belonging to another process - the text a control was created with -
    # so every edit box came back empty however many numbers were in it, and only
    # statics and buttons, whose text is their window name, read back. WM_GETTEXT
    # is answered by the control itself, so an edit's own buffer is reachable.
    $buffer = [System.Runtime.InteropServices.Marshal]::AllocHGlobal(1024)
    try {
        $length = [int][W.U]::SendMessage($window, $WmGetText, [IntPtr]1024, $buffer)
        if ($length -gt 0) {
            return [System.Runtime.InteropServices.Marshal]::PtrToStringUni($buffer)
        }
    } finally {
        [System.Runtime.InteropServices.Marshal]::FreeHGlobal($buffer)
    }

    # Nothing came back - the window has no text of its own, which for an empty
    # edit is the answer rather than a failure.
    return ''
}

function Get-Class([IntPtr]$window) {
    $buffer = New-Object System.Text.StringBuilder 256
    [void][W.U]::GetClassName($window, $buffer, $buffer.Capacity)
    return $buffer.ToString()
}

function Get-Top([IntPtr]$window) {
    $rect = New-Object 'W.U+RECT'
    [void][W.U]::GetWindowRect($window, [ref]$rect)
    return $rect.top
}

# The left edge, converted to dialog units the way the template states them, so a
# listing can be compared with generate_rc.cmake directly. 300 units across.
function Get-LeftUnits([IntPtr]$window, [IntPtr]$dialog) {
    $rect = New-Object 'W.U+RECT'
    [void][W.U]::GetWindowRect($window, [ref]$rect)
    $client = New-Object 'W.U+RECT'
    [void][W.U]::GetClientRect($dialog, [ref]$client)
    if ($client.right -eq 0) { return 0 }
    $perUnit = $client.right / 300.0
    return [int][math]::Round(($rect.left - (Get-WindowLeft $dialog)) / $perUnit)
}

# The dialog's own screen position, needed to turn a child's screen rectangle back
# into a position relative to the client area.
function Get-WindowLeft([IntPtr]$window) {
    $rect = New-Object 'W.U+RECT'
    [void][W.U]::GetWindowRect($window, [ref]$rect)
    $client = New-Object 'W.U+RECT'
    [void][W.U]::GetClientRect($window, [ref]$client)
    return $rect.left + (($rect.right - $rect.left) - $client.right) / 2
}

function Get-Size([IntPtr]$window) {
    $rect = New-Object 'W.U+RECT'
    [void][W.U]::GetWindowRect($window, [ref]$rect)
    return ('{0}x{1}' -f ($rect.right - $rect.left), ($rect.bottom - $rect.top))
}

# What a slider is holding, read straight out of the control. A slider that was
# moved by a switch's layout change shows up here; the rectangles in the listing
# would not.
function Show-Sliders([IntPtr]$window, [string]$when) {
    Write-Output ("sliders ({0}):" -f $when)
    foreach ($id in $SliderIds) {
        $target = (Get-Children $window) | Where-Object { $_.Id -eq $id } | Select-Object -First 1
        if ($target -eq $null) {
            Write-Output ("  id {0,-5} (no such control)" -f $id)
            continue
        }
        $position = [int][W.U]::SendMessage($target.Handle, $SliderGetPosition, [IntPtr]::Zero,
                                            [IntPtr]::Zero)
        Write-Output ("  id {0,-5} position {1,-6} class '{2}'" -f $id, $position, $target.Class)
    }
}

# What the profile box is holding: the rows and which one is chosen. GetWindowText
# cannot be trusted here - a combo answers it through its edit part, which a
# dropdown list does not have - so the items are read with CB_GETLBTEXT.
function Show-ProfileBox([IntPtr]$window, [string]$when) {
    $combo = (Get-Children $window) | Where-Object { $_.Id -eq $KIdProfileCombo } |
        Select-Object -First 1
    if ($combo -eq $null) {
        Write-Output ("profile box ({0}): FAIL - no control with id {1}" -f $when, $KIdProfileCombo)
        return
    }

    $count = [int][W.U]::SendMessage($combo.Handle, $CbGetCount, [IntPtr]::Zero, [IntPtr]::Zero)
    $selected = [int][W.U]::SendMessage($combo.Handle, $CbGetCurSel, [IntPtr]::Zero, [IntPtr]::Zero)
    Write-Output ("profile box ({0}): {1} row(s), selection {2}" -f $when, $count, $selected)

    $buffer = [System.Runtime.InteropServices.Marshal]::AllocHGlobal(512)
    for ($i = 0; $i -lt $count; $i++) {
        $length = [int][W.U]::SendMessage($combo.Handle, $CbGetLbText, [IntPtr]$i, $buffer)
        $text = [System.Runtime.InteropServices.Marshal]::PtrToStringUni($buffer)
        Write-Output ("  row {0}: length {1}, text '{2}'" -f $i, $length, $text)
    }
    [System.Runtime.InteropServices.Marshal]::FreeHGlobal($buffer)
}

# Children of one window, as objects, so callers can search by id or class.
function Get-Children([IntPtr]$parent) {
    $list = New-Object System.Collections.Generic.List[IntPtr]
    $callback = [W.U+EnumProc]{
        param([IntPtr]$child, [IntPtr]$parameter)
        $list.Add($child)
        return $true
    }
    [void][W.U]::EnumChildWindows($parent, $callback, [IntPtr]::Zero)

    $result = @()
    foreach ($child in $list) {
        $result += [pscustomobject]@{
            Handle = $child
            Id     = [W.U]::GetDlgCtrlID($child)
            Class  = Get-Class $child
            Text   = Get-Text $child
        }
    }
    return $result
}

if (-not (Test-Path -LiteralPath $Exe)) {
    Write-Output "not found: $Exe"
    exit 1
}

$process = Start-Process -FilePath $Exe -PassThru
Write-Output ("started pid {0}" -f $process.Id)

Start-Sleep -Seconds $WaitSeconds

$topLevel = New-Object System.Collections.Generic.List[IntPtr]
$callback = [W.U+EnumProc]{
    param([IntPtr]$window, [IntPtr]$parameter)
    $owner = [uint32]0
    [void][W.U]::GetWindowThreadProcessId($window, [ref]$owner)
    if ($owner -eq $process.Id) {
        $topLevel.Add($window)
    }
    return $true
}
[void][W.U]::EnumWindows($callback, [IntPtr]::Zero)

$dialog = [IntPtr]::Zero
foreach ($window in $topLevel) {
    if ((Get-Class $window) -eq '#32770') {
        $dialog = $window
    }
}

if ($dialog -eq [IntPtr]::Zero) {
    Write-Output "FAIL: no dialog window was created (is the dialog resource missing?)"
    if ($process.HasExited) {
        Write-Output ("process exited with code {0}" -f $process.ExitCode)
    } else {
        $process.Kill()
    }
    exit 2
}

Write-Output ""
Write-Output ("dialog: class='{0}' text='{1}' visible={2}" -f `
    (Get-Class $dialog), (Get-Text $dialog), [W.U]::IsWindowVisible($dialog))

Write-Output ("size:  {0}" -f (Get-Size $dialog))

# Does the last row fit inside the client area? A dialog whose template is a few
# units short of its content cuts the bottom edge off the buttons, and that is
# invisible in a listing of top edges.
$clientRect = New-Object 'W.U+RECT'
[void][W.U]::GetClientRect($dialog, [ref]$clientRect)
$lowestBottom = 0
$lowestId = 0
foreach ($probe in (Get-Children $dialog)) {
    if (-not [W.U]::IsWindowVisible($probe.Handle)) { continue }
    $r = New-Object 'W.U+RECT'
    [void][W.U]::GetWindowRect($probe.Handle, [ref]$r)
    $p = New-Object 'W.U+POINT'
    $p.x = 0
    $p.y = $r.bottom
    [void][W.U]::ScreenToClient($dialog, [ref]$p)
    if ($p.y -gt $lowestBottom) {
        $lowestBottom = $p.y
        $lowestId = $probe.Id
    }
}
$slack = $clientRect.bottom - $lowestBottom
$mark = if ($slack -lt 0) { 'FAIL' } else { 'ok  ' }
Write-Output ("fit:   {0} lowest control is {1} (id {2}) against a client of {3} - {4} px spare" -f `
    $mark, $lowestBottom, $lowestId, $clientRect.bottom, $slack)

$children = Get-Children $dialog
foreach ($child in $children) {
    $shown = if ($child.Text.Length -gt 70) { $child.Text.Substring(0, 67) + '...' } else { $child.Text }
    $hidden = if ([W.U]::IsWindowVisible($child.Handle)) { '        ' } else { ' (hidden)' }
    Write-Output ("   id {0,-5} left {1,-5} top {2,-5} {3,-18} '{4}'{5}" -f `
        $child.Id, (Get-LeftUnits $child.Handle $dialog), (Get-Top $child.Handle), `
        $child.Class, $shown, $hidden)
}

function Test-Dot([IntPtr]$window, [string]$when) {
    # The indicator is painted, not a control, so the only evidence that it is
    # there is the pixel. Its centre is 8.5 dialog units left of and 4.5 below the
    # status control's top-left corner - the dot fills (12,478) to (23,489) and the
    # status line starts at (26,479), so its centre (17.5,483.5) is that far from
    # the corner. Keep those two numbers in step with kStatusDotUnits. The units
    # come from the status line's own size, fixed by the template at 262x18.
    $statusControl = (Get-Children $window) | Where-Object { $_.Id -eq $KIdStatus } |
        Select-Object -First 1
    $rect = New-Object 'W.U+RECT'
    [void][W.U]::GetWindowRect($statusControl.Handle, [ref]$rect)

    # The units come from the status line itself, whose size in dialog units the
    # template fixes at 262 by 18. Deriving them from the client size instead would
    # need the template's height, and that is not the height on screen while a card
    # is folded away - which made every offset here read as if the window were
    # permanently expanded.
    $unitX = 4.0 * ($rect.right - $rect.left) / 262.0
    $unitY = 8.0 * ($rect.bottom - $rect.top) / 18.0
    $cx = [int][math]::Round($rect.left - (8.5 * $unitX / 4.0))
    $cy = [int][math]::Round($rect.top + (4.5 * $unitY / 8.0))

    # The dot is the only saturated thing in that part of the window: the
    # background, the cards and the text are all but grey.
    $dc = [W.U]::GetDC([IntPtr]::Zero)
    $centre = [W.U]::GetPixel($dc, $cx, $cy)
    $coloured = 0
    for ($dy = -4; $dy -le 4; $dy++) {
        for ($dx = -5; $dx -le 5; $dx++) {
            $pixel = [W.U]::GetPixel($dc, $cx + $dx, $cy + $dy)
            if ($pixel -eq 0xFFFFFFFF) { continue }
            $r = [int]($pixel -band 0xFF)
            $g = [int](($pixel -shr 8) -band 0xFF)
            $b = [int](($pixel -shr 16) -band 0xFF)
            if (([math]::Abs($r - $g) + [math]::Abs($g - $b)) -gt 60) { $coloured++ }
        }
    }

    if ($coloured -eq 0) {
        # Widen the search before giving up: a dot left behind somewhere else is a
        # very different fault from one that was never painted, and the offset says
        # which. 60 pixels either way is a whole card's worth of shift.
        $stray = $null
        for ($dy = -60; $dy -le 60 -and $stray -eq $null; $dy++) {
            for ($dx = -60; $dx -le 60; $dx++) {
                $pixel = [W.U]::GetPixel($dc, $cx + $dx, $cy + $dy)
                if ($pixel -eq 0xFFFFFFFF) { continue }
                $r = [int]($pixel -band 0xFF)
                $g = [int](($pixel -shr 8) -band 0xFF)
                $b = [int](($pixel -shr 16) -band 0xFF)
                if (([math]::Abs($r - $g) + [math]::Abs($g - $b)) -gt 60) {
                    $stray = @($dx, $dy, $r, $g, $b)
                    break
                }
            }
        }
        if ($stray -eq $null) {
            Write-Output ("dot ({0}): FAIL - nothing coloured within 60px of screen ({1},{2})" -f `
                $when, $cx, $cy)
        } else {
            Write-Output ("dot ({0}): FAIL - expected at ({1},{2}), found rgb({3},{4},{5}) {6},{7} away" -f `
                $when, $cx, $cy, $stray[2], $stray[3], $stray[4], $stray[0], $stray[1])
        }
    } else {
        $cr = [int]($centre -band 0xFF)
        $cg = [int](($centre -shr 8) -band 0xFF)
        $cb = [int](($centre -shr 16) -band 0xFF)
        Write-Output ("dot ({0}): {1} coloured pixel(s) near screen ({2},{3}), centre rgb({4},{5},{6})" -f `
            $when, $coloured, $cx, $cy, $cr, $cg, $cb)
    }
    # The box the status line sits in. Sampled just inside its bottom-left corner,
    # then walked downwards until the colour changes - that is the box's bottom
    # edge. The dot is at (12,478)-(23,489) and the status control starts at
    # (26,479), so du 12 at y 499 is inside the box and clear of both. The box is
    # meant to end at du 501, which is about three pixels further down.
    $boxX = [int][math]::Round($rect.left - (14.0 * $unitX / 4.0))
    $boxY = [int][math]::Round($rect.top + (20.0 * $unitY / 8.0))
    $insidePixel = [W.U]::GetPixel($dc, $boxX, $boxY)
    $ir = [int]($insidePixel -band 0xFF)
    $ig = [int](($insidePixel -shr 8) -band 0xFF)
    $ib = [int](($insidePixel -shr 16) -band 0xFF)

    $span = 0
    while ($span -lt 100) {
        if ([W.U]::GetPixel($dc, $boxX, $boxY + $span) -ne $insidePixel) { break }
        $span++
    }
    $endPixel = [W.U]::GetPixel($dc, $boxX, $boxY + $span)
    $or = [int]($endPixel -band 0xFF)
    $og = [int](($endPixel -shr 8) -band 0xFF)
    $ob = [int](($endPixel -shr 16) -band 0xFF)

    if ($ir -eq $or -and $ig -eq $og -and $ib -eq $ob) {
        Write-Output ("box ({0}): FAIL - the same colour inside and out, rgb({1},{2},{3}) over {4}px" -f `
            $when, $ir, $ig, $ib, $span)
    } else {
        Write-Output ("box ({0}): inside rgb({1},{2},{3}) for {4}px, outside rgb({5},{6},{7})" -f `
            $when, $ir, $ig, $ib, $span, $or, $og, $ob)
    }

    [void][W.U]::ReleaseDC([IntPtr]::Zero, $dc)
}

if ($CheckNote) {
    # The note is given a font three quarters the size of the window's own. That
    # cannot be seen in the control listing, and a font that failed to be created
    # would simply leave the note at the standard size - so the two font handles
    # are read back and compared. They must differ, and neither may be null.
    $noteControl = $children | Where-Object { $_.Id -eq $KIdNote } | Select-Object -First 1
    $baseFont = [W.U]::SendMessage($dialog, $WmGetFont, [IntPtr]::Zero, [IntPtr]::Zero)
    if ($noteControl -eq $null) {
        Write-Output ("note:  FAIL - no control with id {0}" -f $KIdNote)
    } else {
        $noteFont = [W.U]::SendMessage($noteControl.Handle, $WmGetFont, [IntPtr]::Zero, [IntPtr]::Zero)
        if ($noteFont -eq [IntPtr]::Zero -or $noteFont -eq $baseFont) {
            Write-Output ("note:  FAIL - '{0}' kept the window's font ({1})" -f `
                $noteControl.Text, $baseFont)
        } else {
            Write-Output ("note:  ok - '{0}' has its own font {1}, against the window's {2}" -f `
                $noteControl.Text, $noteFont, $baseFont)
        }
    }
}

if ($CheckNoteColour) {
    # The note paints its own text rather than letting the static control paint it,
    # because the word naming the rejoin colour is drawn in that colour. Nothing can
    # read a text colour back off a window, so this is a pixel sample of the note's
    # box: the sentence is in the dim body colour and that one word in a plain
    # yellow, so both a lit pixel and a yellow one have to be in there. The test for
    # yellow compares it against the other two channels rather than looking for the
    # exact value: at three quarters of the dialog font the glyphs are a pixel or
    # two wide, and ClearType blends every one of them towards what is behind.
    $note = $children | Where-Object { $_.Id -eq $KIdNote } | Select-Object -First 1
    if ($note -eq $null) {
        Write-Output ("note colour: FAIL - no control with id {0}" -f $KIdNote)
    } else {
        $noteRect = New-Object 'W.U+RECT'
        [void][W.U]::GetWindowRect($note.Handle, [ref]$noteRect)
        $dc = [W.U]::GetDC([IntPtr]::Zero)
        $lit = 0
        $yellow = 0
        for ($y = $noteRect.top; $y -lt $noteRect.bottom; $y++) {
            for ($x = $noteRect.left; $x -lt $noteRect.right; $x++) {
                $pixel = [W.U]::GetPixel($dc, $x, $y)
                if ($pixel -eq 0xFFFFFFFF) { continue }
                $r = [int]($pixel -band 0xFF)
                $g = [int](($pixel -shr 8) -band 0xFF)
                $b = [int](($pixel -shr 16) -band 0xFF)
                # Anything that is not the window background is part of the note.
                if (([math]::Abs($r - 0x16) + [math]::Abs($g - 0x18) +
                     [math]::Abs($b - 0x15)) -gt 60) {
                    $lit++
                }
                if (($r - $b) -gt 90 -and ($g - $b) -gt 60 -and $r -gt 180) {
                    $yellow++
                }
            }
        }
        [void][W.U]::ReleaseDC([IntPtr]::Zero, $dc)
        if ($yellow -eq 0) {
            Write-Output ("note colour: FAIL - no yellow pixel in the note's box ({0},{1})-({2},{3}), {4} lit" -f `
                $noteRect.left, $noteRect.top, $noteRect.right, $noteRect.bottom, $lit)
        } else {
            Write-Output ("note colour: ok - {0} lit pixel(s), {1} of them yellow, in the note's box" -f `
                $lit, $yellow)
        }
    }
}

if ($SelectProfile -gt 0) {
    $combo = $children | Where-Object { $_.Id -eq $KIdProfileCombo } | Select-Object -First 1
    if ($combo -eq $null) {
        Write-Output ("FAIL: no control with id {0} - the profile box is missing" -f $KIdProfileCombo)
    } else {
        [void][W.U]::SendMessage($combo.Handle, $CbSetCurSel,
                                 [IntPtr]($SelectProfile - 1), [IntPtr]::Zero)
        $command = [IntPtr]((($CbnSelChange -shl 16) -bor $KIdProfileCombo))
        [void][W.U]::SendMessage($dialog, $WmCommand, $command, $combo.Handle)
        Start-Sleep -Milliseconds 300
        Write-Output ("profile box: chose slot {0} (id {1})" -f $SelectProfile, $KIdProfileCombo)
    }
}

if ($SetEdit) {
    foreach ($pair in $SetEdit.Split(',', [System.StringSplitOptions]::RemoveEmptyEntries)) {
        $parts = $pair.Split('=')
        if ($parts.Count -ne 2) {
            Write-Output ("FAIL: -SetEdit wants id=text, got '{0}'" -f $pair)
            continue
        }
        $id = [int]$parts[0].Trim()
        $text = $parts[1].Trim()
        $control = (Get-Children $dialog) | Where-Object { $_.Id -eq $id } |
            Select-Object -First 1
        if ($control -eq $null) {
            Write-Output ("FAIL: no control with id {0}" -f $id)
            continue
        }
        # Set the text, then send the notification the window acts on. WM_SETTEXT
        # alone changes what is on screen and nothing else: every value in this
        # window is read back when its box loses the focus.
        [void][W.U]::SendMessageText($control.Handle, $WmSetText, [IntPtr]::Zero, $text)
        $command = [IntPtr]((($EnKillFocus -shl 16) -bor $id))
        [void][W.U]::SendMessage($dialog, $WmCommand, $command, $control.Handle)
        Start-Sleep -Milliseconds 150
        Write-Output ("edit {0}: typed '{1}', box reads '{2}'" -f `
            $id, $text, (Get-Text $control.Handle))
    }
}

if ($SendNextProfile) {
    Write-Output ""
    $message = [W.U]::RegisterWindowMessage('MW2UnlockerNextProfile')
    Write-Output ("the next-profile message is {0}; posting it..." -f $message)
    [void][W.U]::PostMessage($dialog, $message, [IntPtr]::Zero, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 800
    Show-ProfileBox $dialog 'after the next-profile message'
}

if ($CheckDot) { Test-Dot $dialog 'at launch' }

if ($DumpSliders) { Show-Sliders $dialog 'at launch' }
if ($DumpSliders) { Show-ProfileBox $dialog 'at launch' }

$clickIds = @()
foreach ($piece in $Click.Split(',', [System.StringSplitOptions]::RemoveEmptyEntries)) {
    $clickIds += [int]$piece.Trim()
}

if ($clickIds.Count -gt 0) {
    Write-Output ""
    foreach ($id in $clickIds) {
        $target = (Get-Children $dialog) | Where-Object { $_.Id -eq $id } | Select-Object -First 1
        if ($target -eq $null) {
            Write-Output ("FAIL: no control with id {0}" -f $id)
            continue
        }
        [void][W.U]::SendMessage($target.Handle, $BmClick, [IntPtr]::Zero, [IntPtr]::Zero)
        Start-Sleep -Milliseconds 300
        Write-Output ("clicked {0} '{1}'  ->  size {2}" -f $id, $target.Text, (Get-Size $dialog))
    }

    # Where the rows ended up, so the collapse can be checked rather than assumed.
    Write-Output ""
    foreach ($child in (Get-Children $dialog)) {
        $mark = if ([W.U]::IsWindowVisible($child.Handle)) { 'shown' } else { 'hidden' }
        Write-Output ("   id {0,-5} top {1,-5} {2}" -f $child.Id, (Get-Top $child.Handle), $mark)
    }

    # The cards have moved, so the dot has moved with them - or it has not, which
    # is exactly the failure this catches.
    if ($CheckDot) { Test-Dot $dialog 'after the clicks' }

    # And the sliders: a switch that resized the window must not have moved any of
    # them. The compass is the one to watch - it shares the safe area's switch.
    if ($DumpSliders) { Show-Sliders $dialog 'after the clicks' }
    if ($DumpSliders) { Show-ProfileBox $dialog 'after the clicks' }
}

if ($CheckUpdate) {
    $button = $children | Where-Object { $_.Id -eq $KIdCheckUpdates } | Select-Object -First 1
    if ($button -eq $null) {
        Write-Output ("FAIL: no control with id {0} - the update button is missing" -f $KIdCheckUpdates)
    } else {
        # The listing read the button's text once, when the window opened; the
        # check replaces it, so every reading here goes back to the control.
        $before = New-Object 'W.U+RECT'
        [void][W.U]::GetWindowRect($button.Handle, [ref]$before)
        Write-Output ("update button before: '{0}'  ({1}px wide)" -f `
            (Get-Text $button.Handle), ($before.right - $before.left))
        [void][W.U]::SendMessage($button.Handle, $BmClick, [IntPtr]::Zero, [IntPtr]::Zero)

        $label = ''
        for ($attempt = 0; $attempt -lt 24; $attempt++) {
            Start-Sleep -Milliseconds 500
            $label = Get-Text $button.Handle
            if ($label -notmatch 'Checking') { break }
        }
        # The box is sized to the label by the window, so its width is worth
        # reporting too: a label it did not fit would show up here.
        $rect = New-Object 'W.U+RECT'
        [void][W.U]::GetWindowRect($button.Handle, [ref]$rect)
        Write-Output ("update button after:  '{0}'  ({1}px wide by {2})" -f `
            $label, ($rect.right - $rect.left), $attempt)
    }
}

if ($ClickApply) {
    $apply = $children | Where-Object { $_.Id -eq $KIdApply } | Select-Object -First 1
    if ($apply -eq $null) {
        Write-Output "FAIL: no Apply button (id $KIdApply) in the dialog"
    } else {
        # Change two things first, so the write path is genuinely exercised: a
        # slider value, and a checkbox whose key is commented out in the config.
        $slider = $children | Where-Object { $_.Id -eq $KIdFpsSlider } | Select-Object -First 1
        if ($slider -ne $null) {
            [void][W.U]::SendMessage($slider.Handle, $SliderSetPosition, [IntPtr]333, [IntPtr]::Zero)
        }
        $clamp = $children | Where-Object { $_.Id -eq $KIdFovClamp } | Select-Object -First 1
        if ($clamp -ne $null) {
            # BM_CLICK rather than BM_SETCHECK: the check state belongs to the
                # window, because an owner-drawn button keeps none of its own. So it
                # has to be toggled the way a user would.
                [void][W.U]::SendMessage($clamp.Handle, $BmClick, [IntPtr]::Zero, [IntPtr]::Zero)
            }
    # The sensitivity box is deliberately NOT clicked. Switching it on makes
    # the window write "seta sensitivity ..." into the player's own
    # players\config_mp.cfg, which is a real file belonging to whoever is
    # running the test - and the game is usually running while it does. That
    # write is the feature; it is just not something a smoke test should do.

    
            # Step the hotkey box as well, so the [general] section is covered. It is
            # a custom control, so it gets the click as a real button message.
            $keybox = $children | Where-Object { $_.Id -eq $KIdToggleKey } | Select-Object -First 1
            if ($keybox -ne $null) {
                [void][W.U]::SendMessage($keybox.Handle, $WmLButtonDown, [IntPtr]::Zero, [IntPtr]::Zero)
            }
        Start-Sleep -Milliseconds 300

        Write-Output ""
        Write-Output "clicking Apply..."
        [void][W.U]::SendMessage($apply.Handle, $BmClick, [IntPtr]::Zero, [IntPtr]::Zero)
        Start-Sleep -Seconds 2

        # Re-read the status line after the click.
        $status = (Get-Children $dialog) | Where-Object { $_.Id -eq $KIdStatus } | Select-Object -First 1
        if ($status -ne $null) {
            Write-Output ("status now: '{0}'" -f $status.Text)
        }
    }
}

[void][W.U]::PostMessage($dialog, $WmClose, [IntPtr]::Zero, [IntPtr]::Zero)
Start-Sleep -Seconds 2

if (-not $process.HasExited) {
    Write-Output ""
    Write-Output "the window ignored WM_CLOSE; killing the process"
    $process.Kill()
} else {
    Write-Output ""
    Write-Output ("window closed cleanly, exit code {0}" -f $process.ExitCode)
}
