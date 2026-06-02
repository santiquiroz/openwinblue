# sign-driver.ps1 — Driver attestation signing via Microsoft Hardware Dev Center.
# This script is called by .github/workflows/release.yml on tagged releases.
# Full implementation added in Phase 2 when the driver is functional.

param(
    [Parameter(Mandatory=$true)] [string]$InfPath,
    [Parameter(Mandatory=$true)] [string]$SysPath,
    [Parameter(Mandatory=$true)] [string]$OutDir
)

Write-Host "sign-driver.ps1: stub — attestation signing not yet implemented."
Write-Host "  INF: $InfPath"
Write-Host "  SYS: $SysPath"
Write-Host "  Out: $OutDir"

# Phase 2 will add:
# 1. Zip the driver package
# 2. Submit to Microsoft Partner Center via REST API
# 3. Poll for signing completion
# 4. Download signed .cat + .sys
exit 0
