[CmdletBinding()]
param(
    [string]$CacheDirectory = (Join-Path $env:LOCALAPPDATA 'FriedasBirdview\build-tools')
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

$version = '1.0.4022.49'
$archiveName = "Microsoft.Web.WebView2.$version.nupkg"
$expectedSha256 = 'EE9DE67E5BB9EF3A96C5689B2EFC8188E2DF160A0E79234C0404243782FDE5FB'
$sdkRoot = Join-Path $CacheDirectory "webview2-sdk-$version"
$header = Join-Path $sdkRoot 'build\native\include\WebView2.h'
$loader = Join-Path $sdkRoot 'build\native\x64\WebView2LoaderStatic.lib'

if ((Test-Path -LiteralPath $header) -and (Test-Path -LiteralPath $loader)) {
    Write-Output $sdkRoot
    return
}
if (Test-Path -LiteralPath $sdkRoot) {
    throw "The cached WebView2 SDK is incomplete: $sdkRoot. Remove that exact cache directory and rerun."
}

New-Item -ItemType Directory -Path $CacheDirectory -Force | Out-Null
$archive = Join-Path $CacheDirectory $archiveName
if (-not (Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest `
        -Uri "https://api.nuget.org/v3-flatcontainer/microsoft.web.webview2/$version/microsoft.web.webview2.$version.nupkg" `
        -OutFile $archive
}

$actualSha256 = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash
if (-not $actualSha256.Equals($expectedSha256, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "The WebView2 SDK SHA-256 did not match the pinned value. Remove $archive and rerun."
}

$temporaryArchive = Join-Path $CacheDirectory "$archiveName.zip"
$temporaryRoot = Join-Path $CacheDirectory "webview2-sdk-$version.partial-$PID"
Copy-Item -LiteralPath $archive -Destination $temporaryArchive -Force
try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($temporaryArchive, $temporaryRoot)
    $temporaryHeader = Join-Path $temporaryRoot 'build\native\include\WebView2.h'
    $temporaryLoader = Join-Path $temporaryRoot 'build\native\x64\WebView2LoaderStatic.lib'
    if (-not (Test-Path -LiteralPath $temporaryHeader) -or -not (Test-Path -LiteralPath $temporaryLoader)) {
        throw 'The downloaded WebView2 SDK did not contain the required x64 C++ files.'
    }
    Move-Item -LiteralPath $temporaryRoot -Destination $sdkRoot
} finally {
    Remove-Item -LiteralPath $temporaryArchive -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $temporaryRoot -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Output $sdkRoot
