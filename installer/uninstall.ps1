#Requires -RunAsAdministrator
param([switch]$RemoveFiles, [switch]$RemoveWordFrequency)
$ErrorActionPreference = 'Stop'
if (-not [Environment]::Is64BitProcess) { throw 'Run this script from 64-bit PowerShell.' }
if (Get-Process EShuRu -ErrorAction SilentlyContinue) { throw 'Close trial windows, applications using EShuRu, and the broker before uninstalling. Connected clients can restart the broker.' }
$targetDir = [System.IO.Path]::GetFullPath((Join-Path $env:ProgramFiles 'EShuRu'))
$expectedDir = [System.IO.Path]::GetFullPath((Join-Path $env:ProgramFiles 'EShuRu'))
if ($targetDir -ne $expectedDir -or [System.IO.Path]::GetFileName($targetDir) -ne 'EShuRu') { throw 'Unexpected uninstall path' }
if ((Test-Path -LiteralPath $targetDir) -and ((Get-Item -LiteralPath $targetDir).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Refusing to operate on a junction or symlink.' }
$bridgeName = if (Test-Path -LiteralPath (Join-Path $targetDir 'EShuRuTip-0.2.5.2.dll')) { 'EShuRuTip-0.2.5.2.dll' } elseif (Test-Path -LiteralPath (Join-Path $targetDir 'EShuRuTip-0.2.5.1.dll')) { 'EShuRuTip-0.2.5.1.dll' } elseif (Test-Path -LiteralPath (Join-Path $targetDir 'EShuRuTip-0.2.5.dll')) { 'EShuRuTip-0.2.5.dll' } else { 'EShuRuTip.dll' }
$dllPath = Join-Path $targetDir $bridgeName
$dll32Path = Join-Path $targetDir ('x86\' + $bridgeName)
if (Test-Path -LiteralPath $dll32Path) {
  $result32 = Start-Process -FilePath (Join-Path $env:WINDIR 'SysWOW64\regsvr32.exe') -ArgumentList @('/u','/s',('"' + $dll32Path + '"')) -Wait -PassThru -WindowStyle Hidden
  if ($result32.ExitCode -ne 0) { throw '32-bit input method unregister failed. Files were kept.' }
}
if (Test-Path -LiteralPath $dllPath) {
  $result = Start-Process -FilePath (Join-Path $env:WINDIR 'System32\regsvr32.exe') -ArgumentList @('/u','/s',('"' + $dllPath + '"')) -Wait -PassThru -WindowStyle Hidden
  if ($result.ExitCode -ne 0) { throw 'Input method unregister failed. Files were kept.' }
}
Write-Output 'Input profile unregistered.'
$maintenance = Join-Path $targetDir 'EShuRuMaintenance.exe'
if (Test-Path -LiteralPath $maintenance) {
    $removeInput = Start-Process -FilePath $maintenance -ArgumentList '--unregister-input' -Wait -PassThru -WindowStyle Hidden
    if ($removeInput.ExitCode -ne 0) {
        Write-Warning ('The keyboard list entry could not be removed. Remove EShuRu manually in Settings > Time & language > Language & region > Chinese > Language options > Keyboards. If you uninstalled from a different administrator account, sign in as the target user and run: ' + $maintenance + ' --unregister-input')
    } else {
        Write-Output 'EShuRu was removed from the current user keyboard list.'
    }
} else {
    Write-Warning 'EShuRuMaintenance.exe is missing; remove EShuRu manually from the Chinese keyboard list if it remains.'
}
if ($RemoveWordFrequency) {
    $frequencyDir = [IO.Path]::GetFullPath((Join-Path $env:LOCALAPPDATA 'EShuRu'))
    if ((Test-Path -LiteralPath $frequencyDir) -and ((Get-Item -LiteralPath $frequencyDir).Attributes -band [IO.FileAttributes]::ReparsePoint)) { throw 'Frequency directory must not be a junction or symlink.' }
    foreach ($name in @('word-frequency.dat', 'word-frequency.dat.tmp', 'word-frequency.dat.bak', 'word-frequency.dat.bak.tmp')) {
        $frequencyPath = [IO.Path]::GetFullPath((Join-Path $frequencyDir $name))
        if ([IO.Path]::GetDirectoryName($frequencyPath) -ne $frequencyDir) { throw 'Unexpected frequency path.' }
        if (Test-Path -LiteralPath $frequencyPath) { Remove-Item -LiteralPath $frequencyPath -Force }
    }
    Write-Output 'Personal word frequency removed for the Windows account running this script. Other settings and API credentials retained.'
}
if ($RemoveFiles -and (Test-Path -LiteralPath $targetDir)) {
    try { Remove-Item -LiteralPath $targetDir -Recurse -Force }
    catch { throw ('Some files are in use. Close the broker and applications that loaded the input method, or sign out, then retry. Directory: ' + $targetDir) }
} else {
    Write-Output ('Installation files retained at: ' + $targetDir + '. Use -RemoveFiles after closing applications to remove them.')
}
Write-Output 'Your EShuRu appearance settings and API credential are retained. Before uninstalling, use Delete Key in settings to remove EShuRu/DeepSeek from Windows Credential Manager.'
