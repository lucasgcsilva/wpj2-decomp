param(
    [string]$RomPath = 'E:\projetos\n64-roms\Wonder Project J2 - Koruro no Mori no Jozet (Japan) [T-En by Ryu v1.0].z64'
)

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$python = 'C:\Users\lucas\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'
$env:PYTHONPATH = Join-Path $projectRoot 'tools\python-deps'

if (-not (Test-Path -LiteralPath $RomPath)) {
    throw "ROM não encontrada: $RomPath"
}

& $python (Join-Path $PSScriptRoot 'scan_rom.py') $RomPath
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
