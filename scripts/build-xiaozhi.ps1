# Buduje xiaozhi-esp32 z podkatalogu xiaozhi-esp32/ dla plytki Waveshare ESP32-S3-AUDIO-Board.
#
# ESP-IDF nie obsluguje spacji w sciezkach (linker rozcina "Smart spiker"), a idf.py
# rozwiazuje subst/junction do prawdziwej sciezki. Dlatego zrodla sa kopiowane lustrzanie
# do katalogu roboczego bez spacji, tam budowane, a gotowy obraz wraca do firmware/.
#
# Uzycie:  .\scripts\build-xiaozhi.ps1                       (pl-PL)
#          .\scripts\build-xiaozhi.ps1 -Language en-US
#          .\scripts\build-xiaozhi.ps1 -Flash                (od razu wgrywa na COM8)
param(
    [string]$Language = "pl-PL",
    [string]$Board = "waveshare/esp32-s3-audio-board",
    [string]$IdfPath = "C:\esp\esp-idf-v6.1",
    [string]$WorkDir = "C:\esp\work\xiaozhi-esp32",
    [string]$Port = "COM8",
    [switch]$Flash
)

$ErrorActionPreference = "Stop"
$repo = Split-Path $PSScriptRoot -Parent
$src = Join-Path $repo "xiaozhi-esp32"
$outDir = Join-Path $repo "firmware"

. (Join-Path $IdfPath "export.ps1") | Out-Null

# Kopia lustrzana zrodel; build/, managed_components/ i sdkconfig zostaja w katalogu roboczym,
# zeby kolejne budowania byly przyrostowe.
robocopy $src $WorkDir /MIR /NFL /NDL /NJH /NJS /NP `
    /XD (Join-Path $src ".git") (Join-Path $src "build") (Join-Path $src "managed_components") `
        (Join-Path $WorkDir "build") (Join-Path $WorkDir "managed_components") `
    /XF sdkconfig dependencies.lock lang_config.h | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy nie powiodl sie ($LASTEXITCODE)" }

Push-Location $WorkDir
try {
    python scripts/build.py $Board --language $Language
    if ($LASTEXITCODE -ne 0) { throw "Budowanie nie powiodlo sie" }
    $version = (Select-String -Path CMakeLists.txt -Pattern 'set\(PROJECT_VER "([^"]+)"').Matches[0].Groups[1].Value
}
finally {
    Pop-Location
}

New-Item -ItemType Directory -Force $outDir | Out-Null
$image = Join-Path $outDir "xiaozhi-$version-$Language-esp32-s3-audio-board.bin"
Copy-Item (Join-Path $WorkDir "build\merged-binary.bin") $image -Force
Write-Host "Obraz: $image"

if ($Flash) {
    python -m esptool --port $Port -b 921600 write-flash 0x0 $image
    if ($LASTEXITCODE -ne 0) { throw "write-flash nie powiodl sie" }
}
