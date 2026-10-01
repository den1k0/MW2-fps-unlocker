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
    [string]$Click = ''
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
[System.Runtime.InteropServices.DllImport("user32.dll", EntryPoint = "SendMessageW")]
public static extern System.IntPtr SendMessage(System.IntPtr window, uint message, System.IntPtr wparam, System.IntPtr lparam);
[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool GetWindowRect(System.IntPtr window, out RECT rect);
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

# Control messages used to drive the window from outside the process.
$BmClick = 0x00F5
$WmClose = 0x0010
$WmLButtonDown = 0x0201

# The slider is a custom control of the launcher's own, so it has its own
# message for "set the position" instead of the trackbar's TBM_SETPOS
# (launcher/gui.cpp, kMsgSliderSetPosition = WM_APP + 11).
$SliderSetPosition = 0x8000 + 11

function Get-Text([IntPtr]$window) {
    $buffer = New-Object System.Text.StringBuilder 512
    [void][W.U]::GetWindowText($window, $buffer, $buffer.Capacity)
    return $buffer.ToString()
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

function Get-Size([IntPtr]$window) {
    $rect = New-Object 'W.U+RECT'
    [void][W.U]::GetWindowRect($window, [ref]$rect)
    return ('{0}x{1}' -f ($rect.right - $rect.left), ($rect.bottom - $rect.top))
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

$children = Get-Children $dialog
foreach ($child in $children) {
    $shown = if ($child.Text.Length -gt 70) { $child.Text.Substring(0, 67) + '...' } else { $child.Text }
    $hidden = if ([W.U]::IsWindowVisible($child.Handle)) { '        ' } else { ' (hidden)' }
    Write-Output ("   id {0,-5} top {1,-5} {2,-18} '{3}'{4}" -f `
        $child.Id, (Get-Top $child.Handle), $child.Class, $shown, $hidden)
}

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
