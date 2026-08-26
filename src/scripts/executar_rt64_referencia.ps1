[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$projectRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
$projectsRoot = Split-Path -Parent $projectRoot
$sourceRom = Join-Path $projectsRoot 'n64-roms\Wonder Project J2 - Japan.n64'
$referenceRoot = Join-Path $projectRoot 'tools\wpj2-recomp'
$referenceExe = Join-Path $referenceRoot 'build-wsl-codex\wpj2'
$tempRoot = Join-Path $projectRoot 'temp\projeto\rt64_ref'
$convertedRom = Join-Path $tempRoot 'wpj2.j1.z64'
$expectedMd5 = '0FF1F8628D8FE69582DB54572D2BEA79'

function Convert-ToWslPath([string] $Path) {
    $full = [IO.Path]::GetFullPath($Path)
    if ($full -notmatch '^([A-Za-z]):\\(.*)$') {
        throw "Caminho Windows nao suportado pelo lancador WSL: $full"
    }

    $drive = $Matches[1].ToLowerInvariant()
    $tail = $Matches[2].Replace('\\', '/')
    return "/mnt/$drive/$tail"
}

if (-not (Test-Path -LiteralPath $sourceRom -PathType Leaf)) {
    throw "ROM japonesa nao encontrada: $sourceRom"
}
if (-not (Test-Path -LiteralPath $referenceExe -PathType Leaf)) {
    throw "Binario RT64 nao encontrado: $referenceExe`nCompile primeiro o alvo wpj2 em tools/wpj2-recomp/build-wsl-codex."
}

New-Item -ItemType Directory -Force -Path $tempRoot | Out-Null
Get-ChildItem -LiteralPath $tempRoot -Force -ErrorAction SilentlyContinue |
    Remove-Item -Force -Recurse

try {
    $rom = [IO.File]::ReadAllBytes($sourceRom)
    if ($rom.Length -ne 8MB) {
        throw "Tamanho inesperado da ROM japonesa: $($rom.Length) bytes"
    }

    # O arquivo fornecido usa ordem V64 (37 80 40 12), embora tenha extensao
    # .n64. O recomp externo e seu hash esperam Z64 (80 37 12 40).
    if ($rom[0] -eq 0x37 -and $rom[1] -eq 0x80 -and
        $rom[2] -eq 0x40 -and $rom[3] -eq 0x12) {
        for ($i = 0; $i -lt $rom.Length; $i += 2) {
            $tmp = $rom[$i]
            $rom[$i] = $rom[$i + 1]
            $rom[$i + 1] = $tmp
        }
    }
    elseif (-not ($rom[0] -eq 0x80 -and $rom[1] -eq 0x37 -and
                   $rom[2] -eq 0x12 -and $rom[3] -eq 0x40)) {
        throw 'Cabecalho da ROM nao e V64 nem Z64 reconhecido.'
    }

    [IO.File]::WriteAllBytes($convertedRom, $rom)
    $actualMd5 = (Get-FileHash -LiteralPath $convertedRom -Algorithm MD5).Hash
    if ($actualMd5 -ne $expectedMd5) {
        throw "ROM japonesa nao corresponde ao dump esperado. MD5: $actualMd5"
    }

    $wslRom = Convert-ToWslPath $convertedRom
    $copyCommand = "mkdir -p ~/.config/wpj2-recomp && cp '$wslRom' ~/.config/wpj2-recomp/wpj2_jp.z64"
    & wsl.exe -d Debian -- bash -lc $copyCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Falha ao preparar a ROM no Debian WSL (codigo $LASTEXITCODE)."
    }

    $wslReferenceRoot = Convert-ToWslPath $referenceRoot
    $stdoutLog = Join-Path $tempRoot 'stdout.log'
    $stderrLog = Join-Path $tempRoot 'stderr.log'
    $arguments = @(
        '-d', 'Debian',
        '--cd', $wslReferenceRoot,
        '--', 'env', 'SDL_VIDEODRIVER=x11',
        './build-wsl-codex/wpj2'
    )

    Write-Host 'Abrindo a referencia RT64 (ROM japonesa, sem PT-BR)...'
    $process = Start-Process -FilePath "$env:SystemRoot\System32\wsl.exe" `
        -ArgumentList $arguments -WindowStyle Hidden -PassThru -Wait `
        -RedirectStandardOutput $stdoutLog -RedirectStandardError $stderrLog
    exit $process.ExitCode
}
finally {
    Remove-Item -LiteralPath $convertedRom -Force -ErrorAction SilentlyContinue
}
