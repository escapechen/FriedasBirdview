#!/usr/bin/env bash
# Build a tagged Windows release on a configured VM and optionally attach it to
# its existing GitHub Release. Configuration stays in the ignored local file.
set -euo pipefail
IFS=$'\n\t'

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
default_config="$script_dir/build-and-install-windows.conf"

die() {
    printf 'error: %s\n' "$*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: ./build-and-install-windows.sh [--publish] [--config FILE] vX.Y.Z

Builds and tests the exact tagged checkout already present on the configured
Windows VM, downloads the Setup installer and its SHA-256 manifest, and
verifies the download locally. --publish additionally attaches both files to
the existing GitHub Release. It never creates a release, tag, or commit.
EOF
}

publish=false
config_file=$default_config
release_tag=
while (($#)); do
    case "$1" in
        --publish)
            publish=true
            ;;
        --config)
            (($# >= 2)) || die '--config needs a file path.'
            config_file=$2
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        -*)
            die "Unknown option: $1"
            ;;
        *)
            [[ -z $release_tag ]] || die 'Provide exactly one release tag.'
            release_tag=$1
            ;;
    esac
    shift
done

[[ -n $release_tag ]] || {
    usage >&2
    exit 2
}
[[ $release_tag =~ ^v[0-9]+[.][0-9]+[.][0-9]+([.-][0-9A-Za-z][0-9A-Za-z.-]*)?$ ]] ||
    die "Release tag must look like v1.2.3, not '$release_tag'."
[[ -r $config_file ]] || die "Configuration file is missing: $config_file (copy build-and-install-windows.conf.example first)."

# This is deliberately a shell file so Windows paths do not need a second
# parser. It is user-owned, ignored, and must never be taken from an untrusted
# source.
# shellcheck disable=SC1090
source "$config_file"

: "${WINDOWS_SSH_PORT:=22}"
: "${WINDOWS_SSH_IDENTITY_FILE:=}"
: "${WINDOWS_SIGNING_CERT_THUMBPRINT:=}"
: "${WINDOWS_SIGNING_TIMESTAMP_URL:=}"
: "${WINDOWS_ALLOW_NO_CTEST_TESTS:=0}"

require_config() {
    local name=$1
    [[ -n ${!name:-} ]] || die "$name must be set in $config_file."
}

for name in WINDOWS_SSH_TARGET WINDOWS_REPOSITORY_DIR WINDOWS_QT_ROOT WINDOWS_VCPKG_ROOT WINDOWS_VSDEVCMD GITHUB_REPOSITORY; do
    require_config "$name"
done

[[ $WINDOWS_SSH_PORT =~ ^[0-9]{1,5}$ ]] || die 'WINDOWS_SSH_PORT must be a TCP port number.'
[[ $WINDOWS_ALLOW_NO_CTEST_TESTS == 0 || $WINDOWS_ALLOW_NO_CTEST_TESTS == 1 ]] ||
    die 'WINDOWS_ALLOW_NO_CTEST_TESTS must be 0 or 1.'

reject_line_breaks() {
    local name=$1 value=$2
    [[ $value != *$'\n'* && $value != *$'\r'* ]] || die "$name must not contain a line break."
}

for name in WINDOWS_SSH_TARGET WINDOWS_REPOSITORY_DIR WINDOWS_QT_ROOT WINDOWS_VCPKG_ROOT WINDOWS_VSDEVCMD GITHUB_REPOSITORY WINDOWS_SIGNING_CERT_THUMBPRINT WINDOWS_SIGNING_TIMESTAMP_URL; do
    reject_line_breaks "$name" "${!name}"
done

if [[ -n $WINDOWS_SSH_IDENTITY_FILE ]]; then
    [[ -r $WINDOWS_SSH_IDENTITY_FILE ]] || die "WINDOWS_SSH_IDENTITY_FILE is not readable: $WINDOWS_SSH_IDENTITY_FILE"
fi

