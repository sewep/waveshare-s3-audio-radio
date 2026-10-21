# Przywraca fabryczny firmware (xiaozhi 2.4.2) z pelnej kopii flasha 16 MB.
# Uzycie:  .\backup\restore-factory.ps1            (domyslnie COM8)
#          .\backup\restore-factory.ps1 -Port COM5
param([string]$Port = "COM8")

$ErrorActionPreference = "Stop"
$image = Join-Path $PSScriptRoot "factory-xiaozhi-2.4.2-full16MB.bin"
$expected = (Get-Content (Join-Path $PSScriptRoot "SHA256SUMS")).Split(" ")[0]

if (-not (Test-Path $image)) { throw "Brak pliku kopii: $image" }
$actual = (Get-FileHash $image -Algorithm SHA256).Hash.ToLower()
if ($actual -ne $expected) { throw "Suma SHA256 kopii sie nie zgadza - plik uszkodzony, przerywam." }
Write-Host "Kopia OK ($actual)"

# Zapis calego flasha od 0x0 (bootloader, partycje, NVS, oba sloty OTA, assets).
python -m esptool --port $Port -b 921600 write-flash 0x0 $image
if ($LASTEXITCODE -ne 0) { throw "write-flash nie powiodl sie" }

python -m esptool --port $Port -b 921600 --after hard-reset verify-flash 0x0 $image
if ($LASTEXITCODE -ne 0) { throw "Weryfikacja po zapisie nie powiodla sie" }
Write-Host "Przywrocono fabryczny firmware."
