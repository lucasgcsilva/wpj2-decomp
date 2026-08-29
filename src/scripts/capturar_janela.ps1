param(
    [Parameter(Mandatory = $true)][string]$Titulo,
    [Parameter(Mandatory = $true)][string]$Saida
)

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Wpj2WindowCapture {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
}
"@
Add-Type -AssemblyName System.Drawing

$processo = Get-Process | Where-Object {
    $_.MainWindowHandle -ne 0 -and $_.MainWindowTitle -like "*$Titulo*"
} | Select-Object -First 1
if (-not $processo) { throw "Janela nao encontrada: $Titulo" }

$rect = New-Object Wpj2WindowCapture+RECT
if (-not [Wpj2WindowCapture]::GetWindowRect($processo.MainWindowHandle, [ref]$rect)) {
    throw "Nao foi possivel obter a area da janela"
}
$largura = $rect.Right - $rect.Left
$altura = $rect.Bottom - $rect.Top
$bitmap = New-Object System.Drawing.Bitmap $largura, $altura
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
try {
    $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
    $pasta = Split-Path -Parent $Saida
    if ($pasta) { New-Item -ItemType Directory -Force -Path $pasta | Out-Null }
    $bitmap.Save($Saida, [System.Drawing.Imaging.ImageFormat]::Png)
} finally {
    $graphics.Dispose()
    $bitmap.Dispose()
}
Write-Output $Saida
