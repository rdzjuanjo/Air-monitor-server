#ifndef WEBS_H
#define WEBS_H

/**
 * @file webs.h
 * @brief Servidor web unificado: monitoreo + portal cautivo + configuración
 *
 * Puerto 53  – DNS hijack  → portal cautivo automático
 * Puerto 80  – HTTP  (ESPAsyncWebServer)
 * WS   /ws   – WebSocket async (push de datos cada 2 s)
 *
 * El DNS responde "*" con 192.168.4.1, por lo que iOS/Android abren
 * el navegador automáticamente al conectarse al AP.
 * onNotFound redirige cualquier URL desconocida a "/" — segundo nivel
 * de captura para sistemas que no usan DNS de detección.
 *
 * Rutas:
 *   GET  /           → Monitor en tiempo real
 *   GET  /config     → Configuración (WiFi, device ID, coords)
 *   POST /config     → Guarda y reconecta STA  (params POST)
 *   GET  /api/status → JSON con estado completo
 *   GET  /calibrate  → Calibración rápida del sensor
 *   *                → redirect("/")  ← portal cautivo
 */

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "config.h"
#include "NvsConfig.h"
#include "MQInstrument.h"
#include "system_status.h"

// ============================================================================
// CONFIGURACIÓN DEL PROYECTO
// Config hereda los campos comunes de BaseConfig (wifi, device_id, lat/lon…).
// registerNvsHooks() debe llamarse desde setup() ANTES de libSetup().
// ============================================================================
struct Config : public BaseConfig {
  char mdnsHostname[32];
};
Config config;

inline void registerNvsHooks() {
  nvsInit(&config, sizeof(config), "movoca", 8, CFG_NETWORK_AP_NAME_PREFIX);
  nvsRegisterLoadHook([](Preferences& p) {
    strlcpy(config.mdnsHostname,
            p.getString("mdns_host", CFG_NETWORK_MDNS_HOSTNAME).c_str(),
            sizeof(config.mdnsHostname));
  });
  nvsRegisterSaveHook([](Preferences& p) {
    p.putString("mdns_host", config.mdnsHostname);
  });
  nvsRegisterResetHook([]() {
    config.latitude  = 20.5181f;
    config.longitude = -103.2000f;
    strlcpy(config.mdnsHostname, CFG_NETWORK_MDNS_HOSTNAME, sizeof(config.mdnsHostname));
  });
  nvsRegisterPrintHook([]() {
    Serial.printf("mDNS Host   : %s.local\n", config.mdnsHostname);
  });
}

// ============================================================================
// FORWARD DECLARATIONS
// ============================================================================

extern bool          wifi_connected;
extern bool          webServerStarted;
extern MQInstrument  mq135;

bool   isMQTTConnected();
String getMQTTStatus();
const char *getFirmwareVersion();
const char *getLastMqttPublishTopic();
const char *getLastMqttPublishPayload();
bool getLastMqttPublishSuccess();
const char *getLastMqttInboundTopic();
const char *getLastMqttInboundPayload();
const char *getLastMqttInboundCommand();
unsigned long getLastMqttInboundAtMs();
int getLastMqttReconnectState();
unsigned long getLastMqttReconnectAtMs();
void   wifiStartSTAConnect();

// ============================================================================
// VARIABLES GLOBALES
// ============================================================================

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
DNSServer      dnsServer;

static bool dnsRunning = false;
static volatile bool g_wifiScanRequested = false;  // flag seguro entre tasks

static float    sensorHistory[72] = {};
static uint32_t sampleHistory[72] = {};
static uint32_t sampleCounter     = 0;
static int      historyIndex      = 0;
static bool     historyInitialized = false;
static const    int HISTORY_SIZE  = 72;

// ============================================================================
// HELPERS JSON
// ============================================================================

inline void registerHistoryHooks() {
  mqttPayloadAddMetricsFiller([](JsonObject&) {
    sampleHistory[historyIndex] = sampleCounter++;
    sensorHistory[historyIndex] = mq135.getReading();
    if (++historyIndex >= HISTORY_SIZE) { historyIndex = 0; historyInitialized = true; }
  });
}
String buildStatusJson() {
  float ppm = mq135.getReading();
  DynamicJsonDocument doc(1024);
  doc["device_id"]    = config.device_id;
  doc["ap_prefix"]    = getApNamePrefix();
  doc["mdns_hostname"]= config.mdnsHostname;
  doc["wifi_ssid"]    = config.wifi_ssid;
  doc["sensor_value"] = ppm;
  doc["air_quality"]  = mqGetAirQualityLevel(ppm);
  doc["calibrated"]   = mq135.isCalibrated();
  doc["calibration_adc0"] = mq135.getCalibrationValue();
  doc["adc_current"]      = mq135.getFilteredADC();
  doc["calibration_status"] = mq135.isCalibrated()
      ? String("MQ: ADC0=") + String(mq135.getCalibrationValue(), 0)
      : String("MQ: sin calibracion guardada");
  doc["latitude"]     = config.latitude;
  doc["longitude"]    = config.longitude;
  doc["wifi_ip"]      = wifi_connected ? WiFi.localIP().toString() : String("");
  doc["ap_ip"]        = WiFi.softAPIP().toString();
  doc["uptime"]       = (long long)(millis() / 1000);
  doc["mqtt"]         = isMQTTConnected();
  doc["mqtt_status"]  = getMQTTStatus();
  doc["firmware_version"] = getFirmwareVersion();
  doc["wifi_rssi"]    = wifi_connected ? WiFi.RSSI() : 0;
  doc["ap_clients"]   = WiFi.softAPgetStationNum();
  doc["mqtt_last_publish_topic"] = getLastMqttPublishTopic();
  doc["mqtt_last_publish_ok"] = getLastMqttPublishSuccess();
  doc["mqtt_last_publish_payload"] = getLastMqttPublishPayload();
  doc["mqtt_last_inbound_topic"] = getLastMqttInboundTopic();
  doc["mqtt_last_inbound_command"] = getLastMqttInboundCommand();
  doc["mqtt_last_inbound_payload"] = getLastMqttInboundPayload();
  doc["mqtt_last_inbound_at_ms"] = (long long)getLastMqttInboundAtMs();
  doc["mqtt_last_reconnect_state"] = getLastMqttReconnectState();
  doc["mqtt_last_reconnect_at_ms"] = (long long)getLastMqttReconnectAtMs();
  JsonObject sysObj = doc.createNestedObject("system");
  buildSysStatusJson(sysObj);
  String out; serializeJson(doc, out); return out;
}

