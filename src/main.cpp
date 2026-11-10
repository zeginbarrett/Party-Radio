// Waveshare Party Radio 20: standalone Wi-Fi internet radio + PCM-synchronized RGB ring.
// Target hardware: Waveshare ESP32-S3-AUDIO-Board (ES8311 / TCA9555 / 7 WS2812 RGB LEDs).
// The Raspberry Pi and CYD are not required for internet-radio playback.
#include <Arduino.h>
#include <atomic>
#include <math.h>
#include <time.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <LittleFS.h>
#include <Wire.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <Audio.h>
#include "es8311.h"  // from upstream audioI2S example, installed by scripts/prepare-libs.ps1
#include "default_stations.h"
#include "web_ui.h"
#include "voice_engine.h"

namespace HW {
constexpr int SDA=11, SCL=10;
constexpr int I2S_MCLK=12, I2S_BCLK=13, I2S_LRCLK=14, I2S_DOUT=16;
constexpr int PIXELS=38, PIXEL_COUNT=7;
constexpr uint8_t EXP=0x20;  // TCA9555
constexpr uint8_t EXP_INPUT_PORT1=0x01;
constexpr uint8_t EXP_OUTPUT_PORT1=0x03;
constexpr uint8_t EXP_CONFIG_PORT1=0x07;
constexpr uint8_t AMP_BIT=0;    // EXIO08 => port1 bit0; active high
constexpr uint8_t KEY_MASK[3]={0x02,0x04,0x08}; // EXIO09, 10, 11 active low
}

constexpr uint8_t STATION_COUNT = 20;
constexpr char FW_VERSION[] = "Waveshare Party Radio 20 Pro v3.5 MIC DIAGNOSTICS";
constexpr char AP_SSID[] = "PartyRadio-Setup";
constexpr char AP_PASS[] = "party-radio20"; // First-boot AP only: change home Wi-Fi via setup page

struct Station { String name, genre, url; };
Station stations[STATION_COUNT];
WebServer web(80);
Preferences prefs;
Audio audio;
ES8311 codec;
Adafruit_NeoPixel pixels(HW::PIXEL_COUNT, HW::PIXELS, NEO_GRB + NEO_KHZ800);

String wifiSsid, wifiPass, title, streamName, customUrl;
// Party Radio Pro preferences. All settings remain on the ESP32 after power-off.
String hostname="waveshare-radio", apPassword="party-radio20";
String timezoneRule="CST6CDT,M3.2.0,M11.1.0";
bool keepSetupAP=true, favoritesOnly=false, shuffleMode=false, muted=false;
bool fadeEnabled=true, quietHours=false, alarmEnabled=false;
bool voiceEnabled=true;uint8_t micGain=8;uint8_t micChannel=0;
bool voiceDucking=false;uint32_t voiceDuckTime=0;
String lastVoiceCommand="None";
uint32_t favoriteMask=0;
uint8_t bootStation=0, bootVolume=12;
uint8_t alarmHour=7, alarmMinute=0, alarmStation=0;
uint8_t ledSensitivity=55, ledSpeed=50, ledHueDegrees=200;
uint8_t quietStart=23, quietEnd=7;
uint32_t sleepUntil=0, rebootAt=0, restartAPAt=0, streamRetries=0;
uint32_t lastClockService=0;
int lastAlarmDate=-1;
String lastStreamError="";
bool fadeRunning=false;
uint8_t fadeVolume=0;
uint32_t lastFadeStep=0;

uint8_t selected=0, volume=12, ledMode=0, ledBrightness=90;
bool wantPlay=true, waitingForTune=true, usingCustom=false;
bool codecReady=false, expanderReady=false, apActive=false, mdnsActive=false, serverReady=false;
uint32_t lastAttempt=0, lastWifiAttempt=0, lastAudioCheck=0, lastLed=0, lastButtonPoll=0;
uint32_t lastPlayingSeen=0;
uint32_t nextReconnect=0;
// The audio decoder runs in its own task. Take only atomic PCM statistics there.
std::atomic<uint32_t> pcmPeak{0};
uint8_t visualLevel=0;
bool recentBeat=false;
uint32_t lastBeat=0;

// Called on the decoded I2S output, after gain/volume. This is actual audio,
// NOT a random simulated pattern and does not use the microphones.
// NOTE: prepare-libs.ps1 removes an erroneous weak attribute in Audio.h,
// allowing this callback to override the weak no-op in Audio.cpp.
void audio_process_i2s(int32_t* samples, int16_t words, bool* continueI2S) {
  (void)continueI2S;
  if (!samples || words < 2) return;
  uint32_t localMax=0;
  uint64_t total=0;
  uint16_t count=0;
  // Stereo interleaved 32-bit words with signed samples in upper 16 bits.
  for (int i=0; i<words; i+=16) {
    int32_t v=samples[i] >> 16;
    uint32_t magnitude=(v<0) ? uint32_t(-v) : uint32_t(v);
    if (magnitude>localMax) localMax=magnitude;
    total+=magnitude;
    ++count;
  }
  uint32_t level=(uint32_t)(total / (count?count:1));
  level= max(level*2, localMax/3);
  if (level>32767) level=32767;
  uint32_t previous=pcmPeak.load(std::memory_order_relaxed);
  while (previous < level && !pcmPeak.compare_exchange_weak(previous,level,std::memory_order_relaxed)) {}
}

bool volumeNeedsSave=false;
uint32_t volumeChangedAt=0;

// I2C expander register operations. Its direction/output registers are per PORT, not per pin.
bool expWrite(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(HW::EXP);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission()==0;
}
int expRead(uint8_t reg) {
  Wire.beginTransmission(HW::EXP);
  Wire.write(reg);
  if (Wire.endTransmission(false)!=0) return -1;
  if (Wire.requestFrom((int)HW::EXP,1)!=1) return -1;
  return Wire.read();
}
void setAmp(bool enabled) {
  if (!expanderReady) return;
  // Change EXIO08 only; do not modify latches for the other TCA9555 pins.
  int output=expRead(HW::EXP_OUTPUT_PORT1);
  if (output<0) { Serial.println("ERROR: PA output read failed"); return; }
  const uint8_t desired=enabled ? (uint8_t(output)|0x01) : (uint8_t(output)&0xFE);
  if (!expWrite(HW::EXP_OUTPUT_PORT1,desired)) Serial.println("ERROR: PA output write failed");
}
void initExpander() {
  // EXIO08 is amp enable; EXIO09..11 are active-low user buttons.
  // Preserve other TCA9555 pins and NEVER drive EXIO06/07 (USB/camera mux).
  const int output=expRead(HW::EXP_OUTPUT_PORT1);
  const int config=expRead(HW::EXP_CONFIG_PORT1);
  expanderReady=(output>=0 && config>=0);
  if (expanderReady) {
    expanderReady=expWrite(HW::EXP_OUTPUT_PORT1,uint8_t(output)&0xFE) &&
                  expWrite(HW::EXP_CONFIG_PORT1,uint8_t(config)&0xFE);
  }
  if (!expanderReady) Serial.println("ERROR: TCA9555 expander not detected");
}

