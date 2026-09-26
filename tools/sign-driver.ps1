# sign-driver.ps1 — Test-sign the OpenWinBlue driver and assemble a deployable package.
#
# Produces, in $OutDir: owb_a2dp.sys (signed), owb_a2dp.cat (signed), owb_a2dp.inf,
# and owb_test.cer (the public cert the installer imports into Root/TrustedPublisher).
#
# This is TEST signing (self-signed) — it requires the target machine to have Test
# Signing Mode enabled. Production/attestation signing via Microsoft Partner Center
# is a separate flow (needs a Dev Center account + secrets) and is out of scope here.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $InfPath,
    [Parameter(Mandatory = $true)] [string] $SysPath,
    [Parameter(Mandatory = $true)] [string] $OutDir,
    [string] $CertSubject = 'CN=OpenWinBlue Test Certificate',
    # Raiz bin del WDK/EWDK (ej: F:\Program Files\Windows Kits\10\bin) para
    # maquinas sin WDK instalado que compilan con el EWDK montado.
    [string] $WdkBinRoot = ''
)

$ErrorActionPreference = 'Stop'

function Find-WdkTool([string]$name) {
    $roots = @(
        $script:WdkBinRoot,
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "${env:ProgramFiles}\Windows Kits\10\bin"
    ) | Where-Object { $_ }
    foreach ($arch in @('\\x64\\', '\\x86\\')) {   # inf2cat solo existe en x86
        foreach ($root in $roots) {
            if (-not (Test-Path $root)) { continue }
            $hit = Get-ChildItem -Path $root -Recurse -Filter $name -ErrorAction SilentlyContinue |
                   Where-Object { $_.FullName -match $arch } |
                   Sort-Object FullName -Descending | Select-Object -First 1
            if ($hit) { return $hit.FullName }
        }
    }
    return $null
}

New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

if (-not (Test-Path $SysPath)) { throw "Driver binary not found: $SysPath" }
if (-not (Test-Path $InfPath)) { throw "INF not found: $InfPath" }

Copy-Item $SysPath (Join-Path $OutDir 'owb_a2dp.sys') -Force
Copy-Item $InfPath (Join-Path $OutDir 'owb_a2dp.inf') -Force

$signtool = Find-WdkTool 'signtool.exe'
$inf2cat  = Find-WdkTool 'inf2cat.exe'
if (-not $signtool) { throw 'signtool.exe not found — install the Windows SDK/WDK.' }

Write-Host "signtool: $signtool"
Write-Host "inf2cat:  $inf2cat"

# 1. Self-signed code-signing certificate (reused if already present this session).
$cert = New-SelfSignedCertificate `
    -Type CodeSigningCert `
    -Subject $CertSubject `
    -CertStoreLocation 'Cert:\CurrentUser\My' `
    -KeyUsage DigitalSignature `
    -KeyExportPolicy Exportable `
    -NotAfter (Get-Date).AddYears(5)

$thumb = $cert.Thumbprint
Write-Host "Certificate thumbprint: $thumb"

# 2. Export the public cert for the installer to trust.
Export-Certificate -Cert $cert -FilePath (Join-Path $OutDir 'owb_test.cer') | Out-Null

# 3. Generate the security catalog from the INF (best-effort; needs WDK inf2cat).
if ($inf2cat) {
    & $inf2cat /driver:"$OutDir" /os:10_X64,Server10_X64 /verbose
    if ($LASTEXITCODE -ne 0) { Write-Warning "inf2cat returned $LASTEXITCODE" }
} else {
    Write-Warning 'inf2cat.exe not found — skipping .cat generation (installer will lack a catalog).'
}

# 4. Sign the .sys and .cat with SHA-256 + a timestamp.
$ts = 'http://timestamp.digicert.com'
$targets = @('owb_a2dp.sys', 'owb_a2dp.cat') |
           ForEach-Object { Join-Path $OutDir $_ } |
           Where-Object { Test-Path $_ }

foreach ($t in $targets) {
    & $signtool sign /fd SHA256 /sha1 $thumb /tr $ts /td SHA256 "$t"
    if ($LASTEXITCODE -ne 0) { throw "signtool failed on $t (exit $LASTEXITCODE)" }
    Write-Host "Signed: $t"
}

Write-Host "Test-signed driver package ready in: $OutDir"
