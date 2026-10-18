#Requires -RunAsAdministrator
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Run this script from 64-bit PowerShell.' }
$packageRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$sourceDir = if (Test-Path -LiteralPath (Join-Path $packageRoot 'EShuRu.exe')) { $packageRoot } else { Join-Path $packageRoot 'build\0.2.5\x64\bin' }
$targetDir = Join-Path $env:ProgramFiles 'EShuRu'
$bridgeName = 'EShuRuTip-0.2.5.2.dll'
$bridge64 = Join-Path $targetDir $bridgeName
$bridge32 = Join-Path $targetDir ('x86\' + $bridgeName)
if ([IO.Path]::GetFullPath($sourceDir) -eq [IO.Path]::GetFullPath($targetDir)) { throw 'Run installation from the extracted package or project folder, not the installed directory.' }
& (Join-Path $PSScriptRoot 'diagnose.ps1') -PackageRoot $packageRoot -RequirePackage
if (Get-Process EShuRu -ErrorAction SilentlyContinue) {
    throw 'Close trial windows and exit the EShuRu broker before installing. Also close applications using the installed input method. No processes were stopped.'
}
foreach ($relative in @('EShuRu.exe','EShuRuMaintenance.exe','EShuRuTip.dll','rime.dll','data\word-readings.idx','data\build\e_shuru.schema.yaml','x86\EShuRuTip.dll','licenses')) {
    if (-not (Test-Path -LiteralPath (Join-Path $sourceDir $relative))) { throw ('Missing package file: ' + $relative + '. Build the project first.') }
}
if ((Test-Path -LiteralPath $targetDir) -and ((Get-Item -LiteralPath $targetDir).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Installation target must not be a junction or symlink.' }
if (Test-Path -LiteralPath $targetDir) {
    # Verify existing binaries are not loaded before any overwrite. Never close the user's applications.
    foreach ($relative in @('EShuRu.exe','EShuRuMaintenance.exe','rime.dll',$bridgeName,('x86\' + $bridgeName))) {
        $binaryPath = Join-Path $targetDir $relative
        if (-not (Test-Path -LiteralPath $binaryPath)) { continue }
        try { $probe = [IO.File]::Open($binaryPath, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite, [IO.FileShare]::None); $probe.Dispose() }
        catch { throw ('A binary to replace is in use or not writable. Close its host application, or sign out, then retry: ' + $binaryPath) }
    }
    $backupDir = $targetDir + '.backup-' + [Guid]::NewGuid().ToString('N')
    Copy-Item -LiteralPath $targetDir -Destination $backupDir -Recurse
    Write-Output ('Previous installation preserved for manual recovery: ' + $backupDir)
}
New-Item -ItemType Directory -Path $targetDir -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceDir 'EShuRu.exe'),(Join-Path $sourceDir 'EShuRuMaintenance.exe'),(Join-Path $sourceDir 'rime.dll') -Destination $targetDir -Force
Copy-Item -LiteralPath (Join-Path $sourceDir 'EShuRuTip.dll') -Destination $bridge64 -Force
Copy-Item -LiteralPath (Join-Path $sourceDir 'data'),(Join-Path $sourceDir 'licenses') -Destination $targetDir -Recurse -Force
New-Item -ItemType Directory -Path (Join-Path $targetDir 'x86') -Force | Out-Null
Copy-Item -LiteralPath (Join-Path $sourceDir 'x86\EShuRuTip.dll') -Destination $bridge32 -Force
Copy-Item -LiteralPath $PSScriptRoot -Destination $targetDir -Recurse -Force
$registration = Start-Process -FilePath (Join-Path $env:WINDIR 'System32\regsvr32.exe') -ArgumentList @('/s', ('"' + $bridge64 + '"')) -Wait -PassThru -WindowStyle Hidden
if ($registration.ExitCode -ne 0) { throw ('Registration failed: ' + $registration.ExitCode) }
$registration32 = Start-Process -FilePath (Join-Path $env:WINDIR 'SysWOW64\regsvr32.exe') -ArgumentList @('/s', ('"' + $bridge32 + '"')) -Wait -PassThru -WindowStyle Hidden
if ($registration32.ExitCode -ne 0) {
    $rollback = Start-Process -FilePath (Join-Path $env:WINDIR 'System32\regsvr32.exe') -ArgumentList @('/u','/s',('"' + $bridge64 + '"')) -Wait -PassThru -WindowStyle Hidden
    throw ('32-bit registration failed: ' + $registration32.ExitCode + '; 64-bit unregister result: ' + $rollback.ExitCode + '. Files remain for repair.')
}
$maintenance = Join-Path $targetDir 'EShuRuMaintenance.exe'
$registerInput = Start-Process -FilePath $maintenance -ArgumentList '--register-input' -Wait -PassThru -WindowStyle Hidden
if ($registerInput.ExitCode -ne 0) {
    Write-Warning ('The input method is installed, but adding it to the current user keyboard list failed. ' +
                   'Run this as the target Windows user, then sign out and in if Win+Space still does not list it: ' +
                   $maintenance + ' --register-input')
} else {
    Write-Output 'EShuRu was added to the current user keyboard list.'
}
$profileKey = 'HKLM:\Software\Microsoft\CTF\TIP\{5DB324AE-9ED8-4E80-9BCE-8520DFFA3C93}\LanguageProfile\0x00000804\{6ADE2191-955D-4BB3-BB54-8B9EE941FA20}'
if (Test-Path -LiteralPath $profileKey) {
    Set-ItemProperty -LiteralPath $profileKey -Name IconFile -Value $bridge32 -ErrorAction Stop
    Set-ItemProperty -LiteralPath $profileKey -Name IconIndex -Value 0 -Type DWord -ErrorAction Stop
    Write-Output 'Language profile icon updated.'
} else {
    Write-Warning 'The TSF language profile key is missing; the keyboard list entry may show the old icon.'
}
Write-Output 'Installed. Use Win+Space to select the new input method; existing defaults are not changed.'
Write-Output ('Open settings: ' + (Join-Path $targetDir 'EShuRu.exe'))
Write-Output ('If you installed as a different administrator, sign in as the target user and run: ' + $maintenance + ' --register-input')
Write-Output 'Close any portable trial broker before using the installed input method. If it is missing from Win+Space, sign out and in, then check Chinese language keyboard settings.'