void loadFactoryStations() {
  for (int i=0;i<STATION_COUNT;i++) {
    stations[i]={kDefaultStations[i].name,kDefaultStations[i].genre,kDefaultStations[i].url};
  }
}
bool saveStations() {
  JsonDocument doc;
  JsonArray items=doc.to<JsonArray>();
  for (const auto &s:stations) {
    JsonObject o=items.add<JsonObject>();
    o["name"]=s.name; o["genre"]=s.genre; o["url"]=s.url;
  }
  File f=LittleFS.open("/stations.tmp","w");
  if(!f) return false;
  size_t bytes=serializeJson(doc,f);
  f.close();
  if (!bytes) { LittleFS.remove("/stations.tmp"); return false; }
  LittleFS.remove("/stations.json");
  return LittleFS.rename("/stations.tmp","/stations.json");
}
void loadStations() {
  loadFactoryStations();
  if(!LittleFS.exists("/stations.json")) return;
  File f=LittleFS.open("/stations.json","r");
  if (!f) return;
  JsonDocument doc;
  DeserializationError e=deserializeJson(doc,f);
  f.close();
  if (e || !doc.is<JsonArray>() || doc.as<JsonArray>().size()!=STATION_COUNT) {
    Serial.println("Stored station list invalid; using factory presets");
    return;
  }
  JsonArray arr=doc.as<JsonArray>();
  for(int i=0;i<STATION_COUNT;i++) {
    JsonObject s=arr[i];
    String n=s["name"].as<String>();
    String g=s["genre"].as<String>();
    String u=s["url"].as<String>();
    if(n.length() && u.startsWith("http") && u.length()<=300) stations[i]={n,g,u};
  }
}

void logAudio(Audio::msg_t m) {
  if (!m.msg) return;
  if (m.e == Audio::evt_streamtitle) {
    title=String(m.msg);
    if(title.length()>180) title.remove(180);
    Serial.printf("ICY title: %s\n",title.c_str());
  } else if (m.e == Audio::evt_name) {
    streamName=String(m.msg);
    Serial.printf("Stream name: %s\n",streamName.c_str());
  } else if (m.e == Audio::evt_info || m.e == Audio::evt_eof || m.e == Audio::evt_log) {
    Serial.printf("AUDIO: %s\n",m.msg);
    if(m.e==Audio::evt_eof && wantPlay) nextReconnect=millis()+2000;
  }
}

// Playback navigation honors the optional favorites-only and shuffle modes.
uint8_t nextStation(int direction) {
  uint8_t choices[STATION_COUNT]; uint8_t count=0;
  for (int i=0;i<STATION_COUNT;i++) {
    if (!favoritesOnly || (favoriteMask & (uint32_t(1)<<i))) choices[count++]=i;
  }
  if (count==0) { for(int i=0;i<STATION_COUNT;i++) choices[count++]=i; }
  if (shuffleMode && count>1) {
    uint8_t result=selected;
    for(int tries=0;tries<10 && result==selected;tries++) result=choices[esp_random()%count];
    if(result==selected) { for(int i=0;i<count;i++) if(choices[i]!=selected) return choices[i]; }
    return result;
  }
  for(int i=0;i<count;i++) if(choices[i]==selected) return choices[(i+count+(direction>0?1:-1))%count];
  return direction>0 ? choices[0] : choices[count-1];
}
void scheduleStation(uint8_t idx) {
  selected=idx%STATION_COUNT;
  prefs.putUChar("station",selected);
  usingCustom=false; customUrl="";
  title=""; streamName="";
  wantPlay=true;
  waitingForTune=true;
  nextReconnect=0;
  Serial.printf("Preset %u: %s\n",selected+1,stations[selected].name.c_str());
}
void setVolume(uint8_t v) {
  const uint8_t requested=constrain(int(v),0,21);
  if (volume==requested) return;
  volume=requested;
  if(!muted) audio.setVolume(fadeRunning ? min(volume,fadeVolume) : volume);
  // Defer NVS writes: holding a volume button must not write to flash 5x/sec.
  volumeNeedsSave=true;
  volumeChangedAt=millis();
}
void saveSettledVolume() {
  if (volumeNeedsSave && millis()-volumeChangedAt>=1500) {
    prefs.putUChar("volume",volume);
    volumeNeedsSave=false;
  }
}
void toggleMute() {
  muted=!muted;
  audio.setVolume(muted?0:(fadeRunning?min(volume,fadeVolume):volume));
}
void beginFade() {
  fadeRunning=fadeEnabled && !muted;
  fadeVolume=fadeRunning?0:volume;
  audio.setVolume(muted?0:fadeVolume);
  lastFadeStep=millis();
}
void serviceFade() {
  if(!fadeRunning || muted) return;
  if(millis()-lastFadeStep<65) return;
  lastFadeStep=millis();
  if(fadeVolume < volume) {fadeVolume++;audio.setVolume(fadeVolume);}
  if(fadeVolume>=volume) fadeRunning=false;
}
void togglePlayback() {
  wantPlay=!wantPlay;
  if(!wantPlay) {
    audio.stopSong();
    fadeRunning=false;
    waitingForTune=false;
    Serial.println("Radio stopped");
  } else {
    waitingForTune=true;
    nextReconnect=0;
    Serial.println("Radio resume requested");
  }
}

