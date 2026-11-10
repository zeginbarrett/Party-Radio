# Build and hardware troubleshooting

| Symptom | Notes |
|---|---|
| `SCons.Tool.FortranCommon` missing | v3.1 isolated PlatformIO 6.1.19 in `%LOCALAPPDATA%\PartyRadio20Pro\pio` to avoid a mismatch with installed SCons packages. |
| Link error `app_main` missing | v3.2 enables `CONFIG_AUTOSTART_ARDUINO=y` in `sdkconfig.defaults`. |
| SSL link symbols missing | v3.2 enables mbedTLS PSK options required by the Arduino secure client. |
| SDK config preflight fails after successful build | v3.3 trims line endings before validating generated SDKCONFIG settings. |
| GCC internal compiler error / segfault in `esp_lcd_panel_rgb.c` | v3.4 selects size optimization (`-Os`) and one build worker. This workaround has **not** been confirmed on user hardware. |
| `srmodels.bin` missing | Voice recognition cannot work unless a compatible Computer WakeNet + English MultiNet model image is generated and flashed to the dedicated model partition. The installer should stop before flashing. |
| No home-network web page | Join `PartyRadio-Setup`, then visit `http://192.168.4.1`; if enabled and mDNS works, use `http://waveshare-radio.local` on the same LAN. |
| COM port unavailable | Use a USB **data** cable and the ESP32-S3 serial/JTAG COM port, not Bluetooth COM ports. |
| Voice misses commands over loud music | Dual-microphone capture and ESP-SR audio coexistence have not been bench-tested; inspect the web voice diagnostic panel and adjust mic gain. |

The flashing installer intentionally **does not read or back up firmware**. Existing Waveshare firmware will be overwritten when you confirm FLASH. It should never touch the Windows Sammy installation, Raspberry Pi RadioVault, or unrelated ESP32 devices.

Include the last 80 lines of your Windows build log when filing an issue.
