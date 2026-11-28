param([switch]$BuildOnly)

$ErrorActionPreference = 'Stop'
$repoDirectory = $PSScriptRoot
$buildDirectory = Join-Path $repoDirectory 'out/build/x64-relwithdebinfo'
$developmentExe = Join-Path $buildDirectory 'InvokeInvoiceSystem.exe'

try {
    $runningBuild = Get-Process InvokeInvoiceSystem -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $developmentExe }
    if ($runningBuild) {
        throw 'Save your work and close the development app, then run this launcher again so it can rebuild the EXE.'
    }
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    if (!(Test-Path -LiteralPath $vswhere)) { throw 'Install Visual Studio with the C++ desktop tools first.' }
    $vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (!$vsInstall) { throw 'Visual Studio C++ desktop tools were not found.' }
    & (Join-Path $vsInstall 'Common7/Tools/Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
    Push-Location $repoDirectory
    try {
        if (!(Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt'))) {
            & cmake --preset x64-relwithdebinfo
            if ($LASTEXITCODE) { throw 'Development configuration failed. Check VCPKG_ROOT and the build output above.' }
        }
        & cmake --build --preset x64-relwithdebinfo --parallel 2 --target InvokeInvoiceSystem
        if ($LASTEXITCODE) { throw 'Build failed. The previous EXE has not been launched.' }
    } finally { Pop-Location }
    if (!$BuildOnly) {
        Start-Process -FilePath $developmentExe -ArgumentList '--development' -WorkingDirectory $buildDirectory
        Write-Host 'Opened the latest local development build. Release updates are disabled.'
    }
} catch {
    Write-Host $_.Exception.Message -ForegroundColor Red
    exit 1
}
