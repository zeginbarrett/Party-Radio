$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Do not touch the user's existing %USERPROFILE%\.platformio installations.
# PIOArduino 55.03.39 requires the SCons 4.8.1 runtime shipped with PIO Core 6.1.19.
# PlatformIO 6.2.0 replaces it with 4.11.1 and breaks hybrid Arduino/IDF builds
# at the link step (SCons.Tool.FortranCommon missing).
# Use a short, dedicated cache path, so multiple radio builds and other ESP32
# projects cannot uninstall tool-scons from under one another.
if ($env:LOCALAPPDATA) {
  $env:PLATFORMIO_CORE_DIR = Join-Path $env:LOCALAPPDATA 'PartyRadio20Pro\pio'
} else {
  $env:PLATFORMIO_CORE_DIR = Join-Path $root '.radio-pio-core'
}
New-Item -ItemType Directory -Force -Path $env:PLATFORMIO_CORE_DIR | Out-Null
Write-Host "Isolated PlatformIO cache: $env:PLATFORMIO_CORE_DIR" -ForegroundColor DarkCyan

Write-Host ''
Write-Host '====================================================' -ForegroundColor Cyan
Write-Host ' WAVESHARE PARTY RADIO 20 PRO V3.4 - COMPUTER VOICE' -ForegroundColor Cyan
Write-Host '====================================================' -ForegroundColor Cyan
Write-Host 'Fix: ESP-IDF LCD GCC crash: -Os + single-worker builds; isolated PlatformIO 6.1.19.'
Write-Host 'Direct flash: no firmware backup, no flash read, no backup prompt.'
Write-Host 'Computer wake word + local English commands (experimental dual-mic capture).' 
Write-Host 'Your Raspberry Pi, CYD, and Windows Sam installation are not touched.'
Write-Host ''

