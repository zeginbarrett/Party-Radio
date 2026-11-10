# Computer wake-word troubleshooting: v3.5 diagnostic build

The v3.4 firmware included *untested* ESP-SR voice support. On actual boards, voice can fail because the ES7210 ADC and ES8311 speaker share an I²S clock pair. v3.5 is a diagnostic improvement, **not proof that wake-word recognition now works**.

## What changed

- Fixed an actual software bug: when ESP-SR did not initialize at boot, switching Voice Control on from the browser previously did **not** start it. The radio now automatically restarts to retry startup.
- Fixed toggling Voice Control off so that it no longer forces the microphone callback to fail, which could leave the underlying ESP-SR audio task in a bad state.
- Exposed live microphone activity on both left/right ES7210 channels (average absolute PCM level in 0..32767), DMA frame count, I²S read failures, wake event count and recognized command count.
- Added an **Auto / Left / Right** mic-channel selector. Auto uses whichever of the two raw channels is stronger for each short audio block; use Left/Right to verify whether one channel is wired or mapped incorrectly.
- Logs wake-word and command detection over USB serial.
- Does not change internet radio presets, audio playback, LEDs, networking, or the no-backup flasher policy.

## Test from your phone or computer

1. Open `http://waveshare-radio.local/` on your home Wi-Fi; or connect directly to `PartyRadio-Setup` and open `http://192.168.4.1/`.
2. Scroll to **Computer — on-device voice control**. Verify that **Enable wake word and commands** is selected. If you enable it after startup failed, use Save: the radio will reboot and retry.
3. With music paused or quiet, speak close to the two microphone openings. Watch **Live microphone input** (Left / Right). Refresh is automatic; use Check microphone status if desired.
4. If **PCM frames are zero and stay zero**, look at the startup error and I²S read failures. This usually indicates microphone/controller initialization or shared I²S clock capture, not pronunciation of “Computer.”
5. If **PCM frames climb but both mic levels remain near zero** while speaking, try Left and Right separately (Save; reboots), and examine the ES7210 configuration.
6. If mic level reacts to speech and **Wake detections stays zero**, audio may be distorted, the model may not be loaded, or radio echo may mask the wake phrase. Try with music stopped and increase microphone gain gradually.
7. If **Wake detections increases** but no command runs, inspect **Recognized commands** and the blue listening indication. Say “Next station” after the wake response.

## Firmware model checks

The installer must successfully flash BOTH the application and the generated ESP-SR `srmodels.bin`. The app should be built with `CONFIG_SR_WN_WN9_COMPUTER_TTS=y`, `CONFIG_SR_MN_EN_MULTINET6_QUANT=y`, and the model image written to partition `model` at offset `0x620000`. Voice will not work from the app binary alone.

## Limitations

A successful build does not establish that both microphone channels capture clean PCM. v3.5 preserves the existing stereo/slave input strategy; the hardware uses a shared bus and may require adapting its clock/slot configuration. Neither the wake word nor simultaneous music playback has been verified on the user's device; do not mislabel source-defined commands as tested working commands.

**No flash backup is performed or required.** The installer only flashes after user confirmation.