// All command handlers set state; the actual socket/decoder work runs in loop().
void jsonResponse(JsonDocument& doc, int code=200) {
  String payload;
  serializeJson(doc,payload);
  web.sendHeader("Cache-Control","no-store");
  web.send(code,"application/json",payload);
}
void errorResponse(int code,const char* message) {
  JsonDocument d; d["ok"]=false; d["error"]=message; jsonResponse(d,code);
}
void statusResponse() {
  JsonDocument d;
  d["version"]=FW_VERSION;
  d["wifiConnected"]=WiFi.status()==WL_CONNECTED;
  d["ssid"]=WiFi.status()==WL_CONNECTED ? WiFi.SSID() : String("");
  d["ip"]=WiFi.status()==WL_CONNECTED ? WiFi.localIP().toString():WiFi.softAPIP().toString();
  d["lanIP"]=WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String("");
  d["apIP"]=apActive?WiFi.softAPIP().toString():String("");
  d["apSSID"]=AP_SSID;d["keepAP"]=keepSetupAP;d["hostname"]=hostname;
  d["localUrl"]=String("http://")+hostname+".local/";
  d["favoriteMask"]=favoriteMask;d["favoritesOnly"]=favoritesOnly;d["shuffle"]=shuffleMode;
  d["muted"]=muted;d["fade"]=fadeEnabled;
  d["bootStation"]=bootStation;d["bootVolume"]=bootVolume;
  d["alarmEnabled"]=alarmEnabled;d["alarmHour"]=alarmHour;
  d["alarmMinute"]=alarmMinute;d["alarmStation"]=alarmStation;
  d["timezone"]=timezoneRule;
  d["sensitivity"]=ledSensitivity;d["speed"]=ledSpeed;d["hue"]=ledHueDegrees;
  d["quietHours"]=quietHours;d["quietStart"]=quietStart;d["quietEnd"]=quietEnd;
  d["sleepMinutes"]=sleepUntil && int32_t(sleepUntil-millis())>0? ((sleepUntil-millis()+59999)/60000):0;
  time_t rawClock=time(nullptr);
  if(rawClock>1700000000) {
    struct tm lt;localtime_r(&rawClock,&lt);char clockbuf[48];
    strftime(clockbuf,sizeof(clockbuf),"%a %b %d, %I:%M %p",&lt);
    d["localTime"]=clockbuf;
  } else d["localTime"]="Waiting for time sync";
  d["retries"]=streamRetries;d["lastError"]=lastStreamError;
  d["heapFree"]=ESP.getFreeHeap();d["psramFree"]=ESP.getFreePsram();
  d["setupAP"]=apActive;
  d["preset"]=selected+1;
  d["stationIndex"]=selected;
  d["name"]=usingCustom ? "Pi / custom stream" : stations[selected].name;
  d["genre"]=usingCustom ? "Local" : stations[selected].genre;
  d["url"]=usingCustom ? customUrl : stations[selected].url;
  d["title"]=title;
  d["streamName"]=streamName;
  d["playing"]=wantPlay;
  d["decoderRunning"]=audio.isRunning();
  d["volume"]=volume;
  d["codecReady"]=codecReady;
  d["ampExpanderReady"]=expanderReady;
  d["rssi"]=WiFi.status()==WL_CONNECTED ? WiFi.RSSI():0;
  d["uptimeSeconds"]=millis()/1000;
  d["ledMode"]=ledMode; d["ledBrightness"]=ledBrightness;
  d["audioLevel"]=visualLevel; d["beat"]=recentBeat;
  d["voiceEnabled"]=voiceEnabled;
  d["voiceReady"]=Voice::ready();
  d["voiceListening"]=Voice::listening();
  d["voiceAwake"]=Voice::awake();
  d["voiceFrames"]=Voice::frames();
  d["voiceReadFailures"]=Voice::readsFailed();
  d["voiceWakeCount"]=Voice::wakes();
  d["voiceCommandCount"]=Voice::recognizedCommands();
  d["micLeftLevel"]=Voice::micLeftLevel();
  d["micRightLevel"]=Voice::micRightLevel();
  d["micReg40"]=Voice::micAnalogReg();
  d["micReg4B"]=Voice::micPowerReg();
  d["micReg11"]=Voice::micFormatReg();
  d["micChannel"]=micChannel;
  d["voiceWakeWord"]="Computer";
  d["voiceError"]=Voice::error();
  d["micGain"]=micGain;
  d["lastVoiceCommand"]=lastVoiceCommand;
  d["mode"]=usingCustom?"custom":"station";
  jsonResponse(d);
}
void listStations() {
  JsonDocument d;
  JsonArray arr=d.to<JsonArray>();
  for(int i=0;i<STATION_COUNT;i++) {
    JsonObject s=arr.add<JsonObject>();
    s["id"]=i; s["preset"]=i+1;
    s["name"]=stations[i].name; s["genre"]=stations[i].genre;
    s["url"]=stations[i].url;
    s["favorite"]=bool(favoriteMask & (uint32_t(1)<<i));
  }
  jsonResponse(d);
}
bool parseIntegerArg(const char *name, int &out) {
  if(!web.hasArg(name)) return false;
  const String value=web.arg(name);
  if(value.isEmpty() || value.length()>4) return false;
  for(size_t i=0;i<value.length();i++) {
    if(value[i]<'0' || value[i]>'9') return false;
  }
  out=value.toInt();
  return true;
}
void playPreset() {
  int id;
  if(!parseIntegerArg("id",id)) return errorResponse(400,"Missing/invalid id (zero-based, 0..19)");
  if(id<0 || id>=STATION_COUNT) return errorResponse(400,"Preset must be 0..19");
  scheduleStation(id);
  statusResponse();
}
void toggleRoute() { togglePlayback(); statusResponse(); }
void volumeRoute() {
  int v;
  if(!parseIntegerArg("value",v)) return errorResponse(400,"Missing/invalid volume");
  if(v<0||v>21) return errorResponse(400,"Volume 0..21");
  setVolume(v);
  statusResponse();
}
void editPreset() {
  int i;
  if(!parseIntegerArg("id",i)) return errorResponse(400,"Missing/invalid id");
  String n=web.arg("name"),g=web.arg("genre"),u=web.arg("url");
  n.trim();g.trim();u.trim();
  if(i<0 || i>=STATION_COUNT || n.isEmpty() || n.length()>64 || g.length()>32 ||
     u.length()>300 || !(u.startsWith("https://") || u.startsWith("http://"))) {
    return errorResponse(400,"Invalid preset; name <=64, genre <=32, HTTP(S) URL <=300");
  }
  Station old=stations[i];
  stations[i]={n,g,u};
  if (!saveStations()) {
    stations[i]=old;
    return errorResponse(500,"Could not save station list to flash");
  }
  if(i==selected && !usingCustom && wantPlay) waitingForTune=true;
  JsonDocument d;d["ok"]=true;d["id"]=i;jsonResponse(d);
}
void customStreamRoute() {
  String url=web.arg("url"); url.trim();
  if(url.length()>300 || !(url.startsWith("http://")||url.startsWith("https://")))
    return errorResponse(400,"Expected HTTP(S) URL up to 300 characters");
  customUrl=url;usingCustom=true;title="";streamName="";
  wantPlay=true;waitingForTune=true;nextReconnect=0;
  statusResponse();
}
void wifiRoute() {
  String s=web.arg("ssid"),p=web.arg("password");
  s.trim();
  if(s.length()<1 || s.length()>32 || p.length()>64) return errorResponse(400,"Invalid Wi-Fi name/password");
  wifiSsid=s;wifiPass=p;
  prefs.putString("ssid",wifiSsid);
  prefs.putString("pass",wifiPass);
  JsonDocument d;d["ok"]=true;d["message"]="Saved; radio will reconnect";jsonResponse(d);
  // Connection is attempted later in the main loop so response can finish first.
  lastWifiAttempt=0;
  WiFi.disconnect(false,false);
}
void lightsRoute() {
  int m, b;
  if (parseIntegerArg("mode",m)) {
    if (m<0 || m>6) return errorResponse(400,"LED mode must be 0..6");
    ledMode=(uint8_t)m;
    prefs.putUChar("ledmode",ledMode);
  }
  if (parseIntegerArg("brightness",b)) {
    if (b<10 || b>180) return errorResponse(400,"Brightness must be 10..180");
    ledBrightness=(uint8_t)b;
    prefs.putUChar("ledbright",ledBrightness);
  }
  int x;
  if (parseIntegerArg("sensitivity",x)) {
    if(x>100)return errorResponse(400,"Sensitivity must be 0..100");
    ledSensitivity=x;prefs.putUChar("sense",ledSensitivity);
  }
  if (parseIntegerArg("speed",x)) {
    if(x>100)return errorResponse(400,"Speed must be 0..100");
    ledSpeed=x;prefs.putUChar("speed",ledSpeed);
  }
  if (parseIntegerArg("hue",x)) {
    if(x>255)return errorResponse(400,"Hue must be 0..255");
    ledHueDegrees=x;prefs.putUChar("hue",ledHueDegrees);
  }
  if (parseIntegerArg("quiet",x)) {quietHours=(x==1);prefs.putBool("quiet",quietHours);}
  if (parseIntegerArg("quietStart",x)) {
    if(x>23)return errorResponse(400,"Quiet start must be 0..23");
    quietStart=x;prefs.putUChar("qstart",x);
  }
  if (parseIntegerArg("quietEnd",x)) {
    if(x>23)return errorResponse(400,"Quiet end must be 0..23");
    quietEnd=x;prefs.putUChar("qend",x);
  }
  statusResponse();
}

