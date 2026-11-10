#pragma once
// Entire controller lives in firmware: no cloud, Pi, or PC server needed.
static const char kWebUi[] PROGMEM = R"RVWEB(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Party Radio Pro | Waveshare</title><style>
:root{color-scheme:dark;font-family:system-ui,-apple-system,Segoe UI,Arial,sans-serif}*{box-sizing:border-box}
body{margin:0;background:#0a1020;color:#e6edfc}main{max-width:1050px;margin:0 auto;padding:20px}
header,.row,.between{display:flex;align-items:center;gap:10px;flex-wrap:wrap}.between{justify-content:space-between}
header{justify-content:space-between;margin-bottom:16px}h1{font-size:29px;letter-spacing:-1px;margin:0}h2{font-size:17px;margin:0 0 11px}p{margin:8px 0}
small,.dim{color:#a4b4d1}.hint{font-size:12px;color:#9ab0d1}.tag{font-size:12px;color:#b6d7ff;border:1px solid #365f95;padding:4px 8px;border-radius:99px}
.columns{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}.panel{background:#111b31;border:1px solid #283b58;border-radius:17px;padding:18px;margin:0 0 14px;min-width:0}
button,input,select{font:inherit}button{background:#203555;border:1px solid #3f5c80;border-radius:11px;color:#edf2ff;padding:10px 13px;cursor:pointer}
button:hover{background:#2b4b75}button:disabled{opacity:.4;cursor:default}.primary{background:#3268eb;border-color:#5e8aff}.danger{background:#562937;border-color:#9b415a}
input,select{width:100%;background:#0b162a;border:1px solid #435977;border-radius:9px;color:#fff;padding:10px}input[type=checkbox]{width:auto;accent-color:#5b8fff}
input[type=range]{padding:0;accent-color:#6da4ff}.field{margin:10px 0}.field label{display:block;color:#bfd1ea;font-size:13px;margin:0 0 6px}
.field.inline{display:flex;align-items:center;gap:9px}.field.inline label{margin:0}.field.inline input{width:auto}
.now{font-weight:800;font-size:24px;margin:5px 0 9px;overflow-wrap:anywhere}.sub{color:#a5c1f1;font-size:13px}
.row>button{flex:0 1 auto}.gridbuttons{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:8px}.gridbuttons button{font-size:13px}
.station{display:flex;gap:6px;align-items:center;padding:7px 2px;border-bottom:1px solid #263751}.station.active{background:#1b3358}
.station .identity{flex:1;min-width:0}.station strong{display:block;font-size:13px;overflow-wrap:anywhere}.station small{display:block;font-size:11px}
.station button{padding:7px 9px}.station .heart{font-size:18px}.station .heart.on{color:#ff789e}
.statrow{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:7px;font-size:12px}.statrow span{word-break:break-word}
#toast{position:sticky;bottom:8px;z-index:3;background:#244269;color:white;padding:12px;border-radius:9px;display:none}details summary{cursor:pointer;padding:9px;font-weight:600}
.wide{grid-column:span 2}a{color:#98c1ff}audio{width:100%}
@media(max-width:720px){main{padding:10px}.columns{display:block}.wide{grid-column:auto}.station button{padding:7px}.row>button{flex:1}}
</style></head><body><main>
<header><div><h1>✦ Party Radio Pro</h1><small>WAVESHARE ESP32-S3 • 20 STATION CONTROLLER</small></div><span class="tag" id="connection">Connecting...</span></header>
<div class="columns">
<section class="panel"><h2>Now playing</h2><div class="sub" id="stationName">Loading...</div><div class="now" id="trackTitle">Internet Radio</div>
<div class="row"><button id="prev">⏮ Previous</button><button id="toggle" class="primary">Stop</button><button id="next">Next ⏭</button><button id="mute">Mute</button></div>
<div class="field"><label>Volume <span id="volumeValue">12</span> / 21</label><input id="volume" type="range" min="0" max="21" value="12"></div>
<div class="row"><label class="field inline"><input id="shuffle" type="checkbox"> Shuffle</label><label class="field inline"><input id="favoritesOnly" type="checkbox"> Favorites only</label></div>
<div class="row"><button id="listenHere">▶ Listen on this device</button><button id="stopHere">■ Stop browser audio</button></div><audio id="browserAudio" controls preload="none"></audio>
<div class="hint">Browser listening is separate from speaker playback. Some streams block browsers.</div></section>
<section class="panel"><h2>Music-reactive lighting • 7 LEDs</h2><div class="gridbuttons" id="lightModes">
<button data-mode="0">🌈 Party Bounce</button><button data-mode="1">▮▮ VU Meter</button><button data-mode="2">✨ Rainbow</button><button data-mode="6">🎨 Solid Reactive</button><button data-mode="5">💡 Breathing</button><button data-mode="4">✦ Sparkle</button><button data-mode="3">○ Lights Off</button></div>
<div class="field"><label>Brightness <output id="brightnessOut">90</output></label><input id="brightness" type="range" min="10" max="180" value="90"></div>
<div class="field"><label>Beat sensitivity <output id="sensitivityOut">55</output>%</label><input id="sensitivity" type="range" min="0" max="100" value="55"></div>
<div class="field"><label>Animation speed <output id="speedOut">50</output>%</label><input id="speed" type="range" min="0" max="100" value="50"></div>
<div class="field"><label>Color hue <output id="hueOut">200</output> / 255</label><input id="hue" type="range" min="0" max="255" value="200"></div>
<button id="saveLights" class="primary">Save lighting</button>
<hr style="border:0;border-top:1px solid #283b58;margin:15px 0"><h2>Quiet hours (LEDs off)</h2>
<div class="field inline"><input id="quiet" type="checkbox"><label for="quiet">Enable nighttime quiet hours</label></div>
<div class="row"><div style="flex:1"><label class="hint">Start hour (0–23)</label><input id="quietStart" type="number" min="0" max="23" value="23"></div><div style="flex:1"><label class="hint">End hour (0–23)</label><input id="quietEnd" type="number" min="0" max="23" value="7"></div></div>
<button id="saveQuiet" style="margin-top:9px">Save quiet hours</button>
</section>
<section class="panel wide"><div class="between"><h2>20 stations</h2><span class="hint">Tap ♥ to favorite, ↑↓ to rearrange</span></div>
<input type="search" id="search" placeholder="Search stations by name or genre" style="margin-bottom:10px"><div id="stations"></div>
<details><summary>Modify or restore a station</summary><div class="row"><div style="flex:1"><div class="field"><label>Station number (1–20)</label><input id="editIndex" type="number" min="1" max="20" value="1"></div></div>
<div style="flex:2"><div class="field"><label>Station name</label><input id="editName" maxlength="64"></div></div></div>
<div class="field"><label>Genre</label><input id="editGenre" maxlength="32"></div><div class="field"><label>Direct MP3/AAC or playlist URL</label><input id="editUrl" maxlength="300"></div>
<div class="row"><button id="saveStation" class="primary">Save preset</button><button id="restoreStation">Restore this preset</button><button id="restoreAll" class="danger">Restore all 20 defaults</button></div></details>
<details><summary>Import / export station list</summary><div class="row"><button id="exportStations">Download 20-preset JSON</button><input id="importFile" type="file" accept=".json,application/json" style="max-width:310px"><button id="importStations">Import selected JSON</button></div>
<div class="hint">Exports ONLY station names, genres, and URLs—not Wi-Fi passwords or firmware.</div></details></section>
<section class="panel"><h2>Sleep timer</h2><div class="row"><select id="sleep" style="flex:1"><option value="0">Off</option><option value="15">15 minutes</option><option value="30">30 minutes</option><option value="45">45 minutes</option><option value="60">1 hour</option><option value="90">90 minutes</option><option value="120">2 hours</option><option value="180">3 hours</option><option value="240">4 hours</option></select><button id="setSleep">Set</button></div>
<div class="hint" id="sleepLeft">No sleep timer</div>
<hr style="border:0;border-top:1px solid #283b58;margin:16px 0"><h2>Daily radio alarm</h2><div class="field inline"><input id="alarmEnabled" type="checkbox"><label for="alarmEnabled">Enable alarm</label></div>
<div class="row"><div style="flex:1"><label class="hint">Hour</label><input id="alarmHour" type="number" min="0" max="23" value="7"></div><div style="flex:1"><label class="hint">Minute</label><input id="alarmMinute" type="number" min="0" max="59" value="0"></div></div>
<div class="field"><label>Alarm station</label><select id="alarmStation"></select></div><div class="field"><label>POSIX timezone (default Central)</label><input id="timezone" value="CST6CDT,M3.2.0,M11.1.0"></div><button id="saveAlarm" class="primary">Save alarm</button>
<div class="hint" id="clock">Clock syncing...</div></section>
<section class="panel"><h2>Startup and playback</h2><div class="field"><label>Station to play automatically after boot</label><select id="bootStation"></select></div>
<div class="field"><label>Startup volume (0–21)</label><input id="bootVolume" type="number" min="0" max="21" value="12"></div>
<div class="field inline"><input id="fade" type="checkbox" checked><label for="fade">Smooth fade-in on station start</label></div><button id="saveBoot" class="primary">Save startup preferences</button>
<hr style="border:0;border-top:1px solid #283b58;margin:16px 0"><h2>Play a custom stream URL</h2><input id="customStream" type="url" placeholder="https://.../live.mp3"><button id="playCustom" style="margin-top:10px">Play custom URL</button></section>
<section class="panel"><h2>Wi-Fi and persistent controller</h2>
<div class="hint">Home Wi-Fi: <strong id="ssidValue">—</strong></div><div class="hint">Home IP: <strong id="homeIP">—</strong></div><div class="hint">Setup hotspot: <strong id="setupIP">—</strong></div>
<div class="hint">Local hostname: <strong id="localHost">—</strong></div>
<details><summary>Change Wi-Fi network</summary><div class="field"><label>Network SSID</label><input id="wifiSsid" maxlength="32"></div><div class="field"><label>Network password</label><input id="wifiPass" type="password" maxlength="64" autocomplete="new-password"></div><button id="scanWifi">Scan nearby networks</button><select id="wifiResults" style="margin:8px 0"><option>Scan to choose a network</option></select><button id="saveWifi" class="primary">Connect to Wi-Fi</button></details>
<details><summary>Hotspot and network name</summary><div class="field inline"><input id="keepAP" type="checkbox" checked><label for="keepAP">Always keep PartyRadio-Setup hotspot running</label></div><div class="field"><label>Local network hostname (.local)</label><input id="hostname" maxlength="24"></div><div class="field"><label>New setup hotspot password (optional; 8+ characters)</label><input id="apPass" type="password" placeholder="Leave empty to keep existing password"></div><button id="saveController">Save controller settings</button><p class="hint">Changing hotspot settings may briefly disconnect this page. The radio is not exposed to the public internet.</p></details></section>
<section class="panel"><h2>🎙 Computer • on-device voice control</h2>
<div class="row"><span class="tag">Wake word: <strong>Computer</strong></span><span id="voiceState" class="tag">Initializing</span></div>
<p class="hint">Say <strong>Computer</strong>, then "Next station", "Play news", "Play weather", "Volume up", "Party lights", or "Stop music". The speaker uses its own microphones and an offline ESP-SR model.</p>
<div class="field inline"><input type="checkbox" id="voiceEnabled" checked><label for="voiceEnabled">Enable wake word and commands</label></div>
<div class="field"><label>Microphone gain (0–14) <output id="micGainOut">8</output></label><input type="range" min="0" max="14" value="8" id="micGain"></div>
<div class="field"><label for="micChannel">Microphone input channel (experimental)</label><select id="micChannel"><option value="0">Auto: strongest of the two microphones</option><option value="1">Left input</option><option value="2">Right input</option></select></div>
<div class="row"><button id="saveVoice" class="primary">Save voice settings</button><button id="checkVoice">Check microphone status</button></div>
<div class="field"><label>Live microphone input</label><div class="hint" id="micLevels">Left: — / Right: —</div><progress id="micLeftBar" max="32767" value="0" style="width:100%"></progress><progress id="micRightBar" max="32767" value="0" style="width:100%"></progress></div>
<div class="hint" id="voiceHint">Speak near the microphone and look for the levels to move. If sample count stays at zero, the I2S input is not delivering audio.</div>
<p id="voiceLast" class="hint">Voice: not checked</p>
<p class="hint">Gain changes restart the speaker. Voice-model startup and listening while music plays require hardware verification. A microphone failure will not stop radio playback.</p>
</section>
<section class="panel"><h2>Diagnostics and maintenance</h2><div class="statrow" id="diagnostics"><span>Loading radio status...</span></div>
<div class="row" style="margin-top:15px"><button id="refresh">Refresh</button><button id="reboot" class="danger">Restart speaker</button></div>
<p class="hint">Controller is local and does not require a PC or Raspberry Pi. For security, don't forward port 80 from your router.</p></section>
</div><div id="toast" role="status"></div><div class="hint" style="padding:10px">Party Radio 20 Pro v3.5 • Computer microphone diagnostics • No firmware backup in Windows flasher.</div>
<script>
'use strict';
const $=id=>document.getElementById(id);
let state={},stations=[],editing=false,ready=false;
function h(s){return String(s??'').replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]))}
function notify(s,problem=false){const t=$('toast');t.style.display='block';t.style.background=problem?'#713347':'#244269';t.textContent=s;setTimeout(()=>t.style.display='none',4700)}
async function api(path,data){const opts=data===undefined?{}:{method:'POST',body:new URLSearchParams(data)};const r=await fetch(path,opts);const text=await r.text();let j;try{j=JSON.parse(text)}catch(_){throw Error('Unexpected response ('+r.status+')')};if(!r.ok)throw Error(j.error||'Request failed');return j}
async function doIt(path,data,success){try{await api(path,data);if(success)notify(success);await refresh()}catch(e){notify(e.message,true)}}
function setVal(id,value){const el=$(id);if(el&&document.activeElement!==el)el.value=value??''}
function setCheck(id,val){const el=$(id);if(el&&document.activeElement!==el)el.checked=Boolean(val)}
function optionList(){const o=stations.map(s=>`<option value="${s.id}">${String(s.preset).padStart(2,'0')} • ${h(s.name)}</option>`).join('');for(const id of ['alarmStation','bootStation'])$(id).innerHTML=o}
function populateEdit(){const i=Number($('editIndex').value)-1;const s=stations[i];if(!s)return;setVal('editName',s.name);setVal('editGenre',s.genre);setVal('editUrl',s.url)}
function drawStations(){const filter=$('search').value.toLowerCase().trim();const out=$('stations');out.innerHTML='';for(const s of stations){if(filter&&!(s.name+' '+s.genre).toLowerCase().includes(filter))continue;const el=document.createElement('div');el.className='station'+(state.mode!=='custom'&&s.id===state.stationIndex?' active':'');el.innerHTML=`<div class="identity"><strong>${String(s.preset).padStart(2,'0')} • ${h(s.name)}</strong><small>${h(s.genre)}</small></div>`;
const fav=document.createElement('button');fav.className='heart'+(s.favorite?' on':'');fav.title='Toggle favorite';fav.textContent=s.favorite?'♥':'♡';fav.onclick=()=>doIt('/api/favorite',{id:s.id});el.appendChild(fav);
const play=document.createElement('button');play.textContent='▶';play.title='Play station';play.onclick=()=>doIt('/api/play',{id:s.id});el.appendChild(play);
for(const [down,txt] of [[0,'↑'],[1,'↓']]){const b=document.createElement('button');b.textContent=txt;b.title=down?'Move down':'Move up';b.disabled=(down&&s.id===19)||(!down&&s.id===0);b.onclick=()=>doIt('/api/move',{id:s.id,down},'Station moved');el.appendChild(b)}out.appendChild(el)}}
function render(){if(!ready)return;const s=state;
$('connection').textContent=s.wifiConnected?'● Home Wi-Fi connected':'○ Setup / offline';$('stationName').textContent=(s.mode==='custom'?'Custom stream':`${s.preset}/20 • ${s.name||''}`);$('trackTitle').textContent=s.title||s.streamName||'Live Internet Radio';$('toggle').textContent=s.playing?'■ Stop':'▶ Play';$('mute').textContent=s.muted?'Unmute':'Mute';
setVal('volume',s.volume);$('volumeValue').textContent=s.volume;setCheck('shuffle',s.shuffle);setCheck('favoritesOnly',s.favoritesOnly);
for(const key of ['brightness','sensitivity','speed','hue']){let name=key==='brightness'?'ledBrightness':key;setVal(key,s[name]);$(key+'Out').textContent=s[name]??0}
for(const el of document.querySelectorAll('[data-mode]'))el.style.outline=Number(el.dataset.mode)===s.ledMode?'2px solid #8fbeff':'none';
setCheck('quiet',s.quietHours);setVal('quietStart',s.quietStart);setVal('quietEnd',s.quietEnd);
setCheck('alarmEnabled',s.alarmEnabled);setVal('alarmHour',s.alarmHour);setVal('alarmMinute',s.alarmMinute);setVal('alarmStation',s.alarmStation);setVal('timezone',s.timezone);
setCheck('voiceEnabled',s.voiceEnabled);setVal('micGain',s.micGain);setVal('micChannel',s.micChannel??0);
$('micGainOut').textContent=s.micGain??8;
$('voiceState').textContent=s.voiceReady?(s.voiceListening?'Listening':s.voiceAwake?'Computer heard':((s.voiceFrames||0)>0?'Mic data arriving':'Engine loaded • no mic samples')):(s.voiceEnabled?'Voice startup failed':'Voice disabled');
$('micLeftBar').value=s.micLeftLevel||0;$('micRightBar').value=s.micRightLevel||0;
$('micLevels').textContent='Left: '+(s.micLeftLevel||0)+' / Right: '+(s.micRightLevel||0)+' (0–32767)';
$('voiceHint').textContent=(!s.voiceReady)?('Voice error: '+(s.voiceError||'Unknown')):(!(s.voiceFrames>0)?'No microphone samples arriving. Check shared I2S clocks / capture configuration.':(!((s.micLeftLevel||0)>25||(s.micRightLevel||0)>25)?'PCM is arriving but appears silent. Try the other mic input and reboot.':'Microphone has measurable activity. If Computer does not wake, reduce radio volume and check model / gain.'));
$('voiceLast').textContent='Wake detections: '+(s.voiceWakeCount||0)+' • Recognized commands: '+(s.voiceCommandCount||0)+' • PCM frames: '+(s.voiceFrames||0)+' • I2S read failures: '+(s.voiceReadFailures||0)+' • Last command: '+(s.lastVoiceCommand||'None');
setVal('bootStation',s.bootStation);setVal('bootVolume',s.bootVolume);setCheck('fade',s.fade);setCheck('keepAP',s.keepAP);setVal('hostname',s.hostname);
$('clock').textContent='Radio clock: '+(s.localTime||'Syncing...');$('sleepLeft').textContent=s.sleepMinutes?'Timer active: about '+s.sleepMinutes+' min remaining':'No sleep timer active';
$('ssidValue').textContent=s.ssid||'Not connected';$('homeIP').textContent=s.lanIP||'Not connected';$('setupIP').textContent=s.apIP||'Off';$('localHost').textContent='http://'+s.hostname+'.local/';
$('diagnostics').innerHTML=[['Firmware',s.version],['Wi-Fi signal',s.wifiConnected?s.rssi+' dBm':'Disconnected'],['Uptime',Math.floor((s.uptimeSeconds||0)/60)+' minutes'],['Audio decoder',s.decoderRunning?'Playing':'Idle / connecting'],['Codec',s.codecReady?'Ready':'Not detected'],['Amplifier expander',s.ampExpanderReady?'Ready':'Not detected'],['Wake word',s.voiceWakeWord||'Computer'],['Voice engine',s.voiceReady?'Ready':(s.voiceError||'Not started')],['Microphone samples',s.voiceFrames||0],['Mic L/R level',(s.micLeftLevel||0)+' / '+(s.micRightLevel||0)],['ES7210 analog 0x40',s.micReg40>=0?'0x'+s.micReg40.toString(16).padStart(2,'0'):'No I2C'],['ES7210 mic power 0x4B',s.micReg4B>=0?'0x'+s.micReg4B.toString(16).padStart(2,'0'):'No I2C'],['ES7210 audio format 0x11',s.micReg11>=0?'0x'+s.micReg11.toString(16).padStart(2,'0'):'No I2C'],['Mic read failures',s.voiceReadFailures||0],['Wake events',s.voiceWakeCount||0],['Recognized commands',s.voiceCommandCount||0],['Stream retries',s.retries],['Last error',s.lastError||'None'],['Audio level',Math.round((s.audioLevel||0)*100/255)+'%'],['Free RAM',Math.round((s.heapFree||0)/1024)+' KB'],['PSRAM',Math.round((s.psramFree||0)/1024)+' KB']].map(x=>`<span><span class="dim">${h(x[0])}:</span> ${h(x[1])}</span>`).join('');drawStations()}
async function refresh(full=false){try{if(full||!ready){stations=await api('/api/stations');optionList();ready=true;populateEdit()}state=await api('/api/status');if(!full)stations=await api('/api/stations');render()}catch(e){$('connection').textContent='Connection lost';if(full)notify(e.message,true)}}
$('prev').onclick=()=>doIt('/api/navigation',{step:0});$('next').onclick=()=>doIt('/api/navigation',{step:1});$('toggle').onclick=()=>doIt('/api/toggle',{});$('mute').onclick=()=>doIt('/api/mute',{});
$('volume').onchange=()=>doIt('/api/volume',{value:$('volume').value});$('volume').oninput=()=>$('volumeValue').textContent=$('volume').value;
for(const k of ['shuffle','favoritesOnly'])$(k).onchange=()=>doIt('/api/navigation',{[k]:$(k).checked?1:0});
for(const el of document.querySelectorAll('[data-mode]'))el.onclick=()=>doIt('/api/lights',{mode:el.dataset.mode});
for(const k of ['brightness','sensitivity','speed','hue'])$(k).oninput=()=>$(k+'Out').textContent=$(k).value;
$('saveLights').onclick=()=>doIt('/api/lights',{brightness:$('brightness').value,sensitivity:$('sensitivity').value,speed:$('speed').value,hue:$('hue').value},'Lighting saved');
$('saveQuiet').onclick=()=>doIt('/api/lights',{quiet:$('quiet').checked?1:0,quietStart:$('quietStart').value,quietEnd:$('quietEnd').value},'Quiet hours saved');
$('search').oninput=drawStations;$('editIndex').onchange=populateEdit;
$('saveStation').onclick=async()=>{await doIt('/api/preset',{id:Number($('editIndex').value)-1,name:$('editName').value,genre:$('editGenre').value,url:$('editUrl').value},'Station saved');await refresh(true)};
$('restoreStation').onclick=async()=>{if(!confirm('Restore this one preset to its original stream?'))return;await doIt('/api/restore',{id:Number($('editIndex').value)-1},'Station restored');await refresh(true)};
$('restoreAll').onclick=async()=>{if(!confirm('Restore all 20 original presets? Your edits will be overwritten.'))return;await doIt('/api/restore',{id:-1},'20 presets restored');await refresh(true)};
$('exportStations').onclick=()=>{location.href='/api/stations/export'};
$('importStations').onclick=async()=>{const file=$('importFile').files?.[0];if(!file)return notify('Select a JSON file first',true);try{if(file.size>18000)throw Error('Station file exceeds 18 KB');const t=await file.text();const a=JSON.parse(t);if(!Array.isArray(a)||a.length!==20)throw Error('Expected exactly 20 stations');if(!confirm('Replace all 20 station presets from this file?'))return;await doIt('/api/stations/import',{payload:JSON.stringify(a)},'Stations imported');await refresh(true)}catch(e){notify(e.message,true)}};
$('setSleep').onclick=()=>doIt('/api/sleep',{minutes:$('sleep').value},'Sleep timer updated');
$('saveAlarm').onclick=()=>doIt('/api/alarm',{enabled:$('alarmEnabled').checked?1:0,hour:$('alarmHour').value,minute:$('alarmMinute').value,station:$('alarmStation').value,timezone:$('timezone').value},'Alarm saved');
$('micGain').oninput=()=>$('micGainOut').textContent=$('micGain').value;
$('saveVoice').onclick=()=>doIt('/api/voice',{enabled:$('voiceEnabled').checked?1:0,gain:$('micGain').value,channel:$('micChannel').value},'Voice settings saved (gain changes restart radio)');
$('checkVoice').onclick=()=>refresh(true);
$('saveBoot').onclick=()=>doIt('/api/boot',{station:$('bootStation').value,volume:$('bootVolume').value,fade:$('fade').checked?1:0},'Startup preferences saved');
$('playCustom').onclick=()=>doIt('/api/stream',{url:$('customStream').value},'Custom stream requested');
$('saveWifi').onclick=()=>doIt('/api/wifi',{ssid:$('wifiSsid').value,password:$('wifiPass').value},'Wi-Fi saved; reconnecting');
$('scanWifi').onclick=async()=>{try{let result=await api('/api/scan');$('wifiResults').innerHTML='<option>Scanning... tap Scan again in 3 seconds</option>';if(result.networks?.length){$('wifiResults').innerHTML=result.networks.map(n=>`<option value="${h(n.ssid)}">${h(n.ssid)} (${n.rssi} dBm)</option>`).join('')}}catch(e){notify(e.message,true)}};
$('wifiResults').onchange=()=>setVal('wifiSsid',$('wifiResults').value);
$('saveController').onclick=()=>doIt('/api/controller',{keepAP:$('keepAP').checked?1:0,hostname:$('hostname').value,password:$('apPass').value},'Controller settings saved');
$('refresh').onclick=()=>refresh(true);$('reboot').onclick=()=>{if(confirm('Restart the Waveshare speaker now?'))doIt('/api/reboot',{},'Rebooting radio...')};
$('listenHere').onclick=()=>{const a=$('browserAudio');if(!/^https?:\/\//i.test(state.url||''))return notify('No valid stream URL',true);a.pause();a.src=state.url;a.load();a.play().catch(e=>notify('Browser cannot play this station: '+e.message,true))};
$('stopHere').onclick=()=>{const a=$('browserAudio');a.pause();a.removeAttribute('src');a.load()};
refresh(true);setInterval(()=>{if(!document.hidden)refresh()},3500);
</script></main></body></html>)RVWEB";
