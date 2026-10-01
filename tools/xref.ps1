# Find every code reference to an absolute address in a PE.
#
# On x64 a global is reached through a RIP-relative operand, and the target is
# always:
#
#     target = (address of the byte after the instruction) + disp32
#
# The instruction length therefore does not matter for the arithmetic - RIP
# already points past the whole instruction when it is used as the base. That
# means every reference can be found without decoding opcodes at all: treat
# every byte offset as a possible disp32, resolve it, and keep the offsets that
# land on the target. The only filter needed is that the byte in front of the
# displacement looks like a RIP-relative ModRM (mod = 00, rm = 101), which
# drops coincidences without dropping real hits.
#
# Usage: powershell -NoProfile -File xref.ps1 -Exe <path> -Target 0x140587508

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Target,
    [int]$Context = 48
)

$b = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Exe).Path)
$pe = [BitConverter]::ToInt32($b, 0x3C)
$sectionCount = [BitConverter]::ToUInt16($b, $pe + 6)
$optSize = [BitConverter]::ToUInt16($b, $pe + 20)
$opt = $pe + 24
$imageBase = [BitConverter]::ToUInt64($b, $opt + 24)

$sections = @()
for ($i = 0; $i -lt $sectionCount; $i++) {
    $s = $opt + $optSize + ($i * 40)
    $sections += [pscustomobject]@{
        Name           = ([Text.Encoding]::ASCII.GetString($b, $s, 8)).Trim([char]0)
        VirtualSize    = [BitConverter]::ToUInt32($b, $s + 8)
        VirtualAddress = [BitConverter]::ToUInt32($b, $s + 12)
        RawSize        = [BitConverter]::ToUInt32($b, $s + 16)
        RawPointer     = [BitConverter]::ToUInt32($b, $s + 20)
    }
}

function ToRva([int]$offset) {
    foreach ($s in $sections) {
        if ($offset -ge $s.RawPointer -and $offset -lt ($s.RawPointer + $s.RawSize)) {
            return [uint32]($s.VirtualAddress + ($offset - $s.RawPointer))
        }
    }
    return [uint32]0xFFFFFFFF
}

$targetVa = [uint64]([Convert]::ToUInt64($Target, 16))
Write-Output ("image base 0x{0:X}  target 0x{1:X}" -f $imageBase, $targetVa)
Write-Output "sections (virtual size / raw size):"
foreach ($s in $sections) {
    Write-Output ("  {0,-8} rva 0x{1:X8} vsize 0x{2:X8} rsize 0x{3:X8}" -f `
        $s.Name, $s.VirtualAddress, $s.VirtualSize, $s.RawSize)
}
Write-Output ""

$hits = 0
for ($p = 1; $p -lt ($b.Length - 4); $p++) {
    # ModRM with mod = 00 and rm = 101 is the RIP-relative form.
    if (($b[$p - 1] -band 0xC7) -ne 0x05) { continue }

    $nextOffset = $p + 4
    $nextRva = ToRva $nextOffset
    if ($nextRva -eq 0xFFFFFFFF) { continue }

    $disp = [BitConverter]::ToInt32($b, $p)
    $resolved = [uint64]([int64]$imageBase + [int64]$nextRva + [int64]$disp)
    if ($resolved -ne $targetVa) { continue }

    $hits++
    $start = [Math]::Max(0, $p - 3)
    $rva = ToRva $start
    Write-Output ("ref at rva 0x{0:X} (va 0x{1:X})" -f $rva, ($imageBase + $rva))
    $count = [Math]::Min($Context, $b.Length - $start)
    $slice = $b[$start..($start + $count - 1)]
    Write-Output ("  bytes: {0}" -f (($slice | ForEach-Object { $_.ToString('X2') }) -join ' '))
    $ascii = -join ($slice | ForEach-Object {
        if ($_ -ge 32 -and $_ -lt 127) { [char]$_ } else { '.' }
    })
    Write-Output ("  ascii: {0}" -f $ascii)
    Write-Output ""
}

Write-Output ("$hits reference(s) found")
