param(
    [ValidateSet("Debug", "Release")]
    [string] $Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$telegramRoot = Join-Path $repoRoot "Telegram"
$outputRoot = Join-Path $repoRoot "out"
$librariesRoot = Join-Path (Split-Path -Parent $repoRoot) "Libraries"
$distRoot = Join-Path $repoRoot "dist-local-admin"
$portableRoot = Join-Path $distRoot "Telegram-LocalAdmin-7.0.9-win64-portable"

if (-not $env:TDESKTOP_API_ID -or -not $env:TDESKTOP_API_HASH) {
    throw "Set TDESKTOP_API_ID and TDESKTOP_API_HASH environment variables first."
}

if (-not (Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    throw "Run this script from the Visual Studio 2026 x64 Native Tools Command Prompt initialized with -vcvars_ver=14.44."
}

if (-not (Test-Path $librariesRoot)) {
    Write-Host "Preparing Telegram Desktop dependencies. This can take a long time."
    & (Join-Path $telegramRoot "build\prepare\win.bat")
    if ($LASTEXITCODE -ne 0) {
        throw "Dependency preparation failed."
    }
}

Push-Location $telegramRoot
try {
    & ".\configure.bat" x64 `
        -D "TDESKTOP_API_ID=$env:TDESKTOP_API_ID" `
        -D "TDESKTOP_API_HASH=$env:TDESKTOP_API_HASH" `
        -D "DESKTOP_APP_DISABLE_AUTOUPDATE=ON"
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configuration failed."
    }
} finally {
    Pop-Location
}

cmake --build $outputRoot --config $Configuration --target Telegram --parallel
if ($LASTEXITCODE -ne 0) {
    throw "Telegram build failed."
}

$binary = Join-Path $outputRoot "$Configuration\Telegram.exe"
if (-not (Test-Path $binary)) {
    throw "Telegram.exe was not found at $binary."
}

New-Item -ItemType Directory -Force $portableRoot | Out-Null
Copy-Item $binary (Join-Path $portableRoot "Telegram.exe") -Force
$portableData = Join-Path $portableRoot "TelegramForcePortable"
New-Item -ItemType Directory -Force $portableData | Out-Null
Set-Content `
    -Path (Join-Path $portableData "PORTABLE_DATA_FOLDER.txt") `
    -Encoding UTF8 `
    -Value "Telegram stores this portable copy's local data in this folder."

$archive = "$portableRoot.zip"
if (Test-Path $archive) {
    Remove-Item $archive -Force
}
Compress-Archive -Path $portableRoot -DestinationPath $archive -CompressionLevel Optimal

$archiveSize = [math]::Round((Get-Item $archive).Length / 1MB, 2)
$binarySize = [math]::Round((Get-Item $binary).Length / 1MB, 2)
Write-Host "Portable archive: $archive ($archiveSize MB)"
Write-Host "Telegram.exe: $binarySize MB"
