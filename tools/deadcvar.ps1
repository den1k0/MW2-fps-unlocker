# Is a cvar actually used by this build?
#
# A cvar can be registered - so it appears in cvar lists and gets saved to your
# config - while nothing ever reads it. That is invisible from the outside and
# from the config file, and it looks exactly like a feature that is broken.
#
# This script answers the question from the binary:
#   1. find the cvar's name string, giving its registration site
#   2. read the instruction after the registration call to learn where the
#      returned dvar_t pointer is cached (a static)
#   3. count references to that static: the store itself is the registration,
#      so anything beyond it is a reader
#
# 0 readers => the cvar is dead, whatever value you write into it.
#
# Usage: powershell -NoProfile -File deadcvar.ps1 -Exe <path> -Name cg_drawFPS

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    [Parameter(Mandatory = $true)][string]$Name
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
    # [uint32]::MaxValue, not the literal 0xFFFFFFFF: PowerShell parses that
    # literal as the Int32 -1, and the cast then throws.
    return [uint32]::MaxValue
}

function ToOffset([uint32]$rva) {
    foreach ($s in $sections) {
        if ($rva -ge $s.VirtualAddress -and
            $rva -lt ($s.VirtualAddress + $s.RawSize)) {
            return [int]($s.RawPointer + ($rva - $s.VirtualAddress))
        }
    }
    return -1
}

# Every RIP-relative operand that resolves to $targetVa. Returns the file
# offsets of the displacement bytes, so callers can look around the site.
function FindRefs([uint64]$targetVa) {
    $found = New-Object System.Collections.Generic.List[int]
    for ($p = 1; $p -lt ($b.Length - 4); $p++) {
        if (($b[$p - 1] -band 0xC7) -ne 0x05) { continue }
        $nextRva = ToRva ($p + 4)
        if ($nextRva -eq [uint32]::MaxValue) { continue }
        $disp = [BitConverter]::ToInt32($b, $p)
        $resolved = [uint64]([int64]$imageBase + [int64]$nextRva + [int64]$disp)
        if ($resolved -eq $targetVa) { [void]$found.Add($p) }
    }
    return $found
}

Write-Output ("image: {0}  base 0x{1:X}  cvar: {2}" -f (Split-Path -Leaf $Exe), $imageBase, $Name)

# 1. the name string
$needle = [Text.Encoding]::ASCII.GetBytes($Name + [char]0)
$stringOffset = -1
for ($i = 0; $i -le ($b.Length - $needle.Length); $i++) {
    if ($b[$i] -ne $needle[0]) { continue }
    $same = $true
    for ($j = 1; $j -lt $needle.Length; $j++) {
        if ($b[$i + $j] -ne $needle[$j]) { $same = $false; break }
    }
    if (-not $same) { continue }

    # The name must START a string. Without this, a cvar whose name is the tail
    # of another string matches inside it: "sensitivity" is a suffix of the
    # description "Mouse sensitivity", and the address of that interior position
    # is referenced by nothing, which reads as "registered but never used".
    if ($i -gt 0) {
        $previous = $b[$i - 1]
        if ($previous -ge 32 -and $previous -lt 127) { continue }
    }

    $stringOffset = $i
    break
}
if ($stringOffset -lt 0) {
    Write-Output "  the name string does not exist in this file"
    exit 2
}
$stringRva = ToRva $stringOffset
Write-Output ("  name string at rva 0x{0:X}" -f $stringRva)

# 2. registration site = whoever references the name string
$stringRefs = FindRefs ([uint64]($imageBase + $stringRva))
Write-Output ("  {0} reference(s) to the name string" -f $stringRefs.Count)
if ($stringRefs.Count -eq 0) {
    Write-Output "  no registration site found"
    exit 0
}

$staticRvas = New-Object System.Collections.Generic.List[uint64]

foreach ($p in $stringRefs) {
    $siteRva = ToRva ($p - 3)
    Write-Output ("    reference at rva 0x{0:X}" -f $siteRva)

    # The registration call follows the 'lea reg, [name]' almost immediately;
    # then the return value is cached in a static.
    $limit = [Math]::Min($p + 48, $b.Length - 8)
    for ($q = $p; $q -lt $limit; $q++) {
        if ($b[$q] -ne 0x89) { continue }                       # mov r/m64, r64
        if (($b[$q - 1] -band 0xF8) -ne 0x48) { continue }       # REX.W
        if (($b[$q + 1] -band 0xC7) -ne 0x05) { continue }       # RIP-relative
        $nextRva = ToRva ($q + 6)
        if ($nextRva -eq 0xFFFFFFFF) { continue }
        $disp = [BitConverter]::ToInt32($b, $q + 2)
        $static = [uint64]([int64]$imageBase + [int64]$nextRva + [int64]$disp)
        Write-Output ("      dvar pointer cached at rva 0x{0:X}" -f ($static - $imageBase))
        [void]$staticRvas.Add($static)
        break
    }
}

if ($staticRvas.Count -eq 0) {
    Write-Output "  could not identify the cached dvar pointer"
    exit 0
}

# 3. readers of the cached pointer
foreach ($static in $staticRvas) {
    $refs = FindRefs $static
    $readers = $refs.Count - 1   # the store itself is not a reader
    Write-Output ("  {0} reference(s) to the cached pointer, so {1} reader(s)" -f `
        $refs.Count, $readers)
    foreach ($r in $refs) {
        Write-Output ("    rva 0x{0:X}" -f (ToRva ($r - 3)))
    }
    if ($readers -le 0) {
        Write-Output "  VERDICT: registered but never read - the cvar does nothing in this build"
    } else {
        Write-Output "  VERDICT: read by code, so setting it has an effect"
    }
}