function Test-PythonCandidate {
  param([string]$Command, [string[]]$Prefix = @())
  # PowerShell 5.1 can strip quotes from Python -c arguments when launching
  # native programs. Use --version instead: no embedded quotes or Python code.
  if (-not $Command) { return $null }
  try {
    $output = @(& $Command @Prefix --version 2>&1)
    if ($LASTEXITCODE -ne 0) { return $null }
    foreach ($line in $output) {
      $versionText = [string]$line
      if ($versionText.Trim() -match '^Python\s+(3\.(?:10|11|12|13|14))\.\d+') {
        return $Matches[1]
      }
    }
  } catch {
    # Continue searching other installed Python versions.
  }
  return $null
}
function Find-SupportedPython {
  $launcher = Get-Command py.exe -ErrorAction SilentlyContinue
  if ($launcher) {
    foreach ($version in @('3.13','3.12','3.14','3.11','3.10')) {
      $prefix = @("-$version")
      $found = Test-PythonCandidate -Command $launcher.Source -Prefix $prefix
      if ($found) { return @{ Command = $launcher.Source; Prefix = $prefix; Version = $found } }
    }
  }
  $paths = @(
    (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313\python.exe'),
    (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe'),
    (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python314\python.exe'),
    (Join-Path $env:ProgramFiles 'Python313\python.exe'),
    (Join-Path $env:ProgramFiles 'Python312\python.exe'),
    (Join-Path $env:ProgramFiles 'Python314\python.exe')
  )
  foreach ($path in $paths) {
    if ((Test-Path $path) -and (Test-PythonCandidate -Command $path)) {
      return @{ Command = $path; Prefix = @(); Version = (Test-PythonCandidate -Command $path) }
    }
  }
  $fallback = Get-Command python.exe -ErrorAction SilentlyContinue
  if ($fallback) {
    $version = Test-PythonCandidate -Command $fallback.Source
    if ($version) { return @{ Command = $fallback.Source; Prefix = @(); Version = $version } }
  }
  return $null
}

if (!(Get-Command git.exe -ErrorAction SilentlyContinue)) {
  throw 'Git for Windows is required: https://git-scm.com/download/win'
}

$python = $null
if ($env:RADIOVAULT_PYTHON -and (Test-Path -LiteralPath $env:RADIOVAULT_PYTHON)) {
  $detected = Test-PythonCandidate -Command $env:RADIOVAULT_PYTHON
  if ($detected) {
    $python = @{ Command = $env:RADIOVAULT_PYTHON; Prefix = @(); Version = $detected }
  }
}
if (!$python) { $python = Find-SupportedPython }
if (!$python) {
  Write-Host ''
  Write-Host 'Party Radio could not find a compatible Python interpreter.' -ForegroundColor Red
  Write-Host 'In a NEW PowerShell window try: py -3.13 --version'
  Write-Host 'Or set RADIOVAULT_PYTHON to your Python 3.13 python.exe path.'
  throw 'Python detection failed; no software was installed or flashed. Python 3.15 is unchanged.'
}
Write-Host "Using Python $($python.Version)" -ForegroundColor Green

$venv = Join-Path $root '.radio-tools'
$venvPython = Join-Path $venv 'Scripts\python.exe'
$pio = Join-Path $venv 'Scripts\pio.exe'

if (Test-Path $venvPython) {
  $existing = Test-PythonCandidate -Command $venvPython
  if (!$existing) { $existing = 'unknown' }
  if ($existing -ne $python.Version) {
    Write-Host "Found incomplete/old installer environment (Python $existing). Recreating only .radio-tools..." -ForegroundColor Yellow
    Remove-Item -LiteralPath $venv -Recurse -Force
  }
} elseif (Test-Path $venv) {
  Write-Host 'Removing incomplete installer-only environment...' -ForegroundColor Yellow
  Remove-Item -LiteralPath $venv -Recurse -Force
}
if (!(Test-Path $venvPython)) {
  Write-Host 'Creating Party Radio installer environment...'
  $pythonExe = $python.Command
  $pythonArgs = [string[]]$python.Prefix
  & $pythonExe @pythonArgs -m venv $venv
  if ($LASTEXITCODE -ne 0) { throw 'Cannot create Python environment.' }
}

Write-Host 'Installing/checking PlatformIO (precompiled PyYAML only)...' -ForegroundColor Cyan
& $venvPython -m pip install --disable-pip-version-check --only-binary=PyYAML 'platformio==6.1.19'
if ($LASTEXITCODE -ne 0) {
  throw 'Installing PlatformIO failed. See package errors above; Waveshare has NOT been flashed.'
}
if (!(Test-Path $pio)) { throw 'PlatformIO is missing after installation. Waveshare has NOT been flashed.' }
# Fail closed if an unexpected system-wide/newer PlatformIO somehow took over.
$pioVersionOutput = ((& $pio --version) | Out-String).Trim()
if ($LASTEXITCODE -ne 0 -or $pioVersionOutput -notmatch '6\.1\.19') {
  throw "Unexpected PlatformIO version '$pioVersionOutput'. Expected 6.1.19; no flash attempted."
}
Write-Host "Using $pioVersionOutput" -ForegroundColor Green


Write-Host 'Getting the radio decoder and ES8311 driver...' -ForegroundColor Cyan
& (Join-Path $PSScriptRoot 'prepare-libs.ps1')
if ($LASTEXITCODE -and $LASTEXITCODE -ne 0) { throw 'Audio library preparation failed. Waveshare has NOT been flashed.' }

# Fail early on absent SDK settings rather than compiling for 15+ minutes.
$configText = Get-Content -LiteralPath (Join-Path $root 'sdkconfig.defaults') -Raw
foreach ($required in @('CONFIG_AUTOSTART_ARDUINO=y',
                       'CONFIG_MBEDTLS_PSK_MODES=y',
                       'CONFIG_MBEDTLS_KEY_EXCHANGE_PSK=y',
                       'CONFIG_SR_WN_WN9_COMPUTER_TTS=y',
                       'CONFIG_SR_MN_EN_MULTINET6_QUANT=y',
                       'CONFIG_COMPILER_OPTIMIZATION_SIZE=y')) {
  if ($configText -notmatch ('(?m)^' + [regex]::Escape($required) + '$')) {
    throw "Missing required Arduino/TLS/Computer setting: $required; no flash attempted."
  }
}

# Config defaults only apply when the generated sdkconfig does not already define
# the option. v3.3 may have generated an old -Og sdkconfig before crashing.
# Reset ONLY this Party Radio project's generated SDK config and build folder.
# This does not erase device flash, stations.csv, Windows Sammy, or other projects.
$generatedBeforeBuild = Join-Path $root 'sdkconfig.waveshare_s3'
if (Test-Path -LiteralPath $generatedBeforeBuild) {
  $previousSettings = @(Get-Content -LiteralPath $generatedBeforeBuild | ForEach-Object { $_.Trim() })
  if ($previousSettings -notcontains 'CONFIG_COMPILER_OPTIMIZATION_SIZE=y') {
    Write-Host 'Found older ESP-IDF compiler optimization. Reconfiguring ONLY this Party Radio build...' -ForegroundColor Yellow
    Remove-Item -LiteralPath $generatedBeforeBuild -Force
    $oldSdkconfig = "$generatedBeforeBuild.old"
    if (Test-Path -LiteralPath $oldSdkconfig) { Remove-Item -LiteralPath $oldSdkconfig -Force }
    $localBuild = Join-Path $root '.pio\build\waveshare_s3'
    if (Test-Path -LiteralPath $localBuild) { Remove-Item -LiteralPath $localBuild -Recurse -Force }
  }
}

Write-Host 'Compiling firmware in one-worker mode (no device changes yet)...' -ForegroundColor Cyan
& $pio run -e waveshare_s3 -j 1
if ($LASTEXITCODE -ne 0) { throw 'Voice+Radio firmware compilation failed. The Waveshare has NOT been flashed. Keep the build output.' }
# Verify the GENERATED SDKCONFIG using normalized lines, not CRLF-sensitive regex.
# v3.2 incorrectly rejected successfully compiled firmware when Windows generated
# `CONFIG_...=y` lines with CRLF endings. Get-Content + Trim fixes that.
$generatedSdkconfig = Join-Path $root 'sdkconfig.waveshare_s3'
if (!(Test-Path -LiteralPath $generatedSdkconfig)) {
  throw "Generated ESP-IDF SDKCONFIG missing: $generatedSdkconfig. The speech model choices cannot be verified; no flash."
}
function Test-SdkFlag {
  param([string[]]$Lines, [string]$Setting)
  # Exact values, tolerant of Windows CRLF and whitespace; never treat commented-out flags as enabled.
  return ($Lines -contains $Setting)
}
$activeConfigLines = @(Get-Content -LiteralPath $generatedSdkconfig | ForEach-Object { $_.Trim() })
$requiredFlags = @(
  'CONFIG_AUTOSTART_ARDUINO=y',
  'CONFIG_MBEDTLS_PSK_MODES=y',
  'CONFIG_MBEDTLS_KEY_EXCHANGE_PSK=y',
  'CONFIG_SR_WN_WN9_COMPUTER_TTS=y',
  'CONFIG_SR_MN_EN_MULTINET6_QUANT=y',
  'CONFIG_PARTITION_TABLE_CUSTOM=y',
  'CONFIG_COMPILER_OPTIMIZATION_SIZE=y'
)
$missingFlags = @($requiredFlags | Where-Object { -not (Test-SdkFlag -Lines $activeConfigLines -Setting $_) })
if ($missingFlags.Count -gt 0) {
  Write-Host 'Actual generated ESP-IDF settings:' -ForegroundColor Yellow
  foreach ($wanted in $missingFlags) {
    $key = ($wanted -split '=')[0]
    $candidates = @($activeConfigLines | Where-Object { $_ -match ('^#?\s*' + [regex]::Escape($key) + '(?:\s|=)') })
    if ($candidates.Count -gt 0) { Write-Host "  $key : $($candidates -join ' / ')" }
    else { Write-Host "  $key : absent" }
  }
  throw "Missing required voice/Arduino settings in GENERATED SDKCONFIG. Nothing was flashed. To force a fresh configuration, rename/remove only this project's sdkconfig.waveshare_s3 and .pio\build\waveshare_s3 then rerun."
}
Write-Host 'Actual Arduino/TLS/Computer/MultiNet/partition/size-optimization SDK settings verified.' -ForegroundColor Green

# Confirm actual linked binary was generated.
$buildRoot = Join-Path $root '.pio\build\waveshare_s3'
foreach ($fileName in @('firmware.bin', 'firmware.elf', 'partitions.bin')) {
  $candidate = Join-Path $buildRoot $fileName
  if (!(Test-Path -LiteralPath $candidate)) { throw "Missing compiled artifact: $fileName; no flash." }
}

# The radio's speech data must include the requested model and MultiNet commands.
# PIOArduino hybrid builds sometimes create firmware.bin but do not run ESP-SR's
# srmodels target. In that case, explicitly invoke Espressif's official packer.
$modelsDir = Join-Path $buildRoot 'srmodels'
$model = Join-Path $modelsDir 'srmodels.bin'
$wakeDir = Join-Path $modelsDir 'wn9_computer_tts'
$englishDir = Join-Path $modelsDir 'mn6_en'
if (!(Test-Path -LiteralPath $model) -or !(Test-Path -LiteralPath $wakeDir) -or !(Test-Path -LiteralPath $englishDir)) {
  Write-Host 'Generating actual Computer and English recognition models from the SDKCONFIG...' -ForegroundColor Cyan
  $espSrCandidates = @(
    (Join-Path $buildRoot 'managed_components\espressif__esp-sr'),
    (Join-Path $root 'managed_components\espressif__esp-sr'),
    (Join-Path $root 'components\esp-sr')
  )
  $espSrRoot = $null
  foreach ($candidate in $espSrCandidates) {
    if (Test-Path -LiteralPath (Join-Path $candidate 'model\movemodel.py')) {
      $espSrRoot = $candidate
      break
    }
  }
  if (!$espSrRoot) {
    throw "Espressif speech model builder not found in managed_components. Nothing was flashed. Keep the build output."
  }
  $modelScript = Join-Path $espSrRoot 'model\movemodel.py'
  & $venvPython $modelScript -d1 $generatedSdkconfig -d2 $espSrRoot -d3 $buildRoot
  if ($LASTEXITCODE -ne 0) { throw 'Could not pack Computer voice models. Nothing was flashed.' }
}
foreach ($needed in @($model, $wakeDir, $englishDir)) {
  if (!(Test-Path -LiteralPath $needed)) { throw "Required Computer/English model missing: $needed. Nothing was flashed." }
}
$modelBytes = (Get-Item -LiteralPath $model).Length
if ($modelBytes -lt 50000 -or $modelBytes -gt 6291456) {
  throw "Speech models do not fit the 6MB partition (bytes=$modelBytes). Nothing was flashed."
}
Write-Host "VERIFIED Computer WakeNet + English MultiNet model image: $modelBytes bytes" -ForegroundColor Green


Write-Host ''
Write-Host 'Compilation successful. Available serial ports:' -ForegroundColor Green
& $pio device list
Write-Host ''
Write-Host 'The next step will REPLACE the current firmware on the Waveshare you select.' -ForegroundColor Yellow
Write-Host 'No backup will be created or required.' -ForegroundColor Yellow
$port = (Read-Host 'Waveshare COM port (example COM5; leave blank to cancel)').Trim().ToUpperInvariant()
if (!$port) {
  Write-Host 'Canceled. No changes made to your Waveshare.' -ForegroundColor Yellow
  exit 0
}
if ($port -notmatch '^COM[0-9]+$') { throw 'Invalid COM port. Example COM5.' }
if ((Read-Host "Type FLASH to write Party Radio 20 directly to $port") -cne 'FLASH') {
  Write-Host 'Canceled. Firmware was compiled but nothing was flashed.' -ForegroundColor Yellow
  exit 0
}
Write-Host "Flashing Waveshare directly on $port (NO BACKUP)..." -ForegroundColor Cyan
& $pio run -e waveshare_s3 -j 1 -t upload --upload-port $port
if ($LASTEXITCODE -ne 0) {
  throw 'Upload failed. Check the COM port, BOOT/RESET buttons, and USB data cable. No backup is involved.'
}
Write-Host 'Now flashing the Computer wake-word and command models (6 MB model partition)...' -ForegroundColor Cyan
& $venvPython -m pip install --disable-pip-version-check --only-binary=PyYAML 'esptool>=5.0,<6.0'
if ($LASTEXITCODE -ne 0) { throw 'Could not install esptool for voice models. App flashed; voice models NOT flashed.' }
& $venvPython -m esptool --chip esp32s3 --port $port --baud 460800 write-flash 0x620000 $model
if ($LASTEXITCODE -ne 0) { throw 'Voice model flash failed. Do not assume voice controls work. Check USB and COM port.' }
Write-Host ''
Write-Host 'APPLICATION + COMPUTER MODELS UPLOADED.' -ForegroundColor Green
Write-Host 'Connect to PartyRadio-Setup, password party-radio20.'
Write-Host 'Open http://192.168.4.1 to configure your home 2.4 GHz Wi-Fi.'
Write-Host 'At every boot, Party Radio starts the configured alternative-rock station once Wi-Fi connects.'
Write-Host 'Voice: say COMPUTER, then NEXT STATION or PLAY NEWS (hardware test required).'
Write-Host 'The full web interface remains at waveshare-radio.local or the setup hotspot.'
Write-Host 'Sound and live radio still need to be verified on the actual speaker.'
