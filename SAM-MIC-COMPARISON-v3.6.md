# v3.6 — Sam / ESPHome microphone power-up repair

## Why this repair exists

On the actual Waveshare, Party Radio v3.5 reports **Left 9 / Right 0**, no I2S read failures, and no `Computer` wake events. PCM frame counts increasing do not demonstrate working microphones.

The previously working **SAM-STD-I2S16-ESPHOME-v4** firmware on the *same board* used the ES7210 in **16-bit, standard stereo I2S at 16 kHz**. Verified Sam logs reported nonzero mic peaks / PCM samples. The working configuration's documented registers included `0x11=0x60`, `0x12=0x00`, `0x40=0xC3`, `0x43=0x18`, `0x44=0x18` and `0x4B=0x0F`.

v3.5 had `0x40=0x43` and ended with `0x4B=0x00`, without the `0x71 -> 0x41` final startup/reset. v3.6 changes to the **ESPHome/Sam-inspired power-up sequence** using `0x40=C3` and `0x4B=0F`, matching the reference driver, plus final register readback.

**Important limitation:** Sam captured at 16 kHz, but Party Radio plays internet streams at 48 kHz, with an independent I2S1 slave receiver sharing physical clocks. The radio therefore keeps its known 48-kHz ADC clock coefficient and 48→16-kHz downsample to the speech recognizer. This is **not the full working Sam audio pipeline**, and the shared-clock arrangement still requires actual hardware testing. No one should describe it as proven fixed until microphone levels follow speech and wake events appear.

## What to verify after flashing

1. Open `http://waveshare-radio.local` or `http://192.168.4.1` on the PartyRadio-Setup network.
2. Use the Voice page; say words/clap *near* the microphone with the radio turned down.
3. Check that **Left** or **Right** levels climb significantly above the quiet baseline and vary with speech.
4. Check Diagnostics readback: `ES7210 analog 0x40 = 0xc3`, `ES7210 mic power 0x4B = 0x0f`, and `ES7210 audio format 0x11 = 0x60`.
5. Once signal is present, say `Computer`, then `Next station`. Wake count should rise before command count.

If readback is correct but both microphone levels remain near zero, the issue is likely in **shared I2S timing, BCLK/LRCLK/MCLK, or RX slot alignment**, not recognition sensitivity or the words being spoken. The next hardware-backed step is a temporary mic-only 16-kHz diagnostic firmware mode using the exact working Sam clock arrangement, not another gain change.

## References

- [ESPHome ES7210 2026.10 driver](https://api-docs.esphome.io/es7210_8cpp_source): 0x40 C3, 0x4B/0x4C 0F, final 0x71/0x41 sequence
- [ESPHome ES7210 coefficient table](https://api-docs.esphome.io/es7210__const_8h_source): 48kHz @ 12.288 MHz 0x02 C1 and 0x07 20
- [Waveshare pinout and shared I2S wiring](https://github.com/MichalZaniewicz/esphome-waveshare-esp32-s3-audio-va/blob/main/docs/HARDWARE.md)

**The full original Sam source ZIP was not available in this workspace.** The Sam configuration and observed working audio above are based on the user's earlier recorded Sam logs, compared against the publicly available ESPHome source; no reverse engineering of the private Sam firmware BIN was performed.

**No firmware backups are ever performed by this installer.**
