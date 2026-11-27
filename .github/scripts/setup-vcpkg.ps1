$ErrorActionPreference = 'Stop'
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
