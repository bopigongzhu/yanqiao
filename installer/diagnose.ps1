param([string]$PackageRoot = (Split-Path $PSScriptRoot -Parent), [switch]$RequirePackage)
$ErrorActionPreference = 'Stop'
$root = [IO.Path]::GetFullPath($PackageRoot)
$source = if (Test-Path -LiteralPath (Join-Path $root 'EShuRu.exe')) { $root } else { Join-Path $root 'build\0.2.5\x64\bin' }
$bridgeName = if (Test-Path -LiteralPath (Join-Path $source 'EShuRuTip-0.2.5.3.dll')) { 'EShuRuTip-0.2.5.3.dll' } elseif (Test-Path -LiteralPath (Join-Path $source 'EShuRuTip-0.2.5.2.dll')) { 'EShuRuTip-0.2.5.2.dll' } elseif (Test-Path -LiteralPath (Join-Path $source 'EShuRuTip-0.2.5.1.dll')) { 'EShuRuTip-0.2.5.1.dll' } elseif (Test-Path -LiteralPath (Join-Path $source 'EShuRuTip-0.2.5.dll')) { 'EShuRuTip-0.2.5.dll' } else { 'EShuRuTip.dll' }
$required = @('EShuRu.exe',$bridgeName,('x86\' + $bridgeName),'rime.dll','data\word-readings.idx','data\build\e_shuru.schema.yaml','licenses\THIRD_PARTY_NOTICES.md')
$missing = @($required | Where-Object { -not (Test-Path -LiteralPath (Join-Path $source $_) -PathType Leaf) })
$architecture = @{}
foreach ($relative in @('EShuRu.exe',$bridgeName,('x86\' + $bridgeName))) {
    $path = Join-Path $source $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
    $stream = [IO.File]::OpenRead($path)
    $reader = [IO.BinaryReader]::new($stream)
    try {
        if ($reader.ReadUInt16() -ne 0x5a4d) { throw "Invalid executable: $relative" }
        $stream.Position = 0x3c
        $offset = $reader.ReadInt32()
        if ($offset -lt 64 -or $offset -gt $stream.Length - 6) { throw "Invalid PE header: $relative" }
        $stream.Position = $offset
        if ($reader.ReadUInt32() -ne 0x4550) { throw "Invalid PE signature: $relative" }
        $machine = $reader.ReadUInt16()
        $architecture[$relative] = ('0x{0:X4}' -f $machine)
        $expected = if ($relative.StartsWith('x86\')) { 0x14c } else { 0x8664 }
        if ($machine -ne $expected) { throw "Wrong architecture: $relative" }
    } finally { $reader.Dispose() }
}
$registrations = @{}
foreach ($view in @([Microsoft.Win32.RegistryView]::Registry64, [Microsoft.Win32.RegistryView]::Registry32)) {
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::LocalMachine, $view)
    $key = $base.OpenSubKey('Software\Classes\CLSID\{5DB324AE-9ED8-4E80-9BCE-8520DFFA3C93}\InprocServer32')
    try { $registrations[$view.ToString()] = if ($key) { $key.GetValue('') } else { $null } }
    finally { if ($key) { $key.Dispose() }; $base.Dispose() }
}
$tip = '0804:{5DB324AE-9ED8-4E80-9BCE-8520DFFA3C93}{6ADE2191-955D-4BB3-BB54-8B9EE941FA20}'
$languageProfile = Get-ItemProperty -LiteralPath 'HKCU:\Control Panel\International\User Profile\zh-Hans-CN' -ErrorAction SilentlyContinue
$languageTip = $null -ne $languageProfile -and ($languageProfile.PSObject.Properties.Name -contains $tip)
$enableKey = Get-ItemProperty -LiteralPath 'HKCU:\Software\Microsoft\CTF\TIP\{5DB324AE-9ED8-4E80-9BCE-8520DFFA3C93}\LanguageProfile\0x00000804\{6ADE2191-955D-4BB3-BB54-8B9EE941FA20}' -ErrorAction SilentlyContinue
$profileEnabled = $null -ne $enableKey -and $enableKey.Enable -eq 1
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$report = [ordered]@{
    package = $source
    elevated = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    os_64bit = [Environment]::Is64BitOperatingSystem
    process_64bit = [Environment]::Is64BitProcess
    missing = $missing
    architecture = $architecture
    registration = $registrations
    language_tip = $languageTip
    profile_enabled = $profileEnabled
    broker_pids = @(Get-Process EShuRu -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Id)
    scope = 'Read-only files, PE architecture, COM registration and per-user TSF list. Does not test application compatibility; reads no API key or user frequency.'
}
$report | ConvertTo-Json -Depth 5
if ($RequirePackage -and $missing.Count) { throw ('Missing package files: ' + ($missing -join ', ')) }
