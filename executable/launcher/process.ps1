# Compatibility wrapper only. The C++ executable is the single authoritative implementation.
param(
    [string]$ConfigPath = ""
)

$modRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exePath = Join-Path $modRoot "FarCry4_FPSFix.exe"

if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
    Write-Error "FarCry4_FPSFix.exe was not found at '$exePath'. Build the executable and place it in the mod root folder."
    exit 1
}

if ($ConfigPath) {
    $expectedConfig = Join-Path $modRoot "config\settings.json"
    if ([IO.Path]::GetFullPath($ConfigPath) -ne [IO.Path]::GetFullPath($expectedConfig)) {
        Write-Warning "V2 reads configuration from '$expectedConfig'. The legacy ConfigPath argument is ignored."
    }
}

& $exePath
exit $LASTEXITCODE