powershell_literal() {
    local value=${1//\'/\'\'}
    printf "'%s'" "$value"
}

ssh_options=(-o BatchMode=yes -p "$WINDOWS_SSH_PORT")
scp_options=(-o BatchMode=yes -P "$WINDOWS_SSH_PORT")
if [[ -n $WINDOWS_SSH_IDENTITY_FILE ]]; then
    ssh_options+=(-i "$WINDOWS_SSH_IDENTITY_FILE")
    scp_options+=(-i "$WINDOWS_SSH_IDENTITY_FILE")
fi

allow_no_ctests_ps='$false'
if [[ $WINDOWS_ALLOW_NO_CTEST_TESTS == 1 ]]; then
    allow_no_ctests_ps='$true'
fi

remote_settings=$(printf '%s\n' \
    "\$RepositoryDir = $(powershell_literal "$WINDOWS_REPOSITORY_DIR")" \
    "\$QtRoot = $(powershell_literal "$WINDOWS_QT_ROOT")" \
    "\$VcpkgRoot = $(powershell_literal "$WINDOWS_VCPKG_ROOT")" \
    "\$VsDevCmd = $(powershell_literal "$WINDOWS_VSDEVCMD")" \
    "\$ReleaseTag = $(powershell_literal "$release_tag")" \
    "\$SigningCertificateThumbprint = $(powershell_literal "$WINDOWS_SIGNING_CERT_THUMBPRINT")" \
    "\$SigningTimestampUrl = $(powershell_literal "$WINDOWS_SIGNING_TIMESTAMP_URL")" \
    "\$AllowNoCTests = $allow_no_ctests_ps")

read -r -d '' remote_job <<'POWERSHELL' || true
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'

function Import-DeveloperEnvironment {
    if (-not (Test-Path -LiteralPath $VsDevCmd)) {
        throw "Visual Studio developer command file is missing: $VsDevCmd"
    }

    $command = 'call "' + $VsDevCmd.Replace('"', '""') + '" -arch=x64 -host_arch=x64 >nul && set'
    $environmentLines = & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) {
        throw "VsDevCmd failed with exit code $LASTEXITCODE."
    }
    foreach ($line in $environmentLines) {
        if ($line -match '^(?<name>[^=]+)=(?<value>.*)$') {
            [System.Environment]::SetEnvironmentVariable($matches.name, $matches.value, 'Process')
        }
    }
}

if (-not (Test-Path -LiteralPath $RepositoryDir)) {
    throw "Windows repository is missing: $RepositoryDir"
}

$gitStatus = & git.exe -C $RepositoryDir status --porcelain
if ($LASTEXITCODE -ne 0) {
    throw 'Could not inspect the Windows checkout with git.'
}
if (-not [string]::IsNullOrWhiteSpace(($gitStatus -join "`n"))) {
    throw 'The Windows checkout is dirty. Commit/stash its changes and check out the release tag before publishing.'
}

$headCommitOutput = & git.exe -C $RepositoryDir rev-parse HEAD
if ($LASTEXITCODE -ne 0) {
    throw 'Could not determine the Windows checkout commit.'
}
$headCommit = ($headCommitOutput -join "`n").Trim()
$tagCommitOutput = & git.exe -C $RepositoryDir rev-list -n 1 $ReleaseTag
if ($LASTEXITCODE -ne 0) {
    throw "The Windows checkout does not know release tag $ReleaseTag. Fetch tags, then check it out."
}
$tagCommit = ($tagCommitOutput -join "`n").Trim()
if ([string]::IsNullOrWhiteSpace($tagCommit)) {
    throw "The Windows checkout does not know release tag $ReleaseTag. Fetch tags, then check it out."
}
if ($headCommit -ne $tagCommit) {
    throw "The Windows checkout is not at $ReleaseTag. It is at $headCommit."
}

$cmakeLists = Get-Content -LiteralPath (Join-Path $RepositoryDir 'CMakeLists.txt') -Raw
$versionMatch = [regex]::Match($cmakeLists, 'project\(FriedasBirdview VERSION ([0-9][^ )]*)')
if (-not $versionMatch.Success) {
    throw 'Could not determine the CMake project version.'
}
$version = $versionMatch.Groups[1].Value
if ($ReleaseTag -ne "v$version") {
    throw "Release tag $ReleaseTag does not match CMake project version $version."
}

Import-DeveloperEnvironment
$env:QT_ROOT = $QtRoot

$buildDirectory = Join-Path $RepositoryDir 'build-win-package'
$outputDirectory = Join-Path $RepositoryDir 'dist'
$packageScript = Join-Path $RepositoryDir 'packaging\windows\package.ps1'
$packageParameters = @{
    QtRoot = $QtRoot
    VcpkgRoot = $VcpkgRoot
    BuildDirectory = $buildDirectory
    OutputDirectory = $outputDirectory
}
if (-not [string]::IsNullOrWhiteSpace($SigningCertificateThumbprint)) {
    $packageParameters.SigningCertificateThumbprint = $SigningCertificateThumbprint
}
if (-not [string]::IsNullOrWhiteSpace($SigningTimestampUrl)) {
    $packageParameters.TimestampUrl = $SigningTimestampUrl
}

& $packageScript @packageParameters
if (-not $?) {
    throw 'Windows packaging failed.'
}

$testList = & ctest.exe --test-dir $buildDirectory -N 2>&1
if ($LASTEXITCODE -ne 0) {
    throw "CTest discovery failed with exit code $LASTEXITCODE."
}
$testCountMatch = [regex]::Match(($testList -join "`n"), 'Total Tests:\s+(\d+)')
if (-not $testCountMatch.Success) {
    throw 'Could not determine the number of registered CTest tests.'
}
$testCount = [int]$testCountMatch.Groups[1].Value
if ($testCount -eq 0 -and -not $AllowNoCTests) {
    throw 'No CTest tests are registered; refusing to publish. Add tests or explicitly set WINDOWS_ALLOW_NO_CTEST_TESTS=1 for a temporary exception.'
}
if ($testCount -gt 0) {
    & ctest.exe --test-dir $buildDirectory --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        throw "CTest failed with exit code $LASTEXITCODE."
    }
} else {
    Write-Warning 'No CTest tests are registered; continuing only because WINDOWS_ALLOW_NO_CTEST_TESTS=1.'
}

