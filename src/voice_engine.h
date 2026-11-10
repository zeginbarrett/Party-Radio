#pragma once
// ESP-SR voice processing on device. Does not stream microphone audio to the internet.
#include <stdint.h>

namespace Voice {
  enum Command : int {
    NONE=-1, NEXT=1, PREVIOUS, PLAY, STOP, VOL_UP, VOL_DOWN, MUTE,
    ALTERNATIVE, GRUNGE, ROCK, NEWS, WEATHER, COUNTRY, ELECTRONIC,
    PARTY_LIGHTS, RAINBOW_LIGHTS, LIGHTS_OFF, BRIGHTER_LIGHTS, DIMMER_LIGHTS,
    FAVORITES, SHUFFLE, SLEEP_30, SLEEP_60, PLAY_STATION_ONE
  };
  bool begin(bool enabled, uint8_t gain = 8);
  void end();
  int takeCommand();
  bool ready();
  bool listening();
  bool awake();
  const char* error();
  uint32_t frames();
  void setEnabled(bool enabled);
  bool enabled();
}
