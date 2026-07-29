[CmdletBinding()]
param(
    [string]$QtRoot = $env:QT_ROOT,
    [string]$VcpkgRoot = 'C:\src\vcpkg',
    [string]$WebView2SdkRoot,
    [string]$BuildDirectory,
    [string]$OutputDirectory,
    [string]$SigningCertificateThumbprint = $env:FRIEDASBIRDVIEW_SIGNING_CERT_THUMBPRINT,
    [string]$TimestampUrl,
    [switch]$SkipInstaller
)

$ErrorActionPreference = 'Stop'

if ([string]::IsNullOrWhiteSpace($BuildDirectory)) {
    $BuildDirectory = Join-Path $PSScriptRoot '..\..\build-win-package'
}
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $PSScriptRoot '..\..\dist'
}

function Invoke-Checked {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$FilePath failed with exit code $LASTEXITCODE."
    }
}

function Find-InnoSetupCompiler {
    $command = Get-Command ISCC.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $candidates = @(
        (Join-Path $env:LOCALAPPDATA 'Programs\Inno Setup 6\ISCC.exe'),
        (Join-Path ${env:ProgramFiles(x86)} 'Inno Setup 6\ISCC.exe'),
        (Join-Path $env:ProgramFiles 'Inno Setup 6\ISCC.exe')
    )
    return $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
}

function Find-SignTool {
    $command = Get-Command signtool.exe -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $windowsSdkBin = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10\bin'
    if (Test-Path -LiteralPath $windowsSdkBin) {
        return Get-ChildItem -LiteralPath $windowsSdkBin -Filter signtool.exe -Recurse -ErrorAction SilentlyContinue |
            Where-Object { $_.DirectoryName -match '[\\/]x64$' } |
            Sort-Object FullName -Descending |
            Select-Object -First 1 -ExpandProperty FullName
    }
    return $null
}

function Copy-VcpkgOpenSslRuntime {
    param(
        [string]$VcpkgRoot,
        [string]$DestinationDirectory
    )

    $runtimeDirectory = Join-Path $VcpkgRoot 'installed\x64-windows\bin'
    if (-not (Test-Path -LiteralPath $runtimeDirectory)) {
        throw "The vcpkg OpenSSL runtime directory is missing: $runtimeDirectory"
    }

    $runtimeLibraries = Get-ChildItem -LiteralPath $runtimeDirectory -File |
        Where-Object { $_.Name -match '^lib(crypto|ssl)-[0-9]+-x64\.dll$' } |
        Sort-Object Name
    if (-not ($runtimeLibraries | Where-Object Name -match '^libcrypto-')) {
        throw "vcpkg did not provide a libcrypto runtime DLL in $runtimeDirectory"
    }

    foreach ($library in $runtimeLibraries) {
        Copy-Item -LiteralPath $library.FullName -Destination (Join-Path $DestinationDirectory $library.Name) -Force
    }
}

function Get-SigningCertificate {
    param([string]$Thumbprint)

    $normalizedThumbprint = ($Thumbprint -replace '\s', '').ToUpperInvariant()
    if ($normalizedThumbprint -notmatch '^[0-9A-F]{40}$') {
        throw 'SigningCertificateThumbprint must be the 40-character thumbprint of a CurrentUser\My code-signing certificate.'
    }

    $certificate = Get-ChildItem -Path Cert:\CurrentUser\My |
        Where-Object { $_.Thumbprint -eq $normalizedThumbprint } |
        Select-Object -First 1
    if (-not $certificate -or -not $certificate.HasPrivateKey) {
        throw "No private signing key was found for certificate thumbprint $normalizedThumbprint in Cert:\CurrentUser\My."
    }
    if ($certificate.NotAfter -le (Get-Date)) {
        throw "The signing certificate $normalizedThumbprint has expired."
    }
    if (-not ($certificate.EnhancedKeyUsageList | Where-Object { $_.ObjectId.Value -eq '1.3.6.1.5.5.7.3.3' })) {
        throw "The certificate $normalizedThumbprint is not valid for code signing."
    }
    return $certificate
}

function Invoke-SignFile {
    param(
        [string]$SignTool,
        [string]$Thumbprint,
        [string]$FilePath,
        [string]$Rfc3161TimestampUrl
    )

    $arguments = @('sign', '/fd', 'SHA256', '/sha1', $Thumbprint, '/s', 'My')
    if (-not [string]::IsNullOrWhiteSpace($Rfc3161TimestampUrl)) {
        $arguments += @('/tr', $Rfc3161TimestampUrl, '/td', 'SHA256')
    }
    $arguments += $FilePath
    Invoke-Checked $SignTool $arguments
    Invoke-Checked $SignTool @('verify', '/pa', '/all', $FilePath)
}

if ([string]::IsNullOrWhiteSpace($QtRoot)) {
    throw 'Set QT_ROOT to the Qt msvc2022_64 kit, then rerun this script from a Visual Studio Developer PowerShell.'
}
if ([string]::IsNullOrWhiteSpace($env:VCToolsRedistDir)) {
    throw 'Run this script from a Visual Studio Developer PowerShell so windeployqt can bundle vc_redist.x64.exe.'
}

