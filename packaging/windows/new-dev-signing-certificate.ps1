[CmdletBinding()]
param(
    [string]$Subject = 'CN=FriedasBirdview Development',
    [ValidateRange(1, 365)]
    [int]$ValidForDays = 90,
    [switch]$TrustForCurrentUser,
    [string]$ExportPublicCertificatePath
)

$ErrorActionPreference = 'Stop'

$rootSubject = "$Subject Root"
$expiresOn = (Get-Date).AddDays($ValidForDays)

$rootCertificate = Get-ChildItem -Path Cert:\CurrentUser\My |
    Where-Object {
        $_.Subject -eq $rootSubject -and $_.Issuer -eq $rootSubject -and $_.HasPrivateKey -and
        $_.NotAfter -gt (Get-Date).AddDays(1)
    } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if (-not $rootCertificate) {
    $rootCertificate = New-SelfSignedCertificate `
        -Type Custom `
        -Subject $rootSubject `
        -FriendlyName 'FriedasBirdview Development Root' `
        -CertStoreLocation Cert:\CurrentUser\My `
        -KeyAlgorithm RSA `
        -KeyLength 3072 `
        -KeyUsage CertSign, CRLSign, DigitalSignature `
        -HashAlgorithm SHA256 `
        -KeyExportPolicy NonExportable `
        -TextExtension @('2.5.29.19={critical}{text}ca=true&pathlength=0') `
        -NotAfter $expiresOn
}

$certificate = Get-ChildItem -Path Cert:\CurrentUser\My |
    Where-Object {
        $_.Subject -eq $Subject -and $_.Issuer -eq $rootSubject -and $_.HasPrivateKey -and
        $_.NotAfter -gt (Get-Date).AddDays(1)
    } |
    Sort-Object NotAfter -Descending |
    Select-Object -First 1

if (-not $certificate) {
    $certificate = New-SelfSignedCertificate `
        -Type CodeSigningCert `
        -Subject $Subject `
        -FriendlyName 'FriedasBirdview Development Signing' `
        -Signer $rootCertificate `
        -CertStoreLocation Cert:\CurrentUser\My `
        -KeyAlgorithm RSA `
        -KeyLength 3072 `
        -HashAlgorithm SHA256 `
        -KeyExportPolicy NonExportable `
        -NotAfter $expiresOn
}

$temporaryCertificate = Join-Path $env:TEMP ("friedasbirdview-dev-root-" + $rootCertificate.Thumbprint + '.cer')
try {
    Export-Certificate -Cert $rootCertificate -FilePath $temporaryCertificate -Force | Out-Null

    if (-not [string]::IsNullOrWhiteSpace($ExportPublicCertificatePath)) {
        $exportPath = [System.IO.Path]::GetFullPath($ExportPublicCertificatePath)
        $exportDirectory = Split-Path -Parent $exportPath
        if ($exportDirectory) {
            New-Item -ItemType Directory -Path $exportDirectory -Force | Out-Null
        }
        Copy-Item -LiteralPath $temporaryCertificate -Destination $exportPath -Force
        Write-Output "Public development root certificate exported: $exportPath"
    }

    if ($TrustForCurrentUser) {
        & certutil.exe -user -f -addstore Root $temporaryCertificate
        if ($LASTEXITCODE -ne 0) {
            $manualImport = if ($exportPath) {
                "Import '$exportPath' in certmgr.msc under Certificates - Current User > Trusted Root Certification Authorities."
            } else {
                'Rerun with -ExportPublicCertificatePath, then import the exported root in certmgr.msc under Certificates - Current User > Trusted Root Certification Authorities.'
            }
            throw "Windows blocked the non-interactive current-user root-store update. $manualImport"
        }
    }
} finally {
    Remove-Item -LiteralPath $temporaryCertificate -Force -ErrorAction SilentlyContinue
}

Write-Output "Development signing certificate thumbprint: $($certificate.Thumbprint)"
Write-Output 'The private keys are non-exportable. This local development chain is never suitable for a public release.'
