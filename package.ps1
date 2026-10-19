param(
    [ValidatePattern('^\d+\.\d+\.\d+$')]
    [string]$Version = '0.1.0',
    [string]$PlatformToolset
)

$ErrorActionPreference = 'Stop'
$buildArgs = @{}
if ($PlatformToolset) { $buildArgs.PlatformToolset = $PlatformToolset }

& (Join-Path $PSScriptRoot 'build.ps1') -Configuration Release @buildArgs
& (Join-Path $PSScriptRoot 'tests\run.ps1') @buildArgs

$exe = Join-Path $PSScriptRoot 'x64\Release\FileHash.exe'
$readme = Join-Path $PSScriptRoot 'README.md'
if (-not (Test-Path -LiteralPath $exe)) { throw 'Release executable was not found.' }

$dist = Join-Path $PSScriptRoot 'dist'
New-Item -ItemType Directory -Path $dist -Force | Out-Null
$zip = Join-Path $dist "FileHash-$Version-win-x64.zip"
Compress-Archive -LiteralPath $exe, $readme -DestinationPath $zip -Force
$digest = (Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash.ToLowerInvariant()
"$digest  $(Split-Path $zip -Leaf)" | Set-Content -LiteralPath "$zip.sha256" -Encoding ascii
Write-Output "Package: $zip"
Write-Output "SHA-256: $digest"