void scanRoute() {
  int n=WiFi.scanComplete();
  if(n==-2) {WiFi.scanNetworks(true,false);n=-1;}
  JsonDocument d;d["scanning"]=(n<0);
  JsonArray arr=d["networks"].to<JsonArray>();
  if(n>=0) {
    for(int i=0;i<n && i<30;i++) {
      JsonObject item=arr.add<JsonObject>();
      item["ssid"]=WiFi.SSID(i);item["rssi"]=WiFi.RSSI(i);
      item["encrypted"]=WiFi.encryptionType(i)!=WIFI_AUTH_OPEN;
    }
    WiFi.scanDelete();
  }
  jsonResponse(d);
}
void navigationRoute() {
  int x;
  if(parseIntegerArg("favoritesOnly",x)) {favoritesOnly=(x==1);prefs.putBool("favonly",favoritesOnly);}
  if(parseIntegerArg("shuffle",x)) {shuffleMode=(x==1);prefs.putBool("shuffle",shuffleMode);}
  if(parseIntegerArg("step",x)) {if(x>1)return errorResponse(400,"Step 0=previous, 1=next"); scheduleStation(nextStation(x==1?1:-1));}
  statusResponse();
}
void favoriteRoute() {
  int i;
  if(!parseIntegerArg("id",i)||i<0||i>=STATION_COUNT) return errorResponse(400,"Invalid station");
  favoriteMask ^= (uint32_t(1)<<i);
  prefs.putUInt("favorites",favoriteMask);
  statusResponse();
}
void moveStationRoute() {
  int i,down;
  if(!parseIntegerArg("id",i)||!parseIntegerArg("down",down) ||i<0||i>=STATION_COUNT || down>1)
    return errorResponse(400,"Bad station position");
  int j=i+(down?1:-1);
  if(j<0||j>=STATION_COUNT) return errorResponse(400,"Cannot move beyond list");
  Station tmp=stations[i];stations[i]=stations[j];stations[j]=tmp;
  uint32_t b1=(favoriteMask>>i)&1U,b2=(favoriteMask>>j)&1U;
  favoriteMask &= ~((uint32_t(1)<<i)|(uint32_t(1)<<j));
  favoriteMask |= (b1<<j)|(b2<<i);
  if(!saveStations()) {tmp=stations[i];stations[i]=stations[j];stations[j]=tmp;return errorResponse(500,"Could not save ordering");}
  prefs.putUInt("favorites",favoriteMask);
  if(selected==i) selected=j;else if(selected==j) selected=i;
  statusResponse();
}
void restoreRoute() {
  int id;
  if(web.arg("id")=="-1") id=-1;
  else if(!parseIntegerArg("id",id)) return errorResponse(400,"Invalid preset id");
  if(id<-1 || id>=STATION_COUNT) return errorResponse(400,"Invalid preset id");
  if(id==-1) loadFactoryStations();
  else stations[id]={kDefaultStations[id].name,kDefaultStations[id].genre,kDefaultStations[id].url};
  if(!saveStations()) return errorResponse(500,"Could not save restored presets");
  waitingForTune=wantPlay;
  statusResponse();
}
void exportRoute() {
  JsonDocument d;JsonArray arr=d.to<JsonArray>();
  for(int i=0;i<STATION_COUNT;i++) {
    JsonObject item=arr.add<JsonObject>();
    item["name"]=stations[i].name;item["genre"]=stations[i].genre;item["url"]=stations[i].url;
  }
  String json;serializeJsonPretty(d,json);
  web.sendHeader("Content-Disposition","attachment; filename=party-radio-20-presets.json");
  web.send(200,"application/json",json);
}
void importRoute() {
  String payload=web.arg("payload");
  if(payload.length()<10||payload.length()>18000) return errorResponse(400,"Import file too small or large");
  JsonDocument doc;
  if(deserializeJson(doc,payload)||!doc.is<JsonArray>()||doc.as<JsonArray>().size()!=STATION_COUNT)
    return errorResponse(400,"Import must contain exactly 20 station records");
  Station incoming[STATION_COUNT];JsonArray arr=doc.as<JsonArray>();
  for(int i=0;i<STATION_COUNT;i++) {
    JsonObject x=arr[i];
    incoming[i]={x["name"].as<String>(),x["genre"].as<String>(),x["url"].as<String>()};
    if(incoming[i].name.isEmpty()||incoming[i].name.length()>64||incoming[i].genre.length()>32||
       incoming[i].url.length()>300||!(incoming[i].url.startsWith("https://")||incoming[i].url.startsWith("http://")))
      return errorResponse(400,"Import contains a malformed station");
  }
  Station previous[STATION_COUNT];
  for(int i=0;i<STATION_COUNT;i++) {previous[i]=stations[i];stations[i]=incoming[i];}
  if(!saveStations()) {for(int i=0;i<STATION_COUNT;i++) stations[i]=previous[i];return errorResponse(500,"Failed to save presets");}
  waitingForTune=wantPlay;
  statusResponse();
}
void bootRoute() {
  int x;
  if(parseIntegerArg("station",x)) {
    if(x>=STATION_COUNT)return errorResponse(400,"Boot station 0..19");
    bootStation=x;prefs.putUChar("bootst",x);
  }
  if(parseIntegerArg("volume",x)) {
    if(x>21)return errorResponse(400,"Boot volume 0..21");
    bootVolume=x;prefs.putUChar("bootvol",x);
  }
  if(parseIntegerArg("fade",x)) {fadeEnabled=(x==1);prefs.putBool("fade",fadeEnabled);}
  statusResponse();
}
void muteRoute() {toggleMute();statusResponse();}
void sleepRoute() {
  int m;
  if(!parseIntegerArg("minutes",m)||m>240)return errorResponse(400,"Timer range 0..240 minutes");
  sleepUntil=m?millis()+uint32_t(m)*60000U:0;
  statusResponse();
}
void alarmRoute() {
  int x;
  if(parseIntegerArg("enabled",x)) {alarmEnabled=(x==1);prefs.putBool("alarm",alarmEnabled);}
  if(parseIntegerArg("hour",x)) {if(x>23)return errorResponse(400,"Hour 0..23");alarmHour=x;prefs.putUChar("alhour",x);}
  if(parseIntegerArg("minute",x)) {if(x>59)return errorResponse(400,"Minute 0..59");alarmMinute=x;prefs.putUChar("almin",x);}
  if(parseIntegerArg("station",x)) {if(x>=STATION_COUNT)return errorResponse(400,"Station 0..19");alarmStation=x;prefs.putUChar("alstat",x);}
  if(web.hasArg("timezone")) {
    String tz=web.arg("timezone");tz.trim();
    // POSIX timezones contain only a small ASCII punctuation alphabet.
    if(tz.isEmpty()||tz.length()>64) return errorResponse(400,"Invalid timezone");
    for(size_t j=0;j<tz.length();j++) if(!isAlphaNumeric(tz[j]) && strchr("+-,./:",tz[j])==nullptr)
      return errorResponse(400,"Unsupported timezone characters");
    timezoneRule=tz;prefs.putString("timezone",timezoneRule);
    if(WiFi.status()==WL_CONNECTED) configTzTime(timezoneRule.c_str(),"pool.ntp.org","time.nist.gov");
  }
  statusResponse();
}
void controllerRoute() {
  int x;bool apChange=false,mdnsChange=false;
  if(parseIntegerArg("keepAP",x)) {keepSetupAP=(x==1);prefs.putBool("keepap",keepSetupAP);apChange=true;}
  if(web.hasArg("password")&&!web.arg("password").isEmpty()) {
    String pw=web.arg("password");
    if(pw.length()<8||pw.length()>63)return errorResponse(400,"AP password must be 8..63 chars");
    apPassword=pw;prefs.putString("ap_pass",apPassword);apChange=true;
  }
  if(web.hasArg("hostname")) {
    String host=web.arg("hostname");host.trim();host.toLowerCase();
    if(host.length()<3||host.length()>24||host[0]=='-'||host[host.length()-1]=='-')return errorResponse(400,"Invalid hostname");
    for(size_t j=0;j<host.length();j++) if(!isAlphaNumeric(host[j])&&host[j]!='-') return errorResponse(400,"Hostname letters/numbers/hyphen only");
    if(host!=hostname) {hostname=host;prefs.putString("hostname",hostname);mdnsChange=true;}
  }
  statusResponse();
  if(apChange) restartAPAt=millis()+1800;
  if(mdnsChange) {
    if(mdnsActive) {MDNS.end();mdnsActive=false;}
    if(WiFi.status()==WL_CONNECTED) {mdnsActive=MDNS.begin(hostname.c_str());if(mdnsActive) MDNS.addService("http","tcp",80);}
  }
}
void voiceRoute() {
  int x;
  if(parseIntegerArg("enabled",x)) {
    if(x>1) return errorResponse(400,"Voice enabled must be 0 or 1");
    voiceEnabled=(x==1);prefs.putBool("voice",voiceEnabled);
    // When initialization failed at startup, merely changing SR mode cannot
    // start a nonexistent speech task. Schedule a reboot to retry model/I2S init.
    if(voiceEnabled && !Voice::ready()) rebootAt=millis()+2500;
    else Voice::setEnabled(voiceEnabled);
  }
  if(parseIntegerArg("channel",x)) {
    if(x>2)return errorResponse(400,"Mic channel 0=Auto, 1=Left, 2=Right");
    if(micChannel!=x) {micChannel=x;prefs.putUChar("micchannel",x);rebootAt=millis()+2500;}
  }
  if(parseIntegerArg("gain",x)) {
    if(x>14)return errorResponse(400,"ES7210 mic gain 0..14");
    micGain=x;prefs.putUChar("micgain",micGain);
    // Codec's analog gain changes require a reboot to avoid I2S / SR task races.
    rebootAt=millis()+2500;
  }
  statusResponse();
}
void runVoiceCommand(int cmd) {
  if(cmd==Voice::NONE) return;
  lastVoiceCommand=String("Command ")+String(cmd);
  Serial.printf("COMPUTER voice command %d\n",cmd);
  switch(cmd) {
    case Voice::NEXT: scheduleStation(nextStation(1)); break;
    case Voice::PREVIOUS: scheduleStation(nextStation(-1)); break;
    case Voice::PLAY: if(!wantPlay)togglePlayback(); break;
    case Voice::STOP: if(wantPlay)togglePlayback(); break;
    case Voice::VOL_UP: setVolume(constrain(int(volume)+2,0,21));break;
    case Voice::VOL_DOWN: setVolume(constrain(int(volume)-2,0,21));break;
    case Voice::MUTE: toggleMute();break;
    case Voice::ALTERNATIVE: case Voice::PLAY_STATION_ONE: scheduleStation(0);break;
    case Voice::GRUNGE: scheduleStation(1);break;
    case Voice::ROCK: scheduleStation(3);break;
    case Voice::COUNTRY: scheduleStation(8);break;
    case Voice::ELECTRONIC: scheduleStation(10);break;
    case Voice::NEWS: scheduleStation(15);break;
    case Voice::WEATHER: scheduleStation(18);break;
    case Voice::PARTY_LIGHTS:ledMode=0;prefs.putUChar("ledmode",0);break;
    case Voice::RAINBOW_LIGHTS:ledMode=2;prefs.putUChar("ledmode",2);break;
    case Voice::LIGHTS_OFF:ledMode=3;prefs.putUChar("ledmode",3);break;
    case Voice::BRIGHTER_LIGHTS:ledBrightness=constrain(int(ledBrightness)+20,10,180);prefs.putUChar("ledbright",ledBrightness);break;
    case Voice::DIMMER_LIGHTS:ledBrightness=constrain(int(ledBrightness)-20,10,180);prefs.putUChar("ledbright",ledBrightness);break;
    case Voice::FAVORITES:favoritesOnly=!favoritesOnly;prefs.putBool("favonly",favoritesOnly);break;
    case Voice::SHUFFLE:shuffleMode=!shuffleMode;prefs.putBool("shuffle",shuffleMode);break;
    case Voice::SLEEP_30:sleepUntil=millis()+30U*60000U;break;
    case Voice::SLEEP_60:sleepUntil=millis()+60U*60000U;break;
    default:break;
  }
}
void serviceVoice() {
  const int command=Voice::takeCommand();
  if(command!=Voice::NONE)runVoiceCommand(command);
  const bool listening=voiceEnabled&&Voice::awake();
  if(listening && !voiceDucking) {
    voiceDucking=true;voiceDuckTime=millis();
    if(!muted) audio.setVolume(min(int(volume),3));
  }
  if(voiceDucking && (!listening || millis()-voiceDuckTime>10000)) {
    voiceDucking=false;
    if(!muted)audio.setVolume(fadeRunning?min(volume,fadeVolume):volume);
  }
}

