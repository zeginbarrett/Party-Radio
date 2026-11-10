// Waveshare ESP32-S3-AUDIO-Board microphone + offline ESP-SR wake word / MultiNet.
// Sharing the radio's 48kHz BCLK/LRCLK with an I2S1 RX slave is unverified on hardware.
// A failed mic/SR initialization MUST NOT disable Wi-Fi radio playback.
#include "voice_engine.h"
#include <Arduino.h>
#include <Wire.h>
#include <atomic>
#include <algorithm>
#include "driver/i2s_std.h"
#include "esp32-hal-sr.h"

namespace {
  constexpr uint8_t MIC_ADDR=0x40;
  std::atomic<int> pending{-1};
  std::atomic<bool> detected{false}, listeningNow{false}, awakeNow{false}, targetOn{true};
  std::atomic<uint32_t> framesSeen{0};
  std::atomic<uint32_t> readFailures{0}, wakeEvents{0}, commandEvents{0};
  std::atomic<uint16_t> leftLevel{0}, rightLevel{0};
  std::atomic<uint8_t> captureChannel{0}; // Auto by default (stronger microphone).
  i2s_chan_handle_t rx=nullptr;
  const char* failure="Not started";

  bool wr(uint8_t reg,uint8_t val) {
    Wire.beginTransmission(MIC_ADDR); Wire.write(reg);Wire.write(val);
    return Wire.endTransmission()==0;
  }
  int rd(uint8_t reg) {
    Wire.beginTransmission(MIC_ADDR);Wire.write(reg);
    if(Wire.endTransmission(false)!=0)return -1;
    if(Wire.requestFrom(int(MIC_ADDR),1)!=1)return -1;
    return Wire.read();
  }
  bool upd(uint8_t reg,uint8_t mask,uint8_t value) {
    int old=rd(reg);return old>=0 && wr(reg,uint8_t((old&~mask)|(value&mask)));
  }
  bool initMic(uint8_t gain) {
    if(rd(0x00)<0){failure="ES7210 microphone missing on I2C";return false;}
    // ES7210 register initialization based on Espressif ESP-ADF 48kHz, MCLK 12.288MHz.
    // MIC1/MIC2, 16-bit normal I2S, slave LRCLK/BCLK, both on-board microphone biases.
    const uint8_t seq[][2]={
      {0x00,0xff},{0x00,0x41},{0x01,0x3f},{0x09,0x30},{0x0A,0x30},
      {0x23,0x2a},{0x22,0x0a},{0x20,0x0a},{0x21,0x2a},
      {0x08,0x00},{0x40,0x43},{0x41,0x70},{0x42,0x70},
      {0x07,0x20},{0x02,0xc1},
      // 48kHz coeff table: adc_div=1, dll=1, doubler=1, osr=0x20,
      // LRCK high=1, low=0. See ESP-ADF es7210_config_sample.
      {0x02,0xc1},{0x07,0x20},{0x04,0x01},{0x05,0x00},
      {0x11,0x60},{0x12,0x00},
      {0x47,0x08},{0x48,0x08},{0x49,0x08},{0x4A,0x08},
      {0x4B,0xff},{0x4C,0xff}
    };
    for(const auto &item:seq)if(!wr(item[0],item[1])){failure="ES7210 register setup failed";return false;}
    if(!upd(0x01,0x0b,0x00) || !wr(0x4B,0x00)) {failure="ES7210 MIC1/2 power failed";return false;}
    gain=constrain(int(gain),0,14);
    if(!upd(0x43,0x1f,0x10|gain)||!upd(0x44,0x1f,0x10|gain)) {failure="ES7210 microphone gain setup failed";return false;}
    if(!wr(0x06,0x00)||!wr(0x01,0x00)){failure="ES7210 clock startup failed";return false;}
    return true;
  }
  bool initRx() {
    // Speaker output (I2S0) is clock master @48kHz. Separate I2S1 channel
    // receives the ES7210's audio using the *same external clock pins*.
    i2s_chan_config_t ch=I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_1,I2S_ROLE_SLAVE);
    ch.dma_desc_num=8; ch.dma_frame_num=240;
    if(i2s_new_channel(&ch,nullptr,&rx)!=ESP_OK){failure="I2S1 RX channel unavailable";return false;}
    i2s_std_config_t cfg={};
    cfg.clk_cfg=I2S_STD_CLK_DEFAULT_CONFIG(48000);
    cfg.slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,I2S_SLOT_MODE_STEREO);
    cfg.gpio_cfg.mclk=I2S_GPIO_UNUSED;
    cfg.gpio_cfg.bclk=GPIO_NUM_13;
    cfg.gpio_cfg.ws=GPIO_NUM_14;
    cfg.gpio_cfg.dout=I2S_GPIO_UNUSED;
    cfg.gpio_cfg.din=GPIO_NUM_15;
    cfg.gpio_cfg.invert_flags.mclk_inv=false;
    cfg.gpio_cfg.invert_flags.bclk_inv=false;
    cfg.gpio_cfg.invert_flags.ws_inv=false;
    if(i2s_channel_init_std_mode(rx,&cfg)!=ESP_OK){failure="I2S1 RX pin/clock configuration failed";return false;}
    if(i2s_channel_enable(rx)!=ESP_OK){failure="I2S1 RX enable failed";return false;}
    return true;
  }
  esp_err_t supplyAudio(void*, void* dest,size_t len,size_t*received,uint32_t timeout_ms) {
    *received=0;
    if(!rx)return ESP_FAIL; // Keep DMA input alive while recognition is toggled off.
    int16_t *out=static_cast<int16_t*>(dest);
    const size_t count=len/sizeof(int16_t);
    // 48 kHz stereo -> 16 kHz mono. 3:1 box decimation is a basic low-pass;
    // proper mic/audio echo cancellation requires hardware tuning.
    for(size_t pos=0;pos<count;) {
      int16_t source[3*2*80]; // 80 output samples => 240 stereo source frames.
      size_t batch=min(size_t(80),count-pos);
      size_t got=0;
      esp_err_t err=i2s_channel_read(rx,source,batch*3*2*sizeof(int16_t),&got, pdMS_TO_TICKS(timeout_ms?timeout_ms:100));
      if(err!=ESP_OK || got<batch*3*2*sizeof(int16_t)) {
        readFailures.fetch_add(1,std::memory_order_relaxed);
        return err==ESP_OK?ESP_ERR_TIMEOUT:err;
      }
      uint32_t leftAbs=0,rightAbs=0;
      for(size_t j=0;j<batch*3;j++) {
        leftAbs+=uint32_t(abs(int32_t(source[j*2])));
        rightAbs+=uint32_t(abs(int32_t(source[j*2+1])));
      }
      leftAbs/=uint32_t(batch*3); rightAbs/=uint32_t(batch*3);
      // Track both microphone channels. One dead/incorrectly mapped lane no longer
      // causes permanently silent wake-word input. The web UI exposes these levels.
      leftLevel.store(uint16_t(std::min(leftAbs,uint32_t(32767))),std::memory_order_relaxed);
      rightLevel.store(uint16_t(std::min(rightAbs,uint32_t(32767))),std::memory_order_relaxed);
      uint8_t configured=captureChannel.load(std::memory_order_relaxed);
      const bool useRight=(configured==2)||(configured==0 && rightAbs>leftAbs);
      for(size_t j=0;j<batch;j++) {
        int32_t sum=0;
        for(int k=0;k<3;k++)sum+=source[(j*3+k)*2 + (useRight?1:0)];
        out[pos+j]=int16_t(sum/3);
      }
      pos+=batch;
      framesSeen.fetch_add(batch,std::memory_order_relaxed);
    }
    *received=count*sizeof(int16_t);
    return ESP_OK;
  }
  void voiceEvent(void*,sr_event_t evt,int command_id,int) {
    if(evt==SR_EVENT_WAKEWORD) {
      wakeEvents.fetch_add(1,std::memory_order_relaxed);
      Serial.println("VOICE: COMPUTER wake word detected");
      awakeNow.store(true);listeningNow.store(true);
      // With mono input_format="M", ESP-SR sends WAKEWORD but does NOT send
      // WAKEWORD_CHANNEL. Enter COMMAND mode immediately or commands are lost.
      if(targetOn.load())sr_set_mode(SR_MODE_COMMAND);
    } else if(evt==SR_EVENT_WAKEWORD_CHANNEL) {
      // This is channel verification for an existing detection, not a second wake.
      Serial.println("VOICE: wake channel verified");
      awakeNow.store(true);listeningNow.store(true);
      // ESP-SR's raw API switches to OFF after verification;
      // the application must explicitly enable English command recognition.
      if(targetOn.load())sr_set_mode(SR_MODE_COMMAND);
    } else if(evt==SR_EVENT_COMMAND) {
      commandEvents.fetch_add(1,std::memory_order_relaxed);
      Serial.printf("VOICE: recognized command ID %d\n",command_id);
      pending.store(command_id);awakeNow.store(false);listeningNow.store(false);
      if(targetOn.load())sr_set_mode(SR_MODE_WAKEWORD);
    } else if(evt==SR_EVENT_TIMEOUT) {
      awakeNow.store(false);listeningNow.store(false);
      if(targetOn.load())sr_set_mode(SR_MODE_WAKEWORD);
    }
  }
  // MultiNet accepts multiple phrase synonyms sharing a command ID.
  static const sr_cmd_t commands[]={
    {Voice::NEXT,"Next Station"},{Voice::NEXT,"Next Song"},
    {Voice::PREVIOUS,"Previous Station"},{Voice::PREVIOUS,"Go Back"},
    {Voice::PLAY,"Play Music"},{Voice::PLAY,"Resume Playing"},
    {Voice::STOP,"Stop Music"},{Voice::STOP,"Pause Music"},
    {Voice::VOL_UP,"Volume Up"},{Voice::VOL_UP,"Louder"},
    {Voice::VOL_DOWN,"Volume Down"},{Voice::VOL_DOWN,"Quieter"},
    {Voice::MUTE,"Mute"},{Voice::MUTE,"Unmute"},
    {Voice::ALTERNATIVE,"Play Alternative Rock"},{Voice::ALTERNATIVE,"Play Alternative"},
    {Voice::GRUNGE,"Play Grunge"},{Voice::ROCK,"Play Rock"},
    {Voice::NEWS,"Play News"},{Voice::NEWS,"Play Talk Radio"},
    {Voice::WEATHER,"Play Weather Radio"},{Voice::WEATHER,"Play Weather"},
    {Voice::COUNTRY,"Play Country"},{Voice::ELECTRONIC,"Play Electronic"},
    {Voice::PARTY_LIGHTS,"Party Lights"},{Voice::PARTY_LIGHTS,"Party Mode"},
    {Voice::RAINBOW_LIGHTS,"Rainbow Lights"},{Voice::RAINBOW_LIGHTS,"Rainbow Mode"},
    {Voice::LIGHTS_OFF,"Lights Off"},{Voice::LIGHTS_OFF,"Turn Off Lights"},
    {Voice::BRIGHTER_LIGHTS,"Brighter Lights"},{Voice::DIMMER_LIGHTS,"Dim The Lights"},
    {Voice::FAVORITES,"Favorites Only"},{Voice::SHUFFLE,"Shuffle Stations"},
    {Voice::SLEEP_30,"Sleep In Thirty Minutes"},{Voice::SLEEP_60,"Sleep In One Hour"},
    {Voice::PLAY_STATION_ONE,"Play Station One"}
  };
}
namespace Voice {
  bool begin(bool turnOn,uint8_t gain,uint8_t channel) {
    captureChannel.store(channel<=2?channel:0);
    targetOn.store(turnOn);
    if(!turnOn){failure="Disabled in web settings";return false;}
    failure="Mic/voice not initialized";
    if(!initMic(gain) || !initRx()) {Serial.printf("VOICE: %s\n",failure);return false;}
    const esp_err_t result=sr_start(supplyAudio,nullptr,SR_CHANNELS_MONO,SR_MODE_WAKEWORD,"M",commands,sizeof(commands)/sizeof(commands[0]),voiceEvent,nullptr);
    if(result!=ESP_OK){failure="ESP-SR model startup failed; check model partition";Serial.println(failure);return false;}
    detected.store(true);failure="";
    Serial.println("VOICE: enabled. Say COMPUTER, then a command.");
    return true;
  }
  void end(){if(detected.load())sr_stop();detected.store(false);targetOn.store(false);listeningNow.store(false);}
  int takeCommand(){return pending.exchange(-1);}
  bool ready(){return detected.load();}
  bool listening(){return listeningNow.load();}
  bool awake(){return awakeNow.load();}
  const char* error(){return failure;}
  uint32_t frames(){return framesSeen.load();}
  uint32_t readsFailed(){return readFailures.load();}
  uint32_t wakes(){return wakeEvents.load();}
  uint32_t recognizedCommands(){return commandEvents.load();}
  uint16_t micLeftLevel(){return leftLevel.load();}
  uint16_t micRightLevel(){return rightLevel.load();}
  uint8_t selectedChannel(){return captureChannel.load();}
  void setEnabled(bool on){targetOn.store(on);if(detected.load())sr_set_mode(on?SR_MODE_WAKEWORD:SR_MODE_OFF);}
  bool enabled(){return targetOn.load();}
}
