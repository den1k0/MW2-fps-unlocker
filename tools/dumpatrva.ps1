# Print the section table of a PE and the bytes at one or more RVAs.
#
# Used to identify the fields of the dvar_t by following the pointers the DLL's
# dump revealed: if a slot holds a pointer, the fastest way to learn what the
# field means is to read what it points at.
#
# Usage: powershell -NoProfile -File dumpatrva.ps1 -Exe <path> -Rva 0x21E9F9,0x41FCF0

param(
    [Parameter(Mandatory = $true)][string]$Exe,
    # One comma-separated string rather than a [string[]]: when the script is
    # started with -File from cmd.exe, "a,b" arrives as a single argument and a
    # [string[]] parameter would not split it.
    [Parameter(Mandatory = $true)][string]$Rva
)

$rvaList = $Rva -split ','

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
        VirtualSize    = [BitConverter]::ToUInt32($b, $s + 8)
        VirtualAddress = [BitConverter]::ToUInt32($b, $s + 12)
        RawSize        = [BitConverter]::ToUInt32($b, $s + 16)
        RawPointer     = [BitConverter]::ToUInt32($b, $s + 20)
        Characteristics = [BitConverter]::ToUInt32($b, $s + 36)
    }
}

Write-Output "sections:"
foreach ($s in $sections) {
    $writable = if ($s.Characteristics -band 0x80000000) { 'w' } else { '-' }
    Write-Output ("  {0,-8} rva 0x{1:X8} size 0x{2:X8} raw 0x{3:X8} [{4}]" -f `
        $s.Name, $s.VirtualAddress, $s.RawSize, $s.RawPointer, $writable)
}

function ToOffset([uint32]$rva) {
    foreach ($s in $sections) {
        if ($rva -ge $s.VirtualAddress -and $rva -lt ($s.VirtualAddress + $s.RawSize)) {
            return @($s, [int]($s.RawPointer + ($rva - $s.VirtualAddress)))
        }
    }
    return @($null, -1)
}

foreach ($text in $rvaList) {
    $rva = [uint32]([Convert]::ToUInt32($text, 16))
    $pair = ToOffset $rva
    $section = $pair[0]
    $offset = $pair[1]

    Write-Output ""
    if ($offset -lt 0) {
        Write-Output ("rva 0x{0:X} -> not inside any section" -f $rva)
        continue
    }

    $count = [Math]::Min(80, $b.Length - $offset)
    $slice = $b[$offset..($offset + $count - 1)]
    $ascii = -join ($slice | ForEach-Object {
        if ($_ -ge 32 -and $_ -lt 127) { [char]$_ } else { '.' }
    })
    Write-Output ("rva 0x{0:X} in {1} -> file offset 0x{2:X}" -f $rva, $section.Name, $offset)
    Write-Output ("  ascii: {0}" -f $ascii)
    $hex = ($slice | ForEach-Object { $_.ToString('X2') }) -join ' '
    Write-Output ("  bytes: {0}" -f $hex)
}