void rebootRoute() {JsonDocument d;d["ok"]=true;d["message"]="Reboot scheduled";jsonResponse(d);rebootAt=millis()+1400;}
void setupServer() {
  web.on("/",HTTP_GET,[]{web.send_P(200,"text/html; charset=utf-8",kWebUi);});
  web.on("/api/status",HTTP_GET,statusResponse);
  web.on("/api/stations",HTTP_GET,listStations);
  web.on("/api/play",HTTP_POST,playPreset);
  web.on("/api/toggle",HTTP_POST,toggleRoute);
  web.on("/api/volume",HTTP_POST,volumeRoute);
  web.on("/api/preset",HTTP_POST,editPreset);
  web.on("/api/wifi",HTTP_POST,wifiRoute);
  web.on("/api/stream",HTTP_POST,customStreamRoute);
  web.on("/api/lights",HTTP_POST,lightsRoute);
  web.on("/api/scan",HTTP_GET,scanRoute);
  web.on("/api/navigation",HTTP_POST,navigationRoute);
  web.on("/api/favorite",HTTP_POST,favoriteRoute);
  web.on("/api/move",HTTP_POST,moveStationRoute);
  web.on("/api/restore",HTTP_POST,restoreRoute);
  web.on("/api/stations/export",HTTP_GET,exportRoute);
  web.on("/api/stations/import",HTTP_POST,importRoute);
  web.on("/api/boot",HTTP_POST,bootRoute);
  web.on("/api/mute",HTTP_POST,muteRoute);
  web.on("/api/sleep",HTTP_POST,sleepRoute);
  web.on("/api/alarm",HTTP_POST,alarmRoute);
  web.on("/api/controller",HTTP_POST,controllerRoute);
  web.on("/api/reboot",HTTP_POST,rebootRoute);
  web.on("/api/voice",HTTP_POST,voiceRoute);
  web.on("/health",HTTP_GET,[]{web.send(200,"text/plain","OK RadioVault");});
  web.onNotFound([](){errorResponse(404,"Not found");});
  web.begin();serverReady=true;
  Serial.println("HTTP controller ready: / and /api/status");
}
void startSetupAP() {
  if(apActive) return;
  WiFi.mode(WIFI_AP_STA);
  if(WiFi.softAP(AP_SSID,apPassword.c_str(),6,false,3)) {
    apActive=true;
    Serial.printf("Local controller AP: %s / http://%s\n",AP_SSID,WiFi.softAPIP().toString().c_str());
  } else Serial.println("ERROR: setup AP could not start");
}
void stopSetupAP() {
  if(!apActive) return;
  WiFi.softAPdisconnect(true);apActive=false;
  WiFi.mode(WIFI_STA);
}
void onWifiReady() {
  if(!keepSetupAP) stopSetupAP();
  Serial.printf("Wi-Fi connected: %s / %s\n",WiFi.SSID().c_str(),WiFi.localIP().toString().c_str());
  if(!mdnsActive) {
    mdnsActive=MDNS.begin(hostname.c_str());
    if(mdnsActive) MDNS.addService("http","tcp",80);
  }
  configTzTime(timezoneRule.c_str(),"pool.ntp.org","time.nist.gov");
  if(wantPlay) waitingForTune=true;
}
void checkNetwork() {
  const uint32_t now=millis();
  static bool connected=false;
  bool online=WiFi.status()==WL_CONNECTED;
  if(online && !connected) {connected=true;onWifiReady();}
  if(!online && connected) {
    connected=false;mdnsActive=false;MDNS.end();
    Serial.println("Wi-Fi disconnected. Internet radio waits for Wi-Fi.");
    if(wantPlay) waitingForTune=true;
  }
  if(online) {if(keepSetupAP && !apActive) startSetupAP();return;}
  if(wifiSsid.isEmpty()) {startSetupAP();return;}
  if(now-lastWifiAttempt>15000 || lastWifiAttempt==0) {
    if(lastWifiAttempt!=0 || keepSetupAP) startSetupAP(); // always-on controller or recovery portal
    lastWifiAttempt=now;
    Serial.printf("Connecting to Wi-Fi %s\n",wifiSsid.c_str());
    WiFi.mode(apActive?WIFI_AP_STA:WIFI_STA);
    WiFi.begin(wifiSsid.c_str(),wifiPass.c_str());
  }
}
void serviceAudio() {
  if(!wantPlay || WiFi.status()!=WL_CONNECTED || !codecReady || !expanderReady) return;
  uint32_t now=millis();
  if(waitingForTune && (nextReconnect==0 || int32_t(now-nextReconnect)>=0)) {
    waitingForTune=false;
    lastAttempt=now;
    lastPlayingSeen=now;
    audio.stopSong();
    beginFade();
    const String &url=usingCustom?customUrl:stations[selected].url;
    if(!audio.connecttohost(url.c_str())) {
      Serial.printf("Unable to connect to stream: %s\n",url.c_str());
      streamRetries++;lastStreamError="Connection failed: "+url;
      waitingForTune=true;
      nextReconnect=millis()+10000;
    } else {
      Serial.printf("Stream request: %s\n",url.c_str());
    }
  }
  if(now-lastAudioCheck<1500) return;
  lastAudioCheck=now;
  if(audio.isRunning()) {
    lastPlayingSeen=now;
  } else if(!waitingForTune && now-lastAttempt>12000) {
    waitingForTune=true;
    streamRetries++;lastStreamError="Decoder stalled";
    nextReconnect=now+1000;
    Serial.println("Decoder stopped; retrying stream");
  }
}

