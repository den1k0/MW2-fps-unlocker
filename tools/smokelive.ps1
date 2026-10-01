# Regression test for the live channel.
#
# The launcher and the DLL talk over WM_COPYDATA. An earlier version sent a
# custom registered message instead, which the kernel does NOT marshal: the game
# followed a pointer belonging to the launcher's address space and died with
# STATUS_FATAL_USER_CALLBACK_EXCEPTION on the first Apply. That cost a
# multiplayer session to find, so it gets a test.
#
# The game is not needed. Any 64-bit process will do as a host: the DLL loads,
# creates its control window, fails to find any of the game's cvars (which is
# expected and is what the log will say), and must then survive being sent a
# live update.
#
# Usage: powershell -NoProfile -File tools/smokelive.ps1

param(
    [string]$Dll = 'build\Release\mw2_unlocker.dll',
    [string]$Injector = 'build\Release\injector.exe',
    [string]$Log = 'build\Release\mw2_unlocker.log'
)

# The struct below MIRRORS src/ipc.h. Change one and you must change the other:
# the DLL checks cbData against its own sizeof(Values) and the version against
# its own protocol version, and refuses anything that does not match - by design,
# because a struct that has drifted is a struct that will corrupt memory.
$ProtocolVersion = 9

$ErrorActionPreference = 'Stop'

