#!/usr/bin/env bash
# Build either a pushed Windows release candidate or a tagged release on a
# configured VM. Configuration stays in the ignored local file.
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
       ./build-and-install-windows.sh --candidate GIT-REF [--config FILE]

Fetches the requested tag on the configured Windows VM, creates or resets an
isolated release worktree, then builds and tests it. --candidate instead
resolves a pushed Git ref to an exact commit and builds it in an isolated
candidate worktree. Candidate assets are stored locally only and cannot be
published by this script.

The script downloads the Setup installer and its SHA-256 manifest and verifies
the download locally. --publish is valid only for a release tag and additionally
attaches both verified files to the existing GitHub Release. The script never
creates a release, tag, or commit, and never alters the configured source
checkout.
EOF
}

publish=false
config_file=$default_config
release_tag=
candidate_ref=
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
        --candidate)
            (($# >= 2)) || die '--candidate needs a pushed Git ref.'
            [[ -z $release_tag && -z $candidate_ref ]] || die 'Choose either a release tag or --candidate, not both.'
            candidate_ref=$2
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
            [[ -z $release_tag && -z $candidate_ref ]] || die 'Choose either a release tag or --candidate, not both.'
            release_tag=$1
            ;;
    esac
    shift
done

[[ -n $release_tag || -n $candidate_ref ]] || {
    usage >&2
    exit 2
}
if [[ -n $release_tag ]]; then
    [[ $release_tag =~ ^v[0-9]+[.][0-9]+[.][0-9]+([.-][0-9A-Za-z][0-9A-Za-z.-]*)?$ ]] ||
        die "Release tag must look like v1.2.3, not '$release_tag'."
    build_mode=release
    build_reference=$release_tag
else
    [[ -n $candidate_ref ]] || die '--candidate needs a pushed Git ref.'
    $publish && die '--publish is only valid with a release tag, never with --candidate.'
    build_mode=candidate
    build_reference=$candidate_ref
fi
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

for name in WINDOWS_SSH_TARGET WINDOWS_REPOSITORY_DIR WINDOWS_QT_ROOT WINDOWS_VCPKG_ROOT WINDOWS_VSDEVCMD GITHUB_REPOSITORY WINDOWS_SIGNING_CERT_THUMBPRINT WINDOWS_SIGNING_TIMESTAMP_URL build_reference; do
    reject_line_breaks "$name" "${!name}"
done

if [[ -n $WINDOWS_SSH_IDENTITY_FILE ]]; then
    [[ -r $WINDOWS_SSH_IDENTITY_FILE ]] || die "WINDOWS_SSH_IDENTITY_FILE is not readable: $WINDOWS_SSH_IDENTITY_FILE"
fi

version=
installer_name=
asset_directory=
if [[ $build_mode == release ]]; then
    version=${release_tag#v}
    installer_name="FriedasBirdview-$version-Setup-x64.exe"
    asset_directory="$script_dir/dist/windows/$release_tag"
fi

verify_local_assets() {
    local checksum_file=$asset_directory/$installer_name.sha256
    local expected_checksum actual_checksum

    [[ -f $asset_directory/$installer_name ]] || die "Verified installer is missing: $asset_directory/$installer_name"
    [[ -f $checksum_file ]] || die "Verified SHA-256 manifest is missing: $checksum_file"
    expected_checksum=$(awk 'NR == 1 { print $1; exit }' "$checksum_file")
    actual_checksum=$(shasum -a 256 "$asset_directory/$installer_name" | awk '{ print $1 }')
    [[ $expected_checksum =~ ^[0-9A-Fa-f]{64}$ ]] || die 'Local SHA-256 manifest is malformed.'
    grep -Fqx -- "$expected_checksum *$installer_name" "$checksum_file" ||
        die 'Local SHA-256 manifest does not describe the installer.'
    [[ $expected_checksum == "$actual_checksum" ]] || die 'Local SHA-256 verification of the installer failed.'
}

publish_assets() {
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
}

if [[ $build_mode == release && -e $asset_directory ]]; then
    [[ -d $asset_directory ]] || die "Local asset path is not a directory: $asset_directory"
    if $publish; then
        verify_local_assets
        publish_assets
        exit 0
    fi
    die "Verified local assets already exist: $asset_directory (use --publish to upload them)."
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
    "\$BuildMode = $(powershell_literal "$build_mode")" \
    "\$BuildReference = $(powershell_literal "$build_reference")" \
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
    throw "Windows source checkout is missing: $RepositoryDir"
}

$sourceTopLevelOutput = & git.exe -C $RepositoryDir rev-parse --show-toplevel
if ($LASTEXITCODE -ne 0) {
    throw "Windows source checkout is not a Git worktree: $RepositoryDir"
}
$sourceTopLevel = [System.IO.Path]::GetFullPath(($sourceTopLevelOutput -join "`n").Trim()).TrimEnd([char[]]@('\', '/'))
$repositoryRoot = [System.IO.Path]::GetFullPath($RepositoryDir).TrimEnd([char[]]@('\', '/'))
if (-not $sourceTopLevel.Equals($repositoryRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "WINDOWS_REPOSITORY_DIR must name the source checkout root, not a subdirectory: $RepositoryDir"
}

& git.exe -C $RepositoryDir fetch --prune --tags --force origin
if ($LASTEXITCODE -ne 0) {
    throw 'Fetching the candidate/release refs from origin failed.'
}

$targetCommitOutput = & git.exe -C $RepositoryDir rev-parse ($BuildReference + '^{commit}')
if ($LASTEXITCODE -ne 0) {
    throw "The source checkout does not know build reference $BuildReference after fetching origin."
}
$targetCommit = ($targetCommitOutput -join "`n").Trim()
if ($targetCommit -notmatch '^[0-9a-f]{40}$') {
    throw "Build reference $BuildReference did not resolve to a full commit ID."
}

$shortTargetCommit = $targetCommit.Substring(0, 12)
$worktreeSuffix = switch ($BuildMode) {
    'release' {
        "release-$BuildReference"
        break
    }
    'candidate' {
        "candidate-$shortTargetCommit"
        break
    }
    default {
        throw "Unsupported build mode: $BuildMode"
    }
}
$targetDescription = if ($BuildMode -eq 'release') { "release tag $BuildReference" } else { "candidate $shortTargetCommit" }

$sourceLeaf = Split-Path -Path $RepositoryDir -Leaf
$managedWorktreeDir = Join-Path (Split-Path -Path $RepositoryDir -Parent) "$sourceLeaf-$worktreeSuffix"
if (Test-Path -LiteralPath $managedWorktreeDir) {
    $sourceCommonGitDirOutput = & git.exe -C $RepositoryDir rev-parse --path-format=absolute --git-common-dir
    if ($LASTEXITCODE -ne 0) {
        throw 'Could not determine the source checkout Git directory.'
    }
    $worktreeCommonGitDirOutput = & git.exe -C $managedWorktreeDir rev-parse --path-format=absolute --git-common-dir
    if ($LASTEXITCODE -ne 0) {
        throw "Existing managed build directory is not a Git worktree: $managedWorktreeDir"
    }
    $sourceCommonGitDir = ($sourceCommonGitDirOutput -join "`n").Trim().TrimEnd([char[]]@('\', '/'))
    $worktreeCommonGitDir = ($worktreeCommonGitDirOutput -join "`n").Trim().TrimEnd([char[]]@('\', '/'))
    if (-not $sourceCommonGitDir.Equals($worktreeCommonGitDir, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to clean a build directory owned by another repository: $managedWorktreeDir"
    }

    Write-Output "Resetting script-managed $BuildMode worktree: $managedWorktreeDir"
    & git.exe -C $managedWorktreeDir reset --hard --quiet $targetCommit
    if ($LASTEXITCODE -ne 0) {
        throw "Could not reset the $BuildMode worktree: $managedWorktreeDir"
    }
    & git.exe -C $managedWorktreeDir clean -ffdx
    if ($LASTEXITCODE -ne 0) {
        throw "Could not clean the $BuildMode worktree: $managedWorktreeDir"
    }
} else {
    Write-Output "Creating clean $BuildMode worktree: $managedWorktreeDir"
    & git.exe -C $RepositoryDir worktree add --detach $managedWorktreeDir $targetCommit
    if ($LASTEXITCODE -ne 0) {
        throw "Could not create the $BuildMode worktree: $managedWorktreeDir"
    }
}

$headCommitOutput = & git.exe -C $managedWorktreeDir rev-parse HEAD
if ($LASTEXITCODE -ne 0) {
    throw "Could not determine the $BuildMode worktree commit."
}
$headCommit = ($headCommitOutput -join "`n").Trim()
if ($headCommit -ne $targetCommit) {
    throw "The $BuildMode worktree is not at $targetDescription. It is at $headCommit."
}

$cmakeLists = Get-Content -LiteralPath (Join-Path $managedWorktreeDir 'CMakeLists.txt') -Raw
$versionMatch = [regex]::Match($cmakeLists, 'project\(FriedasBirdview VERSION ([0-9][^ )]*)')
if (-not $versionMatch.Success) {
    throw 'Could not determine the CMake project version.'
}
$version = $versionMatch.Groups[1].Value
if ($BuildMode -eq 'release' -and $BuildReference -ne "v$version") {
    throw "Release tag $BuildReference does not match CMake project version $version."
}

Import-DeveloperEnvironment
$env:QT_ROOT = $QtRoot

$buildDirectory = Join-Path $managedWorktreeDir 'build-win-package'
$outputDirectory = Join-Path $managedWorktreeDir 'dist'
$packageScript = Join-Path $managedWorktreeDir 'packaging\windows\package.ps1'
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
    throw 'No CTest tests are registered; refusing to package. Add tests or explicitly set WINDOWS_ALLOW_NO_CTEST_TESTS=1 for a temporary exception.'
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
Write-Output "FRIEDASBIRDVIEW_BUILD_VERSION=$version"
Write-Output "FRIEDASBIRDVIEW_BUILD_COMMIT=$headCommit"
POWERSHELL

remote_script="$remote_settings"$'\n'"try {"$'\n'"$remote_job"$'\n'"} catch {"$'\n'"    [Console]::Error.WriteLine(\$_.Exception.Message)"$'\n'"    exit 1"$'\n'"}"
remote_payload=$(printf '%s' "$remote_script" | gzip -c | base64 | tr -d '\n')
remote_launcher="\$b=[Convert]::FromBase64String('$remote_payload');\$m=[IO.MemoryStream]::new(\$b);\$g=[IO.Compression.GzipStream]::new(\$m,[IO.Compression.CompressionMode]::Decompress);\$r=[IO.StreamReader]::new(\$g,[Text.Encoding]::UTF8);&([ScriptBlock]::Create(\$r.ReadToEnd()))"
(( ${#remote_launcher} < 7000 )) || die 'The generated remote job is too long for a Windows command line.'
remote_command="powershell.exe -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command \"$remote_launcher\""

printf '%s\n' 'Building and testing on the Windows VM...'
remote_output=$(ssh "${ssh_options[@]}" "$WINDOWS_SSH_TARGET" "$remote_command")
printf '%s\n' "$remote_output"

remote_build_version=$(printf '%s\n' "$remote_output" | awk -F= '$1 == "FRIEDASBIRDVIEW_BUILD_VERSION" { value = $2 } END { print value }' | tr -d '\r')
remote_build_commit=$(printf '%s\n' "$remote_output" | awk -F= '$1 == "FRIEDASBIRDVIEW_BUILD_COMMIT" { value = $2 } END { print value }' | tr -d '\r')
[[ $remote_build_version =~ ^[0-9]+[.][0-9]+[.][0-9]+([.-][0-9A-Za-z][0-9A-Za-z.-]*)?$ ]] ||
    die 'The Windows VM did not report a valid CMake project version.'
[[ $remote_build_commit =~ ^[0-9a-f]{40}$ ]] ||
    die 'The Windows VM did not report a valid candidate/release commit.'

if [[ $build_mode == release ]]; then
    [[ $remote_build_version == "$version" ]] ||
        die "The Windows VM built version $remote_build_version, expected $version from $release_tag."
else
    version=$remote_build_version
    installer_name="FriedasBirdview-$version-Setup-x64.exe"
    candidate_commit=${remote_build_commit:0:12}
    asset_directory="$script_dir/dist/windows/candidate-$candidate_commit"
    if [[ -e $asset_directory ]]; then
        [[ -d $asset_directory ]] || die "Local candidate asset path is not a directory: $asset_directory"
        die "Verified candidate assets already exist: $asset_directory"
    fi
fi

remote_worktree=${WINDOWS_REPOSITORY_DIR//\\//}
remote_worktree=${remote_worktree%/}
if [[ $build_mode == release ]]; then
    remote_worktree="$remote_worktree-release-$release_tag"
else
    remote_worktree="$remote_worktree-candidate-${remote_build_commit:0:12}"
fi
remote_dist="$remote_worktree/dist"
remote_installer="$remote_dist/$installer_name"
remote_checksum="$remote_installer.sha256"

temporary_download=$(mktemp -d "${TMPDIR:-/tmp}/friedasbirdview-windows.XXXXXX")
cleanup() {
    if [[ -n ${temporary_download:-} && -d $temporary_download ]]; then
        rm -rf -- "$temporary_download"
    fi
}
trap cleanup EXIT

printf '%s\n' 'Downloading release assets from the Windows VM...'
scp "${scp_options[@]}" "${WINDOWS_SSH_TARGET}:${remote_installer}" "$temporary_download/$installer_name"
scp "${scp_options[@]}" "${WINDOWS_SSH_TARGET}:${remote_checksum}" "$temporary_download/$installer_name.sha256"

expected_checksum=$(awk 'NR == 1 { print $1; exit }' "$temporary_download/$installer_name.sha256")
actual_checksum=$(shasum -a 256 "$temporary_download/$installer_name" | awk '{ print $1 }')
[[ $expected_checksum =~ ^[0-9A-Fa-f]{64}$ ]] || die 'Downloaded SHA-256 manifest is malformed.'
grep -Fqx -- "$expected_checksum *$installer_name" "$temporary_download/$installer_name.sha256" ||
    die 'Downloaded SHA-256 manifest does not describe the downloaded installer.'
[[ $expected_checksum == "$actual_checksum" ]] || die 'Local SHA-256 verification of the downloaded installer failed.'

mkdir -p -- "$(dirname -- "$asset_directory")"
mv -- "$temporary_download" "$asset_directory"
temporary_download=
printf 'Verified local assets: %s\n' "$asset_directory"

if [[ $build_mode == candidate ]]; then
    printf '%s\n' 'Candidate assets are local-only and cannot be uploaded by this script. Smoke-test this installer before tagging the reported commit.'
    exit 0
fi

if ! $publish; then
    printf '%s\n' 'Not uploaded. Re-run with --publish to attach these verified assets to the existing GitHub Release.'
    exit 0
fi

publish_assets