struct KeyState {
  bool raw=false,stable=false,longHandled=false;
  uint32_t changed=0,downAt=0,repeatAt=0;
} keys[3];
void keysPoll() {
  if(!expanderReady || millis()-lastButtonPoll<25) return;
  lastButtonPoll=millis();
  int r=expRead(HW::EXP_INPUT_PORT1);
  if(r<0) return;
  uint32_t now=millis();
  for(int i=0;i<3;i++) {
    bool pressed=!(r & HW::KEY_MASK[i]);
    KeyState &k=keys[i];
    if(k.raw!=pressed) {k.raw=pressed;k.changed=now;}
    if(now-k.changed>=45 && k.stable!=k.raw) {
      k.stable=k.raw;
      if(k.stable) {k.downAt=now;k.repeatAt=now;k.longHandled=false;}
      else if(!k.longHandled) {
        if(i==0) scheduleStation(nextStation(-1));
        if(i==1) togglePlayback();
        if(i==2) scheduleStation(nextStation(1));
      }
    }
    if(k.stable && i!=1 && now-k.downAt>750 && now-k.repeatAt>220) {
      k.longHandled=true;k.repeatAt=now;
      setVolume(constrain(int(volume)+(i==0?-1:1),0,21));
    }
  }
}
// 7 LED ring: the PCM loudness moves a visible bar, beats trigger color jumps
// and a brief flash, and a continuously rotating rainbow animates the background.
// The algorithm is nonblocking and leaves audio streaming on its own task.
void updateLeds() {
  const uint32_t now=millis();
  if(now-lastLed<33) return;    // ~30fps, 7 LEDs take < 1ms to show
  const float elapsed= float(now-lastLed);
  lastLed=now;
  pixels.setBrightness(ledBrightness);
  if(quietHours && quietStart!=quietEnd) {
    time_t clock=time(nullptr);
    if(clock>1700000000) {
      struct tm t;localtime_r(&clock,&t);
      bool hidden=quietStart<quietEnd ? (t.tm_hour>=quietStart && t.tm_hour<quietEnd)
                                       : (t.tm_hour>=quietStart || t.tm_hour<quietEnd);
      if(hidden) {pixels.clear();pixels.show();return;}
    }
  }
  const bool ready=codecReady && expanderReady;
  const bool online=WiFi.status()==WL_CONNECTED;
  if (!ready || !online || !wantPlay || !audio.isRunning()) {
    pcmPeak.exchange(0,std::memory_order_relaxed);
    visualLevel=0; recentBeat=false;
    if(ledMode==3 && ready && online) {pixels.clear(); pixels.show(); return;}
    const float wave=0.4f+0.6f*(0.5f+0.5f*sinf(now*0.004f));
    const uint8_t b=uint8_t(12+55*wave);
    const uint32_t c=!ready?pixels.Color(b,0,0):!online?pixels.Color(b/2,0,b):!wantPlay?pixels.Color(0,0,b):pixels.Color(b/2,b/3,0);
    pixels.fill(c); pixels.show(); return;
  }
  if(voiceEnabled && Voice::awake()) {
    uint8_t b=uint8_t(70+50*(1+sinf(float(now)*0.015f)));
    pixels.fill(pixels.Color(0,b,255));pixels.show();return;
  }
  const uint32_t raw=pcmPeak.exchange(0,std::memory_order_relaxed);
  static float baseline=900.0f;
  static float smooth=0.0f;
  static uint16_t hue=0;
  static uint8_t flash=0;
  // Adapt to a quiet talk station as well as loud rock. Avoid blowing up on silence.
  const float sensitivityFactor=3.2f-2.5f*(float(ledSensitivity)/100.0f);
  const float normalized=fminf(1.0f, float(raw)/fmaxf(350.0f,baseline*sensitivityFactor));
  smooth=0.55f*smooth+0.45f*normalized;
  recentBeat=(raw>450 && raw>baseline*(2.1f-0.9f*float(ledSensitivity)/100.0f) && now-lastBeat>175);
  if (recentBeat) {lastBeat=now; hue+=9753; flash=220;}
  baseline=0.965f*baseline+0.035f*float(raw);
  if(baseline<150.0f) baseline=150.0f;
  if(flash>17) flash-=17; else flash=0;
  const float speedFactor=0.25f+2.75f*float(ledSpeed)/100.0f;
  hue+= uint16_t(speedFactor*(70+750.0f*smooth));
  const int lit=constrain(int(ceilf(smooth*7.0f)),0,7);
  visualLevel=(uint8_t)constrain(int(smooth*255.0f),0,255);
  if(ledMode==3) {pixels.clear();pixels.show();return;}
  for(int i=0;i<HW::PIXEL_COUNT;i++) {
    // Rotate visual motion while maintaining music-driven intensity.
    const int moving=(i+int(float(now)*speedFactor/135.0f))%HW::PIXEL_COUNT;
    int value=0;
    uint16_t pxHue=hue+uint16_t(i*9300);
    if(ledMode==0) {  // PARTY: rainbow bounce, with transients flashing toward white
      const bool bar=moving<lit;
      value=bar ? (95+int(smooth*135)) : (9+int(smooth*25));
      value=constrain(value + int(flash)*int(bar)/3,0,255);
    } else if (ledMode==1) { // VU: brightly filled segments and dark remaining segments
      value=moving<lit ? 220 : 3;
      pxHue=uint16_t((i*9000)+(smooth*10000));
    } else if(ledMode==2) { // rainbow
      value=50+int(smooth*145);pxHue=uint16_t(hue+i*9300);
    } else if(ledMode==4) { // sparkles, triggered by music, not random pulsing
      value=((i+int(now*speedFactor/90))%7==0 && raw>350) ? 240 : int(6+smooth*45);
      pxHue=uint16_t(uint32_t(ledHueDegrees)*257U+i*4200);
    } else if(ledMode==5) { // breathing pulse with music-synchronized intensity
      float breathe=0.5f+0.5f*sinf(float(now)*0.002f*speedFactor);
      value=constrain(int(20+145*breathe+80*smooth),0,255);
      pxHue=uint16_t(uint32_t(ledHueDegrees)*257U);
    } else { // solid color, responds to volume and beats
      value=constrain(int(14+smooth*220+flash/4),0,255);
      pxHue=uint16_t(uint32_t(ledHueDegrees)*257U);
    }
    uint32_t color=pixels.ColorHSV(pxHue,255,(uint8_t)value);
    if(ledMode==0 && flash>140 && moving<lit) color=pixels.Color(255,255,255);
    pixels.setPixelColor(i,pixels.gamma32(color));
  }
  pixels.show();
}