Add-Type -Namespace Live -Name Client -MemberDefinition @'
[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct Values {
    public uint version;
    public int fpsEnabled;
    public int fpsValue;
    public int fovEnabled;
    public float fovValue;
    public int fovClampEnabled;
    public float fovClampValue;
    public int counterEnabled;
    public int counterMode;
    public int netFpsEnabled;
    public int toggleKey;
    public int sensitivityEnabled;
    public float sensitivityValue;
    public int musicEnabled;
    public int fullbrightEnabled;
    public int hudEnabled;
    public int moveGunEnabled; // the viewmodel offsets, not the "hide the gun" switch
    public int fogEnabled;
    public int filmTweakEnabled;
    public int gunEnabled;
    public float gunX;
    public float gunY;
    public float gunZ;
    public float filmContrast;
    public float filmBrightness;
    public float filmDesaturation;
    public float filmLightTint;
    public float filmMediumTint;
    public float filmDarkTint;
}

[System.Runtime.InteropServices.StructLayout(System.Runtime.InteropServices.LayoutKind.Sequential)]
public struct CopyData {
    public System.IntPtr dwData;
    public int cbData;
    public System.IntPtr lpData;
}

// CharSet.Unicode matters here, and it is not optional: naming the W entry point
// without it marshals the class name as ANSI bytes, so FindWindowW reads garbage
// and reports that the window does not exist. That cost one debugging round.
[System.Runtime.InteropServices.DllImport("user32.dll", EntryPoint = "FindWindowW",
    CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern System.IntPtr FindWindow(string className, string windowName);

public delegate bool EnumProc(System.IntPtr window, System.IntPtr parameter);

[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool EnumWindows(EnumProc callback, System.IntPtr parameter);

[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern uint GetWindowThreadProcessId(System.IntPtr window, out uint pid);

[System.Runtime.InteropServices.DllImport("user32.dll", EntryPoint = "GetClassNameW",
    CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern int GetClassName(System.IntPtr window, System.Text.StringBuilder text, int count);

[System.Runtime.InteropServices.DllImport("user32.dll", EntryPoint = "GetWindowTextW",
    CharSet = System.Runtime.InteropServices.CharSet.Unicode)]
public static extern int GetWindowText(System.IntPtr window, System.Text.StringBuilder text, int count);

[System.Runtime.InteropServices.DllImport("user32.dll")]
public static extern bool IsWindowVisible(System.IntPtr window);

// Every top-level window owned by one process, as "class|title|visible" lines.
// Used to find the control window without relying on FindWindow, so a failure of
// one method can be told apart from a failure of the other.
public static string[] WindowsOfProcess(uint pid) {
    System.Collections.Generic.List<string> found = new System.Collections.Generic.List<string>();
    EnumWindows(delegate(System.IntPtr window, System.IntPtr parameter) {
        uint owner;
        GetWindowThreadProcessId(window, out owner);
        if (owner == pid) {
            System.Text.StringBuilder cls = new System.Text.StringBuilder(256);
            GetClassName(window, cls, cls.Capacity);
            System.Text.StringBuilder title = new System.Text.StringBuilder(256);
            GetWindowText(window, title, title.Capacity);
            found.Add(cls.ToString() + "|" + title.ToString() + "|" +
                      (IsWindowVisible(window) ? "visible" : "hidden"));
        }
        return true;
    }, System.IntPtr.Zero);
    return found.ToArray();
}

// The same search done by class name, for comparison with FindWindow.
public static System.IntPtr FindByClassInProcess(uint pid, string className) {
    System.IntPtr result = System.IntPtr.Zero;
    EnumWindows(delegate(System.IntPtr window, System.IntPtr parameter) {
        uint owner;
        GetWindowThreadProcessId(window, out owner);
        if (owner != pid) {
            return true;
        }
        System.Text.StringBuilder cls = new System.Text.StringBuilder(256);
        GetClassName(window, cls, cls.Capacity);
        if (cls.ToString() == className) {
            result = window;
            return false;
        }
        return true;
    }, System.IntPtr.Zero);
    return result;
}

[System.Runtime.InteropServices.DllImport("user32.dll", EntryPoint = "SendMessageTimeoutW",
    SetLastError = true)]
public static extern System.IntPtr SendMessageTimeout(System.IntPtr window, uint message,
    System.IntPtr wparam, System.IntPtr lparam, uint flags, uint timeout, out System.IntPtr result);

public static int PayloadSize() {
    return System.Runtime.InteropServices.Marshal.SizeOf(typeof(Values));
}

// Returns the value the receiving window procedure returned, or -1 if the
// message could not be delivered at all.
public static long Push(uint magic, uint version, int fps, float fov) {
    Values v = new Values();
    v.version = version;
    v.fpsEnabled = 1;
    v.fpsValue = fps;
    v.fovEnabled = 1;
    v.fovValue = fov;
    v.fovClampEnabled = 0;
    v.fovClampValue = 179.0f;
    v.counterEnabled = 0;
    v.counterMode = 1;
    v.netFpsEnabled = 0;
    // Straight from ipc::MakeDefault(): anything but 0x70..0x7B is ignored by
    // the receiver, so this also proves the field was unmarshalled correctly.
    v.toggleKey = 0x76; // F7, not the default F6, so the log shows the change
    v.sensitivityEnabled = 1;
    v.sensitivityValue = 3.45f;
    v.musicEnabled = 1;
    v.fullbrightEnabled = 1;
    v.hudEnabled = 1;
    v.moveGunEnabled = 1;
    v.fogEnabled = 1;
    v.filmTweakEnabled = 1;
    v.gunEnabled = 1;
    v.gunX = 1.25f;
    v.gunY = -0.5f;
    v.gunZ = 2.0f;
    v.filmContrast = 1.4f;
    v.filmBrightness = 0.1f;
    v.filmDesaturation = 0.2f;
    v.filmLightTint = 1.1f;
    v.filmMediumTint = 0.9f;
    v.filmDarkTint = 0.7f;

    int size = System.Runtime.InteropServices.Marshal.SizeOf(typeof(Values));
    System.IntPtr payload = System.Runtime.InteropServices.Marshal.AllocHGlobal(size);
    System.IntPtr block = System.Runtime.InteropServices.Marshal.AllocHGlobal(
        System.Runtime.InteropServices.Marshal.SizeOf(typeof(CopyData)));
    try {
        System.Runtime.InteropServices.Marshal.StructureToPtr(v, payload, false);

        CopyData data = new CopyData();
        data.dwData = (System.IntPtr)(long)magic;
        data.cbData = size;
        data.lpData = payload;
        System.Runtime.InteropServices.Marshal.StructureToPtr(data, block, false);

        System.IntPtr result;
        // The title is given explicitly rather than passed as null: PowerShell
        // turns $null into an empty string for a string parameter, and
        // FindWindow would then look for a window whose *title* is empty.
        System.IntPtr window = FindWindow("MW2UnlockerLive", "MW2Unlocker");
        if (window == System.IntPtr.Zero) {
            return -2;
        }

        // WM_COPYDATA = 0x004A, SMTO_ABORTIFHUNG = 0x0002
        System.IntPtr sent = SendMessageTimeout(window, 0x004A, System.IntPtr.Zero, block, 0x0002,
                                                2000, out result);
        return (sent == System.IntPtr.Zero) ? -1 : result.ToInt64();
    } finally {
        System.Runtime.InteropServices.Marshal.FreeHGlobal(payload);
        System.Runtime.InteropServices.Marshal.FreeHGlobal(block);
    }
}
'@

Write-Output ("payload size the receiver expects: {0} bytes" -f [Live.Client]::PayloadSize())

if (-not (Test-Path -LiteralPath $Dll)) { Write-Output "not found: $Dll"; exit 1 }
if (-not (Test-Path -LiteralPath $Injector)) { Write-Output "not found: $Injector"; exit 1 }

# A host that stays alive and does not care what is loaded into it.
$hostProcess = Start-Process -FilePath 'powershell.exe' `
    -ArgumentList '-NoProfile', '-Command', 'Start-Sleep -Seconds 300' `
    -PassThru -WindowStyle Hidden
Write-Output ("host: powershell.exe pid {0}" -f $hostProcess.Id)

Start-Sleep -Seconds 1
& $Injector $hostProcess.Id (Resolve-Path -LiteralPath $Dll).Path
if ($LASTEXITCODE -ne 0) {
    Write-Output "injection failed"
    $hostProcess.Kill()
    exit 2
}

# The DLL waits out delayMs before its first apply, and only then creates the
# control window.
$window = [IntPtr]::Zero
for ($attempt = 0; $attempt -lt 40; $attempt++) {
    Start-Sleep -Milliseconds 500
    $window = [Live.Client]::FindWindow('MW2UnlockerLive', 'MW2Unlocker')
    if ($window -ne [IntPtr]::Zero) { break }
}

if ($window -eq [IntPtr]::Zero) {
    # Say exactly what went wrong instead of just "not found": the class may
    # differ, the window may not be top-level, or FindWindow itself may be the
    # problem while enumeration would have worked.
    Write-Output "FindWindow did not find it. Windows owned by the host:"
    foreach ($entry in [Live.Client]::WindowsOfProcess([uint32]$hostProcess.Id)) {
        Write-Output ("   {0}" -f $entry)
    }

    $byEnumeration = [Live.Client]::FindByClassInProcess([uint32]$hostProcess.Id, 'MW2UnlockerLive')
    if ($byEnumeration -ne [IntPtr]::Zero) {
        Write-Output "enumeration DID find it - FindWindow is the problem, not the window"
    }

    if ($hostProcess.HasExited) { Write-Output ("host exited with {0}" -f $hostProcess.ExitCode) }
    else { $hostProcess.Kill() }
    exit 3
}

Write-Output "control window found - pushing values"

# 0x4D573255 is ipc::kCopyDataMagic ('MW2U').
$result = [Live.Client]::Push(0x4D573255, [uint32]$ProtocolVersion, 333, 90.0)
Write-Output ("SendMessageTimeout returned {0} (1 = the receiver applied something)" -f $result)
if ($result -eq -1) { Write-Output "the message could not be delivered" }
if ($result -eq -2) { Write-Output "the control window could not be found" }

Start-Sleep -Seconds 1

# The point of the whole exercise: the host must still be alive.
if ($hostProcess.HasExited) {
    Write-Output ("FAIL: the host process died (exit code {0}) - the payload was not marshalled" -f `
        $hostProcess.ExitCode)
    Write-Output "--- DLL log tail ---"
    if (Test-Path -LiteralPath $Log) { Get-Content $Log | Select-Object -Last 10 }
    exit 4
}

Write-Output "PASS: the host process survived the update"

Write-Output "--- DLL log tail ---"
if (Test-Path -LiteralPath $Log) { Get-Content $Log | Select-Object -Last 8 }

$hostProcess.Kill()
Write-Output "host stopped"
