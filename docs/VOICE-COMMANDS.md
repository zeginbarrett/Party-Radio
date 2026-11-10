# Offline voice commands — experimental

Wake word: **Computer**. Say it first, then speak a command. Commands use the Espressif ESP-SR models defined in the source. Functionality requires a successful firmware build, correct model partition flashing, and hardware verification. No cloud service is required by the recognition engine.

| Action | Supported phrases |
|---|---|
| Next station | Next Station; Next Song |
| Previous station | Previous Station; Go Back |
| Start playback | Play Music; Resume Playing |
| Stop playback | Stop Music; Pause Music |
| Increase volume | Volume Up; Louder |
| Decrease volume | Volume Down; Quieter |
| Toggle mute | Mute; Unmute |
| Alternative rock | Play Alternative Rock; Play Alternative |
| Grunge | Play Grunge |
| Rock | Play Rock |
| News/talk | Play News; Play Talk Radio |
| Weather stream | Play Weather Radio; Play Weather |
| Country | Play Country |
| Electronic | Play Electronic |
| First preset | Play Station One |
| Party lighting | Party Lights; Party Mode |
| Rainbow lighting | Rainbow Lights; Rainbow Mode |
| Disable LEDs | Lights Off; Turn Off Lights |
| Brighter LEDs | Brighter Lights |
| Dimmer LEDs | Dim The Lights |
| Favorites only toggle | Favorites Only |
| Shuffle toggle | Shuffle Stations |
| Sleep 30 min | Sleep In Thirty Minutes |
| Sleep 1 hour | Sleep In One Hour |

*The commands are source-defined, not proven live-device recognition results. Some phrases may map to the same action, and microphone sensitivity/echo handling require testing.*