$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$buildDirectory = [System.IO.Path]::GetFullPath($BuildDirectory)
$outputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
$webView2SdkScript = Join-Path $PSScriptRoot 'ensure-webview2-sdk.ps1'
if ([string]::IsNullOrWhiteSpace($WebView2SdkRoot)) {
    $WebView2SdkRoot = (& $webView2SdkScript | Select-Object -Last 1).Trim()
}
$WebView2SdkRoot = [System.IO.Path]::GetFullPath($WebView2SdkRoot)
$qt6Directory = Join-Path $QtRoot 'lib\cmake\Qt6'
$toolchainFile = Join-Path $VcpkgRoot 'scripts\buildsystems\vcpkg.cmake'
$windeployqt = Join-Path $QtRoot 'bin\windeployqt.exe'
$signTool = $null
$signingCertificate = $null
if (-not [string]::IsNullOrWhiteSpace($SigningCertificateThumbprint)) {
    $signingCertificate = Get-SigningCertificate $SigningCertificateThumbprint
    $signTool = Find-SignTool
    if (-not $signTool) {
        throw 'signtool.exe was not found. Install the Windows SDK Signing Tools or rerun from a Visual Studio Developer PowerShell.'
    }
}

foreach ($path in @(
    $qt6Directory,
    $toolchainFile,
    $windeployqt,
    $webView2SdkScript,
    (Join-Path $WebView2SdkRoot 'build\native\include\WebView2.h'),
    (Join-Path $WebView2SdkRoot 'build\native\x64\WebView2LoaderStatic.lib')
)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required packaging input is missing: $path"
    }
}

$cmakeLists = Get-Content -LiteralPath (Join-Path $repositoryRoot 'CMakeLists.txt') -Raw
if ($cmakeLists -notmatch 'project\(FriedasBirdview VERSION ([0-9][^ )]*)') {
    throw 'Could not determine the FriedasBirdview version from CMakeLists.txt.'
}
$version = $Matches[1]
$stageDirectory = Join-Path $outputDirectory "FriedasBirdview-$version-win64"

Invoke-Checked cmake @(
    '-S', $repositoryRoot,
    '-B', $buildDirectory,
    '-G', 'Ninja',
    '-DCMAKE_BUILD_TYPE=Release',
    "-DQt6_DIR=$qt6Directory",
    "-DCMAKE_TOOLCHAIN_FILE=$toolchainFile",
    "-DFRIEDASBIRDVIEW_WEBVIEW2_SDK_ROOT=$WebView2SdkRoot"
)
Invoke-Checked cmake @('--build', $buildDirectory, '--parallel')

if (Test-Path -LiteralPath $stageDirectory) {
    Remove-Item -LiteralPath $stageDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
New-Item -ItemType Directory -Path $stageDirectory -Force | Out-Null
Invoke-Checked cmake @('--install', $buildDirectory, '--prefix', $stageDirectory)

$executable = Join-Path $stageDirectory 'bin\friedasbirdview.exe'
if (-not (Test-Path -LiteralPath $executable)) {
    throw "The installed executable is missing: $executable"
}
Invoke-Checked $windeployqt @('--release', '--compiler-runtime', $executable)

$runtimeDirectory = Join-Path $stageDirectory 'bin'
Copy-VcpkgOpenSslRuntime $VcpkgRoot $runtimeDirectory

$redist = Join-Path $runtimeDirectory 'vc_redist.x64.exe'
if (-not (Test-Path -LiteralPath $redist)) {
    throw 'windeployqt did not bundle vc_redist.x64.exe. Confirm this is a Visual Studio Developer PowerShell.'
}

Copy-Item -LiteralPath (Join-Path $repositoryRoot 'LICENSE') -Destination (Join-Path $stageDirectory 'LICENSE.txt') -Force
Copy-Item -LiteralPath (Join-Path $repositoryRoot 'THIRD_PARTY_NOTICES.md') -Destination (Join-Path $stageDirectory 'THIRD_PARTY_NOTICES.md') -Force

if ($signingCertificate) {
    Invoke-SignFile $signTool $signingCertificate.Thumbprint $executable $TimestampUrl
}

if ($SkipInstaller) {
    Write-Output "Deployment directory created: $stageDirectory"
    return
}

$innoSetup = Find-InnoSetupCompiler
if (-not $innoSetup) {
    $zipPath = "$stageDirectory.zip"
    if (Test-Path -LiteralPath $zipPath) {
        Remove-Item -LiteralPath $zipPath -Force
    }
    Compress-Archive -Path (Join-Path $stageDirectory '*') -DestinationPath $zipPath -CompressionLevel Optimal
    Write-Warning "Inno Setup 6 was not found. Created a portable ZIP instead: $zipPath"
    Write-Warning 'Install Inno Setup 6 and rerun without -SkipInstaller to create the normal-user Setup.exe.'
    return
}

Invoke-Checked $innoSetup @(
    "/DSourceDir=$stageDirectory",
    "/DOutputDir=$outputDirectory",
    "/DAppVersion=$version",
    (Join-Path $PSScriptRoot 'FriedasBirdview.iss')
)

if ($signingCertificate) {
    $installer = Join-Path $outputDirectory "FriedasBirdview-$version-Setup-x64.exe"
    if (-not (Test-Path -LiteralPath $installer)) {
        throw "The generated installer is missing: $installer"
    }
    Invoke-SignFile $signTool $signingCertificate.Thumbprint $installer $TimestampUrl
}