String buildWsStatusJson() {
  float ppm = mq135.getReading();
  DynamicJsonDocument doc(1024);
  doc["type"]         = "status";
  doc["sensor_value"] = ppm;
  doc["air_quality"]  = mqGetAirQualityLevel(ppm);
  doc["device_id"]    = config.device_id;
  doc["location"]     = String(config.latitude, 5) + ", " + String(config.longitude, 5);
  doc["adc_current"]       = mq135.getFilteredADC();
  doc["calibration_adc0"]  = mq135.getCalibrationValue();
  doc["wifi_connected"]    = wifi_connected;
  doc["wifi_ssid"]         = wifi_connected ? String(WiFi.SSID()) : String(config.wifi_ssid);
  doc["mqtt_status"] = getMQTTStatus();
  doc["firmware_version"] = getFirmwareVersion();
  doc["wifi_rssi"] = wifi_connected ? WiFi.RSSI() : 0;
  doc["mqtt_last_publish_ok"] = getLastMqttPublishSuccess();
  doc["mqtt_last_inbound_command"] = getLastMqttInboundCommand();
  JsonObject sysObj = doc.createNestedObject("system");
  buildSysStatusJson(sysObj);
  String out; serializeJson(doc, out); return out;
}

String buildWsHistoryJson() {
  DynamicJsonDocument doc(2048);
  doc["type"] = "history";
  JsonArray samples = doc.createNestedArray("samples");
  JsonArray vals = doc.createNestedArray("values");
  int start = historyInitialized ? historyIndex : 0;
  int count = historyInitialized ? HISTORY_SIZE : historyIndex;
  for (int i = 0; i < count; i++) {
    int idx = (start + i) % HISTORY_SIZE;
    samples.add((long long)sampleHistory[idx]);
    vals.add(sensorHistory[idx]);
  }
  String out; serializeJson(doc, out); return out;
}

void broadcastStatus() {
  if (ws.count() == 0) return;
  ws.textAll(buildWsStatusJson());
}
void broadcastConfig() { broadcastStatus(); }

// ============================================================================
// PÁGINAS HTML (PROGMEM)
// ============================================================================

