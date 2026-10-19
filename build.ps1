param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Release',
    [string]$PlatformToolset
)

$ErrorActionPreference = 'Stop'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw 'Visual Studio Installer (vswhere.exe) was not found.'
}

$msbuild = & $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' |
    Where-Object { $_ -notmatch '[\\/]amd64[\\/]' } |
    Select-Object -First 1
if (-not $msbuild) {
    throw 'MSBuild was not found. Install the Desktop development with C++ workload.'
}

$arguments = @(
    (Join-Path $PSScriptRoot 'FileHash.vcxproj'),
    '/t:Build',
    "/p:Configuration=$Configuration",
    '/p:Platform=x64',
    '/p:PreferredToolArchitecture=x64',
    '/nologo',
    '/verbosity:minimal'
)
if ($PlatformToolset) {
    $arguments += "/p:PlatformToolset=$PlatformToolset"
}

& $msbuild @arguments
if ($LASTEXITCODE -ne 0) {
    throw "FileHash build failed with exit code $LASTEXITCODE."
}

