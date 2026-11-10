$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$localLib = Join-Path $root 'lib'
$audio = Join-Path $localLib 'ESP32-audioI2S'
$codec = Join-Path $localLib 'ES8311'
New-Item -ItemType Directory -Force -Path $localLib,$codec | Out-Null
if (!(Test-Path (Join-Path $audio 'src\Audio.h'))) {
  if (Test-Path $audio) { Remove-Item $audio -Recurse -Force }
  Write-Host 'Downloading upstream audio decoder + ES8311 codec driver...'
  & git.exe clone --depth 1 https://github.com/schreibfaul1/ESP32-audioI2S.git $audio
  if ($LASTEXITCODE -ne 0) { throw 'Audio library download failed. Check Internet and Git.' }
}
$src = Join-Path $audio 'examples\ES8311'
foreach($name in @('es8311.h','es8311.cpp')) {
  $from = Join-Path $src $name
  if (!(Test-Path $from)) { throw "Upstream ES8311 example missing $name (library may have changed)." }
  Copy-Item $from (Join-Path $codec $name) -Force
}

# Compatibility fix for the upstream ESP32-audioI2S constructor (Arduino core 3.x).
# The upstream source uses a runtime `if` for two incompatible class APIs.
# C++ type-checks both branches, so the Arduino 4-only method fails compilation
# on the pinned Arduino 3.x core, even though its branch won't execute.
# This narrowly replaces the runtime condition with a preprocessor condition.
$audioSource = Join-Path $audio 'src\Audio.cpp'
if (!(Test-Path -LiteralPath $audioSource)) {
  throw "Upstream Audio.cpp not found at $audioSource"
}
$audioText = [System.IO.File]::ReadAllText($audioSource)
$audioPattern = '(?s)\bif\s*\(\s*ESP_ARDUINO_VERSION_MAJOR\s*<\s*4\s*\)\s*\{\s*clientsecure\.setInsecure\s*\(\s*\)\s*;\s*\}\s*else\s*\{\s*clientsecure\.useBuiltinCACertBundle\s*\(\s*\)\s*;\s*\}'
$audioReplacement = @'
#if ESP_ARDUINO_VERSION_MAJOR < 4
        clientsecure.setInsecure();
#else
        clientsecure.useBuiltinCACertBundle();
#endif
'@
if ([regex]::IsMatch($audioText, $audioPattern)) {
  $audioFixed = [regex]::Replace($audioText, $audioPattern, $audioReplacement)
  [System.IO.File]::WriteAllText($audioSource, $audioFixed, ([System.Text.UTF8Encoding]::new($false)))
  Write-Host 'Applied ESP32-audioI2S Arduino 3.x compile fix.' -ForegroundColor Green
} elseif ($audioText -match '(?s)#if\s+ESP_ARDUINO_VERSION_MAJOR\s*<\s*4.*?clientsecure\.useBuiltinCACertBundle\s*\(\s*\)') {
  Write-Host 'Audio library compatibility fix is already applied.' -ForegroundColor Green
} elseif ($audioText -match 'clientsecure\.useBuiltinCACertBundle\s*\(\s*\)') {
  throw 'Unexpected Audio.cpp layout. Cannot safely patch the audio library; nothing has been flashed.'
} else {
  Write-Host 'Audio library does not use the incompatible method; no patch required.' -ForegroundColor Green
}

# Code for these components is fetched from the upstream repository and remains subject
# to its own license terms (see source headers, repo LICENSE and README).
# audioI2S declares the user callback weak in Audio.h; this can cause the
# visualizer's override to be discarded by the linker. Remove weak from ONLY
# the declaration and retain the library's default weak definition.
$audioHeader = Join-Path $audio 'src\Audio.h'
$headerText = [System.IO.File]::ReadAllText($audioHeader)
$declarationPattern = 'extern\s+__attribute__\(\(weak\)\)\s+void\s+audio_process_i2s\s*\('
if ([regex]::IsMatch($headerText, $declarationPattern)) {
  $headerText = [regex]::Replace($headerText, $declarationPattern, 'extern void audio_process_i2s(')
  [System.IO.File]::WriteAllText($audioHeader, $headerText, ([System.Text.UTF8Encoding]::new($false)))
  Write-Host 'Enabled strong PCM callback for real music-synchronized LEDs.' -ForegroundColor Green
} elseif ($headerText -match 'extern\s+void\s+audio_process_i2s\s*\(') {
  Write-Host 'PCM callback declaration already ready.' -ForegroundColor Green
} else {
  throw 'Audio API changed: PCM hook missing. Build stopped BEFORE flashing.'
}
Write-Host 'Audio libraries prepared.' -ForegroundColor Green
