# Signs the Windows release with a Certum cloud certificate (SimplySign) and publishes the release.
# Guide: installer/certum/README.md. Runs on the maintainer's PC, in Windows PowerShell 5.1 or later.
#
# With the repository variable WINDOWS_SIGNING=certum, a tag vX.Y.Z makes .github/workflows/release.yml
# create a draft release without the Windows installer and upload the unsigned program as the
# artifact windows-stage. This script:
#   1. downloads windows-stage from that run;
#   2. signs Mentis.exe and MentisMathText.dll;
#   3. builds the installer with Inno Setup from the tag's installer/mentis.iss, signing the setup
#      and its uninstaller;
#   4. adds the installer and its checksum (SHA256SUMS) to the draft release;
#   5. with -Publish, publishes the release (the app's update check then sees it).
#
# Before: SimplySign Desktop connected (code from the SimplySign phone app), gh logged in to GitHub.
#
#   .\tools\sign-windows-certum.ps1 -Version 1.2.0
#   .\tools\sign-windows-certum.ps1 -Version 1.2.0 -Publish

param(
    [Parameter(Mandatory = $true)][string]$Version,
    # A part of the certificate's subject: Certum's open source certificates start with this text.
    [string]$Certificate = 'Open Source Developer',
    [string]$Repo = 'simozock/Mentis',
    [switch]$Publish
)

$ErrorActionPreference = 'Stop'
# Certum's timestamp server: with a timestamp, signatures stay valid after the certificate expires.
$timestamp = 'http://time.certum.pl'
$tag = "v$Version"
$installerName = "Mentis-$Version-setup-x64.exe"

function Invoke-Native {
    param([string]$File, [string[]]$Arguments)
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$(Split-Path $File -Leaf) failed (exit code $LASTEXITCODE)." }
}

function Find-Tool {
    param([string]$Name, [string[]]$Candidates, [string]$Hint)
    foreach ($candidate in $Candidates) {
        $found = Get-Item $candidate -ErrorAction SilentlyContinue | Sort-Object FullName -Descending | Select-Object -First 1
        if ($null -ne $found) { return $found.FullName }
    }
    throw "$Name not found. $Hint"
}

if ($Version -notmatch '^\d+\.\d+\.\d+$') { throw "The version must be X.Y.Z, not '$Version'." }

$gh = (Get-Command gh -ErrorAction SilentlyContinue).Source
if ($null -eq $gh) { throw 'gh (GitHub CLI) not found: winget install GitHub.cli, then gh auth login.' }
$signtool = Find-Tool 'signtool.exe' @("${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\signtool.exe") `
    'Install the Windows SDK signing tools: winget install Microsoft.WindowsSDK.10.0.26100'
$iscc = Find-Tool 'Inno Setup 6' @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") `
    'Install it: winget install JRSoftware.InnoSetup'

# SimplySign Desktop puts the certificate in the user's store only while it is connected.
$cert = Get-ChildItem Cert:\CurrentUser\My -CodeSigningCert | Where-Object { $_.Subject -like "*$Certificate*" }
if ($null -eq $cert) {
    throw "No code signing certificate with '$Certificate' in its subject: open SimplySign Desktop and connect with the code from the SimplySign app."
}
Write-Host "Certificate: $(@($cert)[0].Subject), valid until $(@($cert)[0].NotAfter)"

$release = Invoke-Native $gh @('release', 'view', $tag, '--repo', $Repo, '--json', 'isDraft') | ConvertFrom-Json
if (-not $release.isDraft) { throw "The release $tag is already published: this script only completes a draft." }

$runs = Invoke-Native $gh @('run', 'list', '--repo', $Repo, '--workflow', 'release.yml', '--branch', $tag,
    '--event', 'push', '--status', 'success', '--limit', '1', '--json', 'databaseId') | ConvertFrom-Json
if (@($runs).Count -eq 0) { throw "No successful Release run for $tag." }
$runId = [string]@($runs)[0].databaseId

$work = Join-Path $env:TEMP "mentis-sign-$Version"
if (Test-Path $work) { Remove-Item -Recurse -Force $work }
New-Item -ItemType Directory -Path $work | Out-Null
Write-Host "Downloading the program of run $runId into $work"
Invoke-Native $gh @('run', 'download', $runId, '--repo', $Repo, '--name', 'windows-stage', '--dir', $work)

$signArgs = @('sign', '/fd', 'sha256', '/tr', $timestamp, '/td', 'sha256', '/n', $Certificate, '/d', 'Mentis')
$program = @("$work\stage\bin\Mentis.exe", "$work\stage\bin\MentisMathText.dll")
foreach ($file in $program) { if (-not (Test-Path $file)) { throw "Missing in windows-stage: $file" } }
Write-Host 'Signing the program'
Invoke-Native $signtool ($signArgs + $program)
Invoke-Native $signtool (@('verify', '/pa') + $program)

# Inno Setup's sign tool: $q is a quote, $f the quoted file name (https://jrsoftware.org/ishelp/).
$signCommand = '$q' + $signtool + '$q ' + (($signArgs | ForEach-Object { if ($_ -match ' ') { '$q' + $_ + '$q' } else { $_ } }) -join ' ') + ' $f'
Write-Host 'Building and signing the installer'
Invoke-Native $iscc @("/DAppVersion=$Version", "/DStageDir=$work\stage", '/DSignTool=mentis', "/Smentis=$signCommand",
    "$work\installer\mentis.iss")
$installer = "$work\dist\$installerName"
Invoke-Native $signtool @('verify', '/pa', $installer)
if ((Get-AuthenticodeSignature $installer).Status -ne 'Valid') { throw "Windows does not accept the signature of $installer." }

# SHA256SUMS as sha256sum writes it (lowercase hash, two spaces, LF), which the update check reads.
Invoke-Native $gh @('release', 'download', $tag, '--repo', $Repo, '--pattern', 'SHA256SUMS', '--dir', "$work\dist", '--clobber')
$sums = "$work\dist\SHA256SUMS"
$hash = (Get-FileHash -Algorithm SHA256 $installer).Hash.ToLowerInvariant()
$lines = @("$hash  $installerName") + @(Get-Content $sums | Where-Object { $_ -and $_ -notmatch '-setup-x64\.exe$' })
[System.IO.File]::WriteAllText($sums, (($lines -join "`n") + "`n"), (New-Object System.Text.UTF8Encoding $false))
Get-Content $sums

Write-Host "Adding the installer and SHA256SUMS to the draft $tag"
Invoke-Native $gh @('release', 'upload', $tag, '--repo', $Repo, '--clobber', $installer, $sums)

if ($Publish) {
    Invoke-Native $gh @('release', 'edit', $tag, '--repo', $Repo, '--draft=false', '--latest')
    Write-Host "Published: https://github.com/$Repo/releases/tag/$tag"
} else {
    Write-Host "Draft ready: https://github.com/$Repo/releases (check it, then run again with -Publish or publish it there)."
}