void setup() {
  Serial.begin(115200);
  delay(600);
  Serial.println("\n================ Waveshare Party Radio 20 ================");
  Serial.printf("Free PSRAM: %u bytes\n",(unsigned)ESP.getFreePsram());
  prefs.begin("radiovault",false);
  wifiSsid=prefs.getString("ssid","");
  wifiPass=prefs.getString("pass","");
  // Startup preset defaults to alternative rock, user can change it in web settings.
  bootStation=constrain(int(prefs.getUChar("bootst",0)),0,STATION_COUNT-1);
  bootVolume=constrain(int(prefs.getUChar("bootvol",12)),0,21);
  selected=bootStation;
  volume=bootVolume;
  keepSetupAP=prefs.getBool("keepap",true);
  apPassword=prefs.getString("ap_pass",AP_PASS);
  if(apPassword.length()<8 || apPassword.length()>63) apPassword=AP_PASS;
  hostname=prefs.getString("hostname","waveshare-radio");
  favoriteMask=prefs.getUInt("favorites",0);
  favoritesOnly=prefs.getBool("favonly",false);shuffleMode=prefs.getBool("shuffle",false);
  fadeEnabled=prefs.getBool("fade",true);
  voiceEnabled=prefs.getBool("voice",true);micGain=prefs.getUChar("micgain",8);
  micChannel=constrain(int(prefs.getUChar("micchannel",0)),0,2);
  alarmEnabled=prefs.getBool("alarm",false);
  alarmHour=prefs.getUChar("alhour",7);alarmMinute=prefs.getUChar("almin",0);
  alarmStation=constrain(int(prefs.getUChar("alstat",0)),0,STATION_COUNT-1);
  timezoneRule=prefs.getString("timezone",timezoneRule);
  ledSensitivity=prefs.getUChar("sense",55);ledSpeed=prefs.getUChar("speed",50);
  ledHueDegrees=prefs.getUChar("hue",200);
  quietHours=prefs.getBool("quiet",false);quietStart=prefs.getUChar("qstart",23);
  quietEnd=prefs.getUChar("qend",7);
  ledMode=constrain(int(prefs.getUChar("ledmode",0)),0,6);
  ledBrightness=constrain(int(prefs.getUChar("ledbright",90)),10,180);

  pixels.begin();pixels.setBrightness(ledBrightness);pixels.clear();pixels.show();
  if(!LittleFS.begin(true)) Serial.println("WARNING: LittleFS unavailable; custom presets won't save");
  loadStations();

  // The reference ES8311 driver initializes the same Wire bus used by the expander.
  codecReady=codec.begin(HW::SDA,HW::SCL,100000);
  if(codecReady) {
    codec.setBitsPerSample(16);
    codec.setSampleRate(48000);
    codec.setVolume(75); // physical DAC output; final listener volume is audio.setVolume()
  } else Serial.println("ERROR: ES8311 codec not detected. Check board model.");
  initExpander();

  Audio::audio_info_callback=logAudio;
  audio.setOutputSampleRate(Audio::SR_48000); // codec MCLK and audio sample rate agree
  audio.setPinout(HW::I2S_BCLK,HW::I2S_LRCLK,HW::I2S_DOUT,HW::I2S_MCLK);
  audio.forceMono(true);
  audio.setVolume(volume);
  audio.setConnectionTimeout(5000,7000);
  setAmp(true);
  // Always try on-device speech; failure leaves internet radio fully usable.
  Voice::begin(voiceEnabled,micGain,micChannel);

  WiFi.mode(keepSetupAP?WIFI_AP_STA:WIFI_STA);
  if(keepSetupAP) startSetupAP();
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  if(wifiSsid.length()) {WiFi.begin(wifiSsid.c_str(),wifiPass.c_str());lastWifiAttempt=millis();}
  else startSetupAP();
  setupServer();
  Serial.printf("Auto-starting music on boot, preset %u/%u: %s\n",selected+1,STATION_COUNT,stations[selected].name.c_str());
  Serial.printf("Radio web: http://%s.local/ or controller AP: http://192.168.4.1/\n",hostname.c_str());
}
void serviceTimers() {
  uint32_t now=millis();
  if(rebootAt && int32_t(now-rebootAt)>=0) ESP.restart();
  if(restartAPAt && int32_t(now-restartAPAt)>=0) {
    restartAPAt=0;
    if(apActive) {WiFi.softAPdisconnect(true);apActive=false;}
    if(keepSetupAP || WiFi.status()!=WL_CONNECTED) startSetupAP();
    else WiFi.mode(WIFI_STA);
  }
  if(sleepUntil && int32_t(now-sleepUntil)>=0) {
    sleepUntil=0;
    if(wantPlay) togglePlayback();
  }
  if(now-lastClockService<900) return;
  lastClockService=now;
  if(!alarmEnabled || WiFi.status()!=WL_CONNECTED) return;
  time_t clock=time(nullptr);
  if(clock<1700000000) return;
  struct tm t;localtime_r(&clock,&t);
  int today=(t.tm_year+1900)*400+t.tm_yday;
  if(t.tm_hour==alarmHour && t.tm_min==alarmMinute && today!=lastAlarmDate) {
    lastAlarmDate=today;
    sleepUntil=0;
    scheduleStation(alarmStation);
    Serial.println("Daily alarm: playing configured station");
  }
}
void loop() {
  // Audio loop must be called frequently; do not use delay() for network retry.
  audio.loop();
  web.handleClient();
  keysPoll();
  checkNetwork();
  serviceAudio();
  serviceVoice();
  serviceFade();
  serviceTimers();
  saveSettledVolume();
  updateLeds();
  vTaskDelay(1);
}
