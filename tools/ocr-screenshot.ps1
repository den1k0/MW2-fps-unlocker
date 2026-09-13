# Extracts text from a screenshot using the OCR engine built into Windows 10/11.
# Used to read x64dbg UI screenshots that the assistant cannot view directly.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\ocr-screenshot.ps1 -Path "shot.png"
#
# Writes the result to ocr_result.txt in the current directory.
# Recognises with every installed language (both English and Russian are tried)
# so that translated UI strings are captured too. Requires Windows PowerShell
# 5.1 (powershell.exe), not PowerShell 7.

param(
    [Parameter(Mandatory = $true)]
    [string]$Path
)

$ErrorActionPreference = 'Stop'

[void][Windows.Media.Ocr.OcrEngine, Windows.Foundation, ContentType = WindowsRuntime]
[void][Windows.Graphics.Imaging.BitmapDecoder, Windows.Foundation, ContentType = WindowsRuntime]
[void][Windows.Storage.StorageFile, Windows.Foundation, ContentType = WindowsRuntime]

Add-Type -AssemblyName System.Runtime.WindowsRuntime

$asTaskGeneric = ([System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object {
    $_.Name -eq 'AsTask' -and
    $_.GetParameters().Count -eq 1 -and
    $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
})[0]

function Await($WinRtTask, $ResultType) {
    $asTask = $asTaskGeneric.MakeGenericMethod($ResultType)
    $netTask = $asTask.Invoke($null, @($WinRtTask))
    $netTask.Wait(-1) | Out-Null
    $netTask.Result
}

$out = New-Object System.Collections.Generic.List[string]

$languages = [Windows.Media.Ocr.OcrEngine]::AvailableRecognizerLanguages
$out.Add("=== OCR LANGUAGES ===")
foreach ($l in $languages) { $out.Add(" - " + $l.LanguageTag) }

$full = (Resolve-Path -LiteralPath $Path).Path
$out.Add("=== FILE: $full ===")

$file = Await ([Windows.Storage.StorageFile]::GetFileFromPathAsync($full)) ([Windows.Storage.StorageFile])
$stream = Await ($file.OpenAsync([Windows.Storage.FileAccessMode]::Read)) ([Windows.Storage.Streams.IRandomAccessStream])
$decoder = Await ([Windows.Graphics.Imaging.BitmapDecoder]::CreateAsync($stream)) ([Windows.Graphics.Imaging.BitmapDecoder])
$bitmap = Await ($decoder.GetSoftwareBitmapAsync()) ([Windows.Graphics.Imaging.SoftwareBitmap])
$out.Add("IMAGE SIZE: " + $bitmap.PixelWidth + "x" + $bitmap.PixelHeight)

# Try every installed recogniser; different engines catch different scripts.
foreach ($tag in @('ru', 'en-US', 'en-GB')) {
    $lang = $languages | Where-Object { $_.LanguageTag -eq $tag } | Select-Object -First 1
    if (-not $lang) { continue }

    $engine = [Windows.Media.Ocr.OcrEngine]::TryCreateFromLanguage($lang)
    if (-not $engine) { continue }

    $result = Await ($engine.RecognizeAsync($bitmap)) ([Windows.Media.Ocr.OcrResult])
    $out.Add("=== RESULT (" + $tag + ") ===")
    foreach ($line in $result.Lines) { $out.Add([string]$line.Text) }
}

$out.Add("=== END ===")
Set-Content -Path 'ocr_result.txt' -Value $out -Encoding UTF8
Write-Host "written: ocr_result.txt"