const char PAGE_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Nariz Digital</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',sans-serif;background:#0d1117;color:#e6edf3;min-height:100vh;display:flex;flex-direction:column}
.badge{display:inline-block;padding:2px 10px;border-radius:12px;font-size:.72rem;font-family:monospace}
.online{background:#1a3a1a;color:#3fb950;border:1px solid #3fb950}
.offline{background:#2d1a1a;color:#f85149;border:1px solid #f85149}
.topbar{background:#161b22;border-bottom:1px solid #30363d;padding:10px 16px;display:flex;align-items:center;justify-content:space-between;gap:10px}
.btn-config{display:inline-block;padding:7px 18px;background:#30363d;color:#e6edf3;border:none;border-radius:8px;font-size:.9rem;font-weight:600;cursor:pointer;text-decoration:none}
.btn-config:hover{opacity:.85}
main{flex:1;max-width:860px;width:100%;margin:0 auto;padding:14px 14px 0}
.card{background:#161b22;border-radius:10px;padding:20px;margin-bottom:14px;border:1px solid #30363d}
.big-val{font-size:4rem;font-weight:700;text-align:center;letter-spacing:-2px;transition:color .4s;font-family:monospace}
.lbl{text-align:center;color:#7d8590;font-size:.82rem;margin-bottom:4px}
.quality{text-align:center;margin-top:8px}
.quality span{display:inline-block;padding:5px 18px;border-radius:20px;font-weight:600;font-size:.9rem}
.q-alta span{background:#1a3a1a;color:#3fb950;border:1px solid #3fb950}
.q-medio span{background:#3d2e00;color:#d29922;border:1px solid #d29922}
.q-baja span{background:#2d1a1a;color:#f85149;border:1px solid #f85149}
.chart-container{height:380px;width:100%;position:relative;margin-top:4px}
footer{background:#161b22;border-top:1px solid #30363d;padding:10px 16px;font-size:.73rem;color:#7d8590;display:flex;gap:16px;flex-wrap:wrap;justify-content:center}
footer b{color:#adb5bd}
</style>
<script src="/chart.umd.js"></script>
</head>
<body>
<div class="topbar">
  <a href="/config" class="btn-config">⚙ Configuración</a>
  <span style="font-size:.75rem;color:#7d8590">Nariz Digital</span>
</div>
<main>
  <div class="card">
    <p class="lbl">Concentración de COVs (ppm)</p>
    <div class="big-val" id="ppm">--</div>
    <div class="quality" id="qual"><span>—</span></div>
  </div>
  <div class="chart-container">
    <canvas id="chart"></canvas>
  </div>
</main>
<footer>
  <span>Dispositivo: <b id="footDevId">--</b></span>
  <span>WiFi: <b id="footWifi">--</b></span>
  <span>Red: <b id="footSsid">--</b></span>
  <span>ADC: <b id="devAdc">--</b></span>
  <span>ADC0: <b id="devAdc0">--</b></span>
</footer>
<script>
(function(){
  let chart=null;
  if(typeof Chart!=='undefined'){
    try{
      const ctx=document.getElementById('chart').getContext('2d');
      chart=new Chart(ctx,{type:'line',data:{datasets:[{label:'COVs (ppm)',data:[],
        borderColor:'#58a6ff',backgroundColor:'rgba(88,166,255,.1)',fill:true,tension:.4,
        pointRadius:3,pointHoverRadius:6}]},options:{responsive:true,maintainAspectRatio:false,
        scales:{x:{
          grid:{color:'#21262d'},ticks:{color:'#7d8590',maxTicksLimit:8,
            callback:v=>'#'+v}},
          y:{beginAtZero:true,grid:{color:'#21262d'},ticks:{color:'#7d8590',callback:v=>v+' ppm'}}},
        plugins:{legend:{display:false},
          tooltip:{callbacks:{title:i=>'Muestra #'+i[0].parsed.x,label:c=>c.parsed.y.toFixed(2)+' ppm'}}}}});
    }catch(_){}
  }

  const qMap={'Alta':{c:'q-alta',l:'BAJO'},'MEDIO':{c:'q-medio',l:'MODERADO'},'Baja':{c:'q-baja',l:'ALTO'}};
  let wsConn,retry=2000;

  function updateUI(d){
    const ppm=parseFloat(d.sensor_value);
    document.getElementById('ppm').textContent=isNaN(ppm)?'--':ppm.toFixed(2);
    document.getElementById('devAdc').textContent=d.adc_current!=null?Math.round(d.adc_current):'--';
    document.getElementById('devAdc0').textContent=d.calibration_adc0!=null?Math.round(d.calibration_adc0):'--';
    document.getElementById('footDevId').textContent=d.device_id||'--';
    document.getElementById('footWifi').textContent=d.wifi_connected?'Conectado':'Desconectado';
    document.getElementById('footSsid').textContent=d.wifi_ssid||'--';
    const q=qMap[d.air_quality]||{c:'q-medio',l:d.air_quality||'—'};
    const qd=document.getElementById('qual');
    qd.className='quality '+q.c;qd.querySelector('span').textContent=q.l;
    const cols={'q-alta':'#3fb950','q-medio':'#d29922','q-baja':'#f85149'};
    document.getElementById('ppm').style.color=cols[q.c]||'#e6edf3';
  }

  function loadHistory(d){
    if(!chart||!d.samples||!d.values)return;
    chart.data.datasets[0].data=d.samples.map((s,i)=>({x:s,y:d.values[i]}));
    chart.update();
  }

  function initWS(){
    wsConn=new WebSocket('ws://'+location.hostname+'/ws');
    wsConn.onopen=()=>{
      retry=2000;
      wsConn.send('getStatus');wsConn.send('getHistory');
      setInterval(()=>{if(wsConn.readyState===1)wsConn.send('getStatus');},2000);
    };
    wsConn.onclose=wsConn.onerror=()=>{
      setTimeout(initWS,retry);retry=Math.min(retry*2,30000);
    };
    wsConn.onmessage=e=>{try{const d=JSON.parse(e.data);
      if(d.type==='status')updateUI(d);
      if(d.type==='history')loadHistory(d);}catch(_){}};
  }
  initWS();
})();
</script>
</body></html>
)rawliteral";

const char PAGE_CONFIG[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Nariz Digital – Configuración</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',sans-serif;background:#0d1117;color:#e6edf3;min-height:100vh}
header{background:#161b22;padding:14px 20px;display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid #30363d}
header h1{font-size:1.1rem;color:#58a6ff;font-family:monospace}
nav a{color:#58a6ff;text-decoration:none;margin-left:14px;font-size:.85rem}
main{max-width:520px;margin:28px auto;padding:0 14px}
.card{background:#161b22;border-radius:10px;padding:24px;margin-bottom:20px;border:1px solid #30363d}
h2{font-size:.82rem;color:#58a6ff;font-family:monospace;margin-bottom:14px;padding-bottom:8px;border-bottom:1px solid #30363d;text-transform:uppercase;letter-spacing:.08em}
label{display:block;font-size:.79rem;color:#7d8590;margin-bottom:4px;margin-top:13px}
label:first-of-type{margin-top:0}
input{width:100%;padding:9px 12px;background:#0d1117;border:1px solid #30363d;border-radius:8px;color:#e6edf3;font-size:.9rem;outline:none;transition:border .2s}
input:focus{border-color:#58a6ff}
select{width:100%;padding:9px 12px;background:#0d1117;border:1px solid #30363d;border-radius:8px;color:#e6edf3;font-size:.9rem;outline:none;appearance:none;cursor:pointer}
select:focus{border-color:#58a6ff}
.scan-row{display:flex;gap:8px}
.btn-sm{margin:0!important;width:auto!important;padding:9px 12px!important;flex-shrink:0;background:#30363d!important;color:#e6edf3!important}
.hint{font-size:.72rem;color:#555;margin-top:3px}
button{margin-top:18px;width:100%;padding:11px;background:#58a6ff;color:#0d1117;border:none;border-radius:8px;font-size:.95rem;font-weight:700;cursor:pointer;transition:opacity .2s}
button:hover{opacity:.85}
button:disabled{opacity:.35;cursor:default}
.btn-sec{background:#21262d;color:#e6edf3;border:1px solid #30363d}
.btn-sec:hover{opacity:.85}
.msg{margin-top:10px;padding:10px 13px;border-radius:8px;font-size:.84rem;display:none}
.msg.ok{background:#1a3a1a;color:#3fb950;border:1px solid #3fb950;display:block}
.msg.err{background:#2d1a1a;color:#f85149;border:1px solid #f85149;display:block}
#map{border:1px solid #30363d;border-radius:8px;margin:10px 0}
.leaflet-control-container .leaflet-control{background:#161b22;border:1px solid #30363d}
.leaflet-control-container .leaflet-control a{color:#e6edf3;background:#161b22}
.leaflet-control-container .leaflet-control a:hover{background:#0d1117}
</style>
<link rel="stylesheet" href="https://unpkg.com/leaflet@1.9.4/dist/leaflet.css">
<script src="https://unpkg.com/leaflet@1.9.4/dist/leaflet.js"></script>
</head>
<body>
<header>
  <h1>⚙ Configuración</h1>
  <nav><a href="/">← Monitor</a></nav>
</header>
<main>
  <div class="card">
    <h2>Red y dispositivo</h2>
    <label>Red WiFi</label>
    <div class="scan-row">
      <select id="ssid" onchange="onSsidChange()"><option value="">Buscando redes…</option></select>
      <button type="button" id="btnScan" onclick="startScan()" class="btn-sm">🔄</button>
    </div>
    <input type="text" id="ssid-manual" maxlength="63" placeholder="Nombre de la red (SSID)" style="display:none;margin-top:8px">
    <label>Contraseña WiFi</label>
    <input type="password" id="pass" maxlength="63" placeholder="••••••••">
    <p class="hint">Dejar en blanco para conservar la contraseña guardada.</p>
    <label>Latitud</label>
    <input type="text" id="lat" inputmode="decimal" placeholder="20.518100">
    <label>Longitud</label>
    <input type="text" id="lng" inputmode="decimal" placeholder="-103.200000">
    <div id="map" style="height:250px;position:relative">
      <div id="map-offline" style="display:none;height:100%;background:#0d1117;border:1px solid #30363d;border-radius:8px;display:flex;align-items:center;justify-content:center;padding:16px;text-align:center">
        <p style="color:#7d8590;font-size:.85rem;line-height:1.5">Para configurar la ubicación con el mapa, conéctate a tu red WiFi e ingresa <b style="color:#58a6ff">aire.local</b> en el explorador.</p>
      </div>
    </div>
    <p class="hint">Haz clic en el mapa para seleccionar coordenadas o escríbelas manualmente.</p>
    <button id="btnSave" onclick="guardar()">Guardar y reconectar</button>
    <div class="msg" id="msg"></div>
  </div>
  <div class="card">
    <button onclick="location.href='/advanced'" style="background:#30363d;color:#e6edf3">Avanzados →</button>
  </div>
</main>
<script>
let currentSsid='';
fetch('/api/status').then(r=>r.json()).then(d=>{
  currentSsid=d.wifi_ssid||'';
  document.getElementById('lat').value=d.latitude||'';
  document.getElementById('lng').value=d.longitude||'';
  document.getElementById('ssid').innerHTML='<option value="">Presiona aquí y luego \uD83D\uDD04 </option>';
  initMap();
}).catch(()=>{ initMap(); });

let map, marker;
function initMap(){
  if(typeof L==='undefined'){
    const off=document.getElementById('map-offline');
    if(off)off.style.display='flex';
    return;
  }
  document.getElementById('map-offline').style.display='none';
  const lat = parseFloat(document.getElementById('lat').value) || 20.518100;
  const lng = parseFloat(document.getElementById('lng').value) || -103.200000;
  map = L.map('map').setView([lat, lng], 15);
  L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png',{
    attribution:'© OpenStreetMap'}).addTo(map);
  marker = L.marker([lat, lng], {draggable: true}).addTo(map);
  marker.on('dragend', updateFromMap);
  map.on('click', function(e){ marker.setLatLng(e.latlng); updateFromMap(); });
  document.getElementById('lat').addEventListener('input', updateFromInputs);
  document.getElementById('lng').addEventListener('input', updateFromInputs);
}

function updateFromMap(){
  const pos = marker.getLatLng();
  document.getElementById('lat').value = pos.lat.toFixed(6);
  document.getElementById('lng').value = pos.lng.toFixed(6);
}

function updateFromInputs(){
  const lat = parseFloat(document.getElementById('lat').value);
  const lng = parseFloat(document.getElementById('lng').value);
  if(!isNaN(lat) && !isNaN(lng) && lat>=-90 && lat<=90 && lng>=-180 && lng<=180){
    marker.setLatLng([lat, lng]); map.setView([lat, lng], map.getZoom());
  }
}

let scanning=false,scanDeadline=0;
function startScan(){
  if(scanning)return;
  scanning=true;
  scanDeadline=Date.now()+20000;  // timeout 20 s
  const sel=document.getElementById('ssid'),btn=document.getElementById('btnScan');
  sel.innerHTML='<option value="">Buscando redes…</option>';
  if(btn)btn.disabled=true;
  fetch('/api/scan?start=1').then(r=>r.json()).then(handleScanResp)
    .catch(()=>{scanning=false;if(btn)btn.disabled=false;});
}
function pollScan(){
  if(!scanning)return;
  if(Date.now()>scanDeadline){
    scanning=false;
    const b=document.getElementById('btnScan');if(b)b.disabled=false;
    document.getElementById('ssid').innerHTML='<option value="">Tiempo agotado, intenta de nuevo</option>';
    return;
  }
  fetch('/api/scan').then(r=>r.json()).then(handleScanResp)
    .catch(()=>{scanning=false;const b=document.getElementById('btnScan');if(b)b.disabled=false;});
}
function handleScanResp(d){
  if(d.scanning){setTimeout(pollScan,2500);return;}  // poll cada 2.5 s
  scanning=false;
  const btn=document.getElementById('btnScan');if(btn)btn.disabled=false;
  const sel=document.getElementById('ssid');
  sel.innerHTML='';
  const nets=(d.networks||[]).sort((a,b)=>b.rssi-a.rssi);
  nets.forEach(n=>{
    const o=document.createElement('option');
    o.value=n.ssid;o.textContent=n.ssid+' ('+n.rssi+' dBm)';
    if(n.ssid===currentSsid)o.selected=true;
    sel.appendChild(o);
  });
  const m=document.createElement('option');m.value='__manual__';
  m.textContent='Otra red (ingresar manualmente)\u2026';sel.appendChild(m);
  if(currentSsid&&!nets.find(n=>n.ssid===currentSsid)){
    sel.value='__manual__';
    document.getElementById('ssid-manual').style.display='block';
    document.getElementById('ssid-manual').value=currentSsid;
  }
}
function onSsidChange(){
  const v=document.getElementById('ssid').value,m=document.getElementById('ssid-manual');
  m.style.display=v==='__manual__'?'block':'none';
  if(v==='__manual__')m.focus();
}

function guardar(){
  const btn=document.getElementById('btnSave'),msg=document.getElementById('msg');
  const latVal = parseFloat(document.getElementById('lat').value.replace(',','.'));
  const lngVal = parseFloat(document.getElementById('lng').value.replace(',','.'));
  if (document.getElementById('lat').value && (isNaN(latVal)||latVal<-90||latVal>90)) {
    msg.className='msg err'; msg.textContent='✗ Latitud inválida (debe estar entre -90 y 90)'; return;
  }
  if (document.getElementById('lng').value && (isNaN(lngVal)||lngVal<-180||lngVal>180)) {
    msg.className='msg err'; msg.textContent='✗ Longitud inválida (debe estar entre -180 y 180)'; return;
  }

  btn.disabled=true;btn.textContent='Guardando…';msg.className='msg';
  const p=new URLSearchParams({
    ssid:(document.getElementById('ssid').value==='__manual__'?document.getElementById('ssid-manual').value:document.getElementById('ssid').value).trim(),
    pass:document.getElementById('pass').value,
    lat:isNaN(latVal)?'':latVal.toString(),
    lng:isNaN(lngVal)?'':lngVal.toString()});
  fetch('/config',{method:'POST',body:p,headers:{'Content-Type':'application/x-www-form-urlencoded'}})
    .then(r=>r.json())
    .then(d=>{
      if(d.ok){msg.className='msg ok';msg.textContent='✓ Guardado. Reconectando…';
        setTimeout(()=>location.href='/',3500);}
      else{msg.className='msg err';msg.textContent='✗ '+(d.error||'Error desconocido');
        btn.disabled=false;btn.textContent='Guardar y reconectar';}})
    .catch(()=>{msg.className='msg err';msg.textContent='✗ Sin respuesta';
      btn.disabled=false;btn.textContent='Guardar y reconectar';});
}
</script>
</body></html>
)rawliteral";

const char PAGE_ADVANCED[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Avanzado</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Segoe UI',sans-serif;background:#0d1117;color:#e6edf3;min-height:100vh}
header{background:#161b22;padding:14px 20px;display:flex;justify-content:space-between;align-items:center;border-bottom:1px solid #30363d}
header h1{font-size:1.1rem;color:#58a6ff;font-family:monospace}
nav a{color:#58a6ff;text-decoration:none;margin-left:14px;font-size:.85rem}
main{max-width:520px;margin:28px auto;padding:0 14px}
.card{background:#161b22;border-radius:10px;padding:24px;margin-bottom:20px;border:1px solid #30363d}
h2{font-size:.82rem;color:#58a6ff;font-family:monospace;margin-bottom:14px;padding-bottom:8px;border-bottom:1px solid #30363d;text-transform:uppercase;letter-spacing:.08em}
label{display:block;font-size:.79rem;color:#7d8590;margin-bottom:4px;margin-top:13px}
label:first-of-type{margin-top:0}
input{width:100%;padding:9px 12px;background:#0d1117;border:1px solid #30363d;border-radius:8px;color:#e6edf3;font-size:.9rem;outline:none;transition:border .2s}
input:focus{border-color:#58a6ff}
.hint{font-size:.72rem;color:#555;margin-top:3px}
button{margin-top:18px;width:100%;padding:11px;background:#58a6ff;color:#0d1117;border:none;border-radius:8px;font-size:.95rem;font-weight:700;cursor:pointer;transition:opacity .2s}
button:hover{opacity:.85}
button:disabled{opacity:.35;cursor:default}
.msg{margin-top:10px;padding:10px 13px;border-radius:8px;font-size:.84rem;display:none}
.msg.ok{background:#1a3a1a;color:#3fb950;border:1px solid #3fb950;display:block}
.msg.err{background:#2d1a1a;color:#f85149;border:1px solid #f85149;display:block}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:9px}
.info-item{background:#0d1117;border-radius:8px;padding:10px}
.k{font-size:.71rem;color:#555;margin-bottom:3px}
.v{font-size:.83rem;color:#aaa;word-break:break-all;font-family:monospace}
.diag{margin-top:14px;padding:12px;border-radius:8px;background:#0d1117;border:1px solid #30363d;font-family:monospace;font-size:.76rem;line-height:1.4;color:#c9d1d9;white-space:pre-wrap;word-break:break-word}
.logs{margin-top:14px;display:grid;gap:8px}
.log-item{padding:10px 12px;border-radius:8px;background:#0d1117;border:1px solid #30363d;font-family:monospace;font-size:.75rem;line-height:1.35;color:#c9d1d9}
.log-item .t{display:block;color:#58a6ff;margin-bottom:4px}
.meta{display:flex;gap:14px;flex-wrap:wrap;font-size:.8rem;color:#7d8590}
</style>
</head>
<body>
<header>
  <h1>⚙ Avanzado</h1>
  <nav><a href="/config">← Config</a></nav>
</header>
<main>
  <div class="card">
    <h2>Estado del sistema</h2>
    <div class="grid" id="grid"><div class="info-item"><div class="k">Cargando…</div></div></div>
  </div>
  <div class="card">
    <h2>Red avanzada</h2>
    <label>ID de Dispositivo</label>
    <input type="text" id="devid" maxlength="31" placeholder="AABBCC">
    <p class="hint">Identificador único del dispositivo. Se usa en topics MQTT y AP.</p>
    <label>Prefijo del AP</label>
    <input type="text" id="apprefix" maxlength="15" placeholder="Nariz Digital-">
    <p class="hint">Nombre base del punto de acceso. El ID del dispositivo se agrega al final.</p>
    <label>Hostname mDNS</label>
    <input type="text" id="mdns" maxlength="31" placeholder="aire">
    <p class="hint">Nombre local del dispositivo en la red. Disponible como <b>[hostname].local</b>.</p>
  </div>
  <div class="card">
    <h2>Calibración ADC0</h2>
    <label>Valor ADC0 (calibración MQ)</label>
    <input type="text" id="adc0" inputmode="decimal" placeholder="2000">
    <p class="hint">Valor de referencia ADC en aire limpio. Se usa para calcular la concentración de COVs.</p>
    <button id="btnCal" onclick="calibrar()">Calibrar ahora (20 lecturas)</button>
    <div class="msg" id="msgCal"></div>
  </div>
  <div class="card">
    <button id="btnSave" onclick="guardar()">Guardar ajustes avanzados</button>
    <div class="msg" id="msg"></div>
  </div>
  <div class="card">
    <h2>Diagnóstico MQTT</h2>
    <div class="meta">
      <span>Firmware: <b id="fwVer">--</b></span>
      <span>MQTT: <b id="mqttState">--</b></span>
      <span>RSSI: <b id="wifiRssi">--</b></span>
      <span>Clientes AP: <b id="apClients">--</b></span>
      <span>Calibración: <b id="calState">--</b></span>
    </div>
    <div class="diag" id="mqttDiag">Cargando diagnóstico…</div>
  </div>
</main>
<script>
function fmtUp(s){const h=Math.floor(s/3600),m=Math.floor((s%3600)/60),sec=s%60;
  return(h?h+'h ':'')+( m?m+'m ':'')+sec+'s';}

fetch('/api/status').then(r=>r.json()).then(d=>{
  const items=[
    ['Dispositivo',d.device_id||'—'],['IP STA',d.wifi_ip||'Sin conexión'],
    ['IP AP','192.168.4.1'],['Sensor',(parseFloat(d.sensor_value)||0).toFixed(2)+' ppm'],
    ['Calibrado',d.calibrated?'Sí':'No'],['ADC0',(parseFloat(d.calibration_adc0)||0).toFixed(0)],
    ['AP Prefix',d.ap_prefix||'Nariz Digital-'],['mDNS',(d.mdns_hostname||'aire')+'.local'],
    ['Uptime',fmtUp(d.uptime||0)]];
  document.getElementById('grid').innerHTML=items.map(([k,v])=>
    `<div class="info-item"><div class="k">${k}</div><div class="v">${v}</div></div>`).join('');
  document.getElementById('devid').value=d.device_id||'';
  document.getElementById('apprefix').value=d.ap_prefix||'Nariz Digital-';
  document.getElementById('mdns').value=d.mdns_hostname||'aire';
  document.getElementById('adc0').value=d.calibration_adc0||'';
  document.getElementById('fwVer').textContent=d.firmware_version||'--';
  document.getElementById('mqttState').textContent=d.mqtt_status||((d.mqtt!==undefined)?(d.mqtt?'Conectado':'Desconectado'):'--');
  document.getElementById('wifiRssi').textContent=(d.wifi_rssi!==undefined&&d.wifi_rssi!==null)?d.wifi_rssi+' dBm':'--';
  document.getElementById('apClients').textContent=(d.ap_clients!==undefined)?d.ap_clients:'--';
  document.getElementById('calState').textContent=d.calibration_status||(d.calibrated?'Calibrado':'Sin calibrar');
  const lastPublish=d.mqtt_last_publish_topic?`Último publish: ${d.mqtt_last_publish_topic}\nEstado: ${d.mqtt_last_publish_ok?'OK':'FALLÓ'}\nPayload: ${d.mqtt_last_publish_payload||'--'}`:'Último publish: --';
  const lastInbound=d.mqtt_last_inbound_topic?`Último comando: ${d.mqtt_last_inbound_command||'--'}\nTopic: ${d.mqtt_last_inbound_topic}\nPayload: ${d.mqtt_last_inbound_payload||'--'}`:'Último comando: --';
  const reconnect=`Reconexión MQTT: ${d.mqtt_last_reconnect_state!==undefined?d.mqtt_last_reconnect_state:'--'}\nEstado textual: ${d.mqtt_status||'--'}`;
  document.getElementById('mqttDiag').textContent=`${lastPublish}\n\n${lastInbound}\n\n${reconnect}`;
}).catch(()=>{});

function calibrar(){
  const btn=document.getElementById('btnCal'),msg=document.getElementById('msgCal');
  btn.disabled=true;btn.textContent='Calibrando (10s)…';msg.className='msg';
  fetch('/calibrate').then(r=>r.json()).then(d=>{
    if(d.ok){msg.className='msg ok';msg.textContent='✓ Calibrado OK – ADC0: '+d.adc0;
      document.getElementById('adc0').value=d.adc0;}
    else{msg.className='msg err';msg.textContent='✗ '+(d.error||'Error');}
    btn.disabled=false;btn.textContent='Calibrar ahora (20 lecturas)';
  }).catch(()=>{msg.className='msg err';msg.textContent='✗ Sin respuesta';
    btn.disabled=false;btn.textContent='Calibrar ahora (20 lecturas)';});
}

function guardar(){
  const btn=document.getElementById('btnSave'),msg=document.getElementById('msg');
  btn.disabled=true;btn.textContent='Guardando…';msg.className='msg';
  const p=new URLSearchParams({
    devid:document.getElementById('devid').value.trim(),
    apprefix:document.getElementById('apprefix').value.trim(),
    mdns:document.getElementById('mdns').value.trim(),
    adc0:document.getElementById('adc0').value.trim()});
  fetch('/config',{method:'POST',body:p,headers:{'Content-Type':'application/x-www-form-urlencoded'}})
    .then(r=>r.json())
    .then(d=>{
      if(d.ok){msg.className='msg ok';msg.textContent='✓ Guardado correctamente.';
        setTimeout(()=>{msg.className='msg';btn.textContent='Guardar ajustes avanzados';},3000);}
      else{msg.className='msg err';msg.textContent='✗ '+(d.error||'Error desconocido');}
      btn.disabled=false;btn.textContent='Guardar ajustes avanzados';})
    .catch(()=>{msg.className='msg err';msg.textContent='✗ Sin respuesta';
      btn.disabled=false;btn.textContent='Guardar ajustes avanzados';});
}
</script>
</body></html>
)rawliteral";
// ============================================================================
// WEBSOCKET HANDLER
// ============================================================================

void onWsEvent(AsyncWebSocket* srv, AsyncWebSocketClient* client,
               AwsEventType type, void* arg, uint8_t* data, size_t len) {
  if (type == WS_EVT_CONNECT) {
    Serial.printf("[WS %u] conectado\n", client->id());
    client->text(buildWsStatusJson());
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.printf("[WS %u] desconectado\n", client->id());
  } else if (type == WS_EVT_DATA) {
    String msg = "";
    for (size_t i = 0; i < len; i++) msg += (char)data[i];
    if (msg == "getStatus")  client->text(buildWsStatusJson());
    if (msg == "getHistory") client->text(buildWsHistoryJson());
  }
}

// ============================================================================
// SETUP / LOOP
// ============================================================================

void setupWeb() {
  registerHistoryHooks();  // El historial de la gráfica se registra junto con la web

  // ── LittleFS (archivos estáticos) ─────────────────────────────────────────
  if (!LittleFS.begin(false)) {
    Serial.println("[Web] LittleFS: primer montaje fallido, formateando…");
    LittleFS.begin(true);
  }
  Serial.printf("[Web] LittleFS montado – chart.umd.js: %s\n",
                LittleFS.exists("/chart.umd.js") ? "OK" : "NO ENCONTRADO");

  // ── DNS hijack: cualquier dominio → IP del AP ─────────────────────────────
  // TTL bajo (60 s) para que los dispositivos no cacheen demasiado
  dnsServer.setTTL(60);
  dnsServer.start(53, "*", WiFi.softAPIP());
  dnsRunning = true;
  Serial.printf("DNS portal cautivo activo – * → %s\n",
                WiFi.softAPIP().toString().c_str());

  // ── WebSocket ─────────────────────────────────────────────────────────────
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);

  // ── Rutas ─────────────────────────────────────────────────────────────────

  server.serveStatic("/chart.umd.js", LittleFS, "/chart.umd.js")
        .setCacheControl("max-age=86400");

  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/html", PAGE_INDEX);
  });

  server.on("/config", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/html", PAGE_CONFIG);
  });

  server.on("/advanced", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/html", PAGE_ADVANCED);
  });

  // POST /config  — parámetros en cuerpo application/x-www-form-urlencoded
  server.on("/config", HTTP_POST, [](AsyncWebServerRequest* req){
    bool changed = false;

    // Helper lambda para leer param POST
    auto postParam = [&](const char* name) -> String {
      if (req->hasParam(name, true)) return req->getParam(name, true)->value();
      return "";
    };

    String ssid  = postParam("ssid");
    String pass  = postParam("pass");
    String devid = postParam("devid");
    String apprefix = postParam("apprefix");
    String lat   = postParam("lat");
    String lng   = postParam("lng");
    String adc0  = postParam("adc0");
    String mdns  = postParam("mdns");

    if (ssid.length()  > 0) { setWiFiCredentials(ssid.c_str(), pass.c_str(), false); changed = true; }
    if (pass.length()  == 0 && ssid.length() > 0) { setWiFiCredentials(ssid.c_str(), "", false); changed = true; }
    if (devid.length() > 0) { devid.toCharArray(config.device_id, sizeof(config.device_id)); changed = true; }
    if (apprefix.length() > 0) { setApNamePrefix(apprefix.c_str(), false); changed = true; }
    if (lat.length() > 0) {
      float v = lat.toFloat();
      if (v >= -90 && v <= 90) { config.latitude = v; changed = true; }
    }
    if (lng.length() > 0) {
      float v = lng.toFloat();
      if (v >= -180 && v <= 180) { config.longitude = v; changed = true; }
    }
    if (adc0.length() > 0) {
      float v = adc0.toFloat();
      if (mq135.calibrate(v)) {
        changed = true;
      }
    }
    if (mdns.length() > 0 && mdns.length() < 32) {
      mdns.toCharArray(config.mdnsHostname, sizeof(config.mdnsHostname));
      changed = true;
    }

    if (changed) {
      saveConfig();
      req->send(200, "application/json", "{\"ok\":true}");
      Serial.println("Config actualizada via web – reconectando STA…");
      WiFi.disconnect(false);
      delay(200);
      wifiStartSTAConnect();
    } else {
      req->send(400, "application/json",
                "{\"ok\":false,\"error\":\"Sin parametros validos\"}");
    }
  });

  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "application/json", buildStatusJson());
  });

  server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest* req){
    // Si el flag está activo, el loopWeb() aún no ha iniciado el scan — reportar en progreso
    if (g_wifiScanRequested) {
      req->send(200, "application/json", "{\"scanning\":true}");
      return;
    }
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      req->send(200, "application/json", "{\"scanning\":true}");
      return;
    }
    if (n >= 0) {
      DynamicJsonDocument doc(3072);
      JsonArray arr = doc.createNestedArray("networks");
      for (int i = 0; i < n; i++) {
        String ssid = WiFi.SSID(i);
        if (ssid.length() == 0) continue;
        JsonObject obj = arr.createNestedObject();
        obj["ssid"] = ssid;
        obj["rssi"] = WiFi.RSSI(i);
      }
      WiFi.scanDelete();
      String out; serializeJson(doc, out);
      req->send(200, "application/json", out);
      return;
    }
    // Sin resultados previos: si se pide con ?start=1 marcar flag para el loop principal
    if (req->hasParam("start")) {
      g_wifiScanRequested = true;
      req->send(200, "application/json", "{\"scanning\":true}");
    } else {
      req->send(200, "application/json", "{\"scanning\":false,\"networks\":[]}");
    }
  });

  server.on("/calibrate", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!mq135.isInitialized()) {
      req->send(503, "application/json",
                "{\"ok\":false,\"error\":\"Sensor no inicializado\"}");
      return;
    }
    float sum = 0;
    for (int i = 0; i < 20; i++) { sum += (float)analogRead(34); delay(500); }
    float adc0 = sum / 20.0f;
    if (adc0 < 100 || adc0 > 4000) {
      req->send(400, "application/json",
                "{\"ok\":false,\"error\":\"ADC0 fuera de rango\",\"adc0\":" + String(adc0,0) + "}");
      return;
    }
    mq135.calibrate(adc0);
    saveConfig();
    req->send(200, "application/json",
              "{\"ok\":true,\"adc0\":" + String(adc0,0) + "}");
    Serial.printf("Calibración via web OK – ADC0=%.0f\n", adc0);
  });

  // ── Catch-all → portal cautivo ────────────────────────────────────────────
  // CRÍTICO: cualquier URL no reconocida redirige a "/"
  // Esto es lo que hace que el "Sign in to network" aparezca en iOS/Android
  server.onNotFound([](AsyncWebServerRequest* req){
    req->redirect("http://192.168.4.1/");
  });

  server.begin();
  Serial.printf("Servidor web listo – http://%s\n",
                WiFi.softAPIP().toString().c_str());

  // mDNS: anuncia el hostname del servidor web en la red local
  if (MDNS.begin(config.mdnsHostname)) {
    MDNS.addService("http", "tcp", 80);
    MDNS.addService("ws",   "tcp", 81);
    Serial.printf("✓ mDNS → %s.local\n", config.mdnsHostname);
  }
}

// Llamar en loop() — AsyncWebServer no necesita handleClient()
// Solo procesar DNS y limpiar WebSocket
void loopWeb() {
  if (dnsRunning) dnsServer.processNextRequest();
  ws.cleanupClients();
  // WiFi.scanNetworks() DEBE llamarse desde el task principal (loop),
  // nunca desde un handler de AsyncWebServer (task async_tcp) → crash
  if (g_wifiScanRequested) {
    g_wifiScanRequested = false;
    WiFi.scanNetworks(true);
  }
}

#endif // WEBS_H