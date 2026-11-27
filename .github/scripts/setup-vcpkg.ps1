$ErrorActionPreference = 'Stop'
# MSVC setup overwrites VCPKG_ROOT with Visual Studio's bundled copy. Always use
# the isolated CI checkout, then export it again for the following build steps.
$env:VCPKG_ROOT = Join-Path $env:GITHUB_WORKSPACE '.ci-vcpkg'
"VCPKG_ROOT=$env:VCPKG_ROOT" >> $env:GITHUB_ENV
New-Item -ItemType Directory -Force $env:VCPKG_DEFAULT_BINARY_CACHE | Out-Null
$baseline = (Get-Content vcpkg-configuration.json -Raw | ConvertFrom-Json).'default-registry'.baseline
git init $env:VCPKG_ROOT
if ($LASTEXITCODE) { exit $LASTEXITCODE }
git -C $env:VCPKG_ROOT remote add origin https://github.com/microsoft/vcpkg.git
if ($LASTEXITCODE) { exit $LASTEXITCODE }
git -C $env:VCPKG_ROOT fetch --depth 1 origin $baseline
if ($LASTEXITCODE) { exit $LASTEXITCODE }
git -C $env:VCPKG_ROOT checkout --detach FETCH_HEAD
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& "$env:VCPKG_ROOT/bootstrap-vcpkg.bat" -disableMetrics
exit $LASTEXITCODE
