# List printable ASCII strings in a PE that contain a pattern, with their RVAs.
#
# Short cvar names are easy to misremember, and the engine's spelling is not
# always the documented one (cg_drawFPS, not cg_drawfps). Searching for what is
# actually in the file settles the name before anything else is checked.
#
# Usage: powershell -NoProfile -File findstrings.ps1 -Exe <path> -Pattern lagomet

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Pattern,
    [int]$MinLength = 3
)

$b = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Exe).Path)
$pe = [BitConverter]::ToInt32($b, 0x3C)
$sectionCount = [BitConverter]::ToUInt16($b, $pe + 6)
$optSize = [BitConverter]::ToUInt16($b, $pe + 20)
$opt = $pe + 24

$sections = @()
for ($i = 0; $i -lt $sectionCount; $i++) {
    $s = $opt + $optSize + ($i * 40)
    $sections += [pscustomobject]@{
        Name           = ([Text.Encoding]::ASCII.GetString($b, $s, 8)).Trim([char]0)
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

function IsPrintable([byte]$v) { return ($v -ge 32 -and $v -lt 127) }

Write-Output ("image: {0}  looking for '{1}'" -f (Split-Path -Leaf $Exe), $Pattern)

$seen = @{}
$hits = 0

for ($i = 0; $i -lt ($b.Length - $Pattern.Length); $i++) {
    if (-not (IsPrintable $b[$i])) { continue }

    # Cheap reject before doing any string work.
    $first = [char]$b[$i]
    if ($first.ToString().ToLower() -ne $Pattern[0].ToString().ToLower()) { continue }

    # Walk back to the start of the string and forward to its end.
    $start = $i
    while ($start -gt 0 -and (IsPrintable $b[$start - 1])) { $start-- }
    $end = $i
    while ($end -lt $b.Length -and (IsPrintable $b[$end])) { $end++ }

    $length = $end - $start
    if ($length -lt $MinLength) { continue }

    $text = [Text.Encoding]::ASCII.GetString($b, $start, $length)
    if ($text.IndexOf($Pattern, [StringComparison]::OrdinalIgnoreCase) -lt 0) { continue }

    $key = "{0}:{1}" -f $start, $text
    if ($seen.ContainsKey($key)) { continue }
    $seen[$key] = $true

    $rva = ToRva $start
    $where = if ($rva -eq 0xFFFFFFFF) { '?' } else { '0x{0:X}' -f $rva }
    Write-Output ("  rva {0,-10} {1}" -f $where, $text)
    $hits++
}

Write-Output ("{0} string(s)" -f $hits)