$installerName = "FriedasBirdview-$version-Setup-x64.exe"
$installer = Join-Path $outputDirectory $installerName
if (-not (Test-Path -LiteralPath $installer)) {
    throw "Expected installer was not created: $installer"
}

$checksumFile = "$installer.sha256"
$checksum = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText(
    $checksumFile,
    "$checksum *$installerName`n",
    [System.Text.Encoding]::ASCII
)

Write-Output "Installer: $installer"
Write-Output "Checksum: $checksumFile"
POWERSHELL

remote_script="$remote_settings"$'\n'"try {"$'\n'"$remote_job"$'\n'"} catch {"$'\n'"    [Console]::Error.WriteLine(\$_.Exception.Message)"$'\n'"    exit 1"$'\n'"}"
remote_payload=$(printf '%s' "$remote_script" | gzip -c | base64 | tr -d '\n')
remote_launcher="\$b=[Convert]::FromBase64String('$remote_payload');\$m=[IO.MemoryStream]::new(\$b);\$g=[IO.Compression.GzipStream]::new(\$m,[IO.Compression.CompressionMode]::Decompress);\$r=[IO.StreamReader]::new(\$g,[Text.Encoding]::UTF8);&([ScriptBlock]::Create(\$r.ReadToEnd()))"
(( ${#remote_launcher} < 7000 )) || die 'The generated remote job is too long for a Windows command line.'
remote_command="powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"$remote_launcher\""

printf '%s\n' 'Building and testing on the Windows VM...'
ssh "${ssh_options[@]}" "$WINDOWS_SSH_TARGET" "$remote_command"

version=${release_tag#v}
installer_name="FriedasBirdview-$version-Setup-x64.exe"
remote_dist=${WINDOWS_REPOSITORY_DIR%/}
remote_dist=${remote_dist%\\}
remote_dist="$remote_dist/dist"
remote_installer="$remote_dist/$installer_name"
remote_checksum="$remote_installer.sha256"

temporary_download=$(mktemp -d "${TMPDIR:-/tmp}/friedasbirdview-windows.XXXXXX")
cleanup() {
    [[ -n ${temporary_download:-} && -d $temporary_download ]] && rm -rf -- "$temporary_download"
}
trap cleanup EXIT

printf '%s\n' 'Downloading release assets from the Windows VM...'
scp "${scp_options[@]}" "${WINDOWS_SSH_TARGET}:\"${remote_installer}\"" "$temporary_download/$installer_name"
scp "${scp_options[@]}" "${WINDOWS_SSH_TARGET}:\"${remote_checksum}\"" "$temporary_download/$installer_name.sha256"

expected_checksum=$(awk 'NR == 1 { print $1; exit }' "$temporary_download/$installer_name.sha256")
actual_checksum=$(shasum -a 256 "$temporary_download/$installer_name" | awk '{ print $1 }')
[[ $expected_checksum =~ ^[0-9A-Fa-f]{64}$ ]] || die 'Downloaded SHA-256 manifest is malformed.'
grep -Fqx -- "$expected_checksum *$installer_name" "$temporary_download/$installer_name.sha256" ||
    die 'Downloaded SHA-256 manifest does not describe the downloaded installer.'
[[ $expected_checksum == "$actual_checksum" ]] || die 'Local SHA-256 verification of the downloaded installer failed.'

asset_directory="$script_dir/dist/windows/$release_tag"
[[ ! -e $asset_directory ]] || die "Refusing to overwrite existing local assets: $asset_directory"
mkdir -p -- "$(dirname -- "$asset_directory")"
mv -- "$temporary_download" "$asset_directory"
temporary_download=
printf 'Verified local assets: %s\n' "$asset_directory"

if ! $publish; then
    printf '%s\n' 'Not uploaded. Re-run with --publish to attach these verified assets to the existing GitHub Release.'
    exit 0
fi

command -v gh >/dev/null 2>&1 || die 'GitHub CLI (gh) is required for --publish.'
gh auth status --hostname github.com >/dev/null
gh release view "$release_tag" --repo "$GITHUB_REPOSITORY" >/dev/null ||
    die "GitHub Release $release_tag does not exist in $GITHUB_REPOSITORY. Create it (normally by pushing the tag) before uploading."

gh release upload "$release_tag" \
    "$asset_directory/$installer_name" \
    "$asset_directory/$installer_name.sha256" \
    --repo "$GITHUB_REPOSITORY" \
    --clobber
printf 'Uploaded verified Windows installer assets to %s release %s.\n' "$GITHUB_REPOSITORY" "$release_tag"
