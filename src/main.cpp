#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <SoftwareSerial.h>
#include <TinyGPSPlus.h>
#include <time.h>
#include <EEPROM.h>
#include <Updater.h>
#include <memory>
#include "ntp_fallback.h"
#include "nmea_timing.h"
#include "gps_calibration.h"

// GPS TX -> D5. RX is not needed. All GPIO signals must be 3.3 V.
SoftwareSerial gpsSerial(D5, -1);
TinyGPSPlus gps;
NmeaTiming gpsTiming;
uint32_t gpsWireCorrectionUs = 0, gpsTimingRejected = 0, gpsOverflows = 0;
GpsCalibration gpsCalibration;
uint32_t rawGpsSeconds = 0, rawGpsMicros = 0, lastNtpDelayUs = 0;
bool gpsCalibrated = false, ntpMeasured = false;
IPAddress calibrationAddress;
uint8_t calibrationLeap = 0;
TinyGPSCustom satellitesInView(gps, "GPGSV", 3);
ESP8266WebServer web(80);
DNSServer dns;
WiFiUDP ntp;
WiFiUDP upstream;
String fallbackHost;
String fallbackState = "deaktiviert";
IPAddress upstreamAddress;
IPAddress referenceAddress;
uint8_t upstreamNonce[8] = {};
bool upstreamPending = false, upstreamAttempted = false;
uint32_t upstreamSentMicros = 0, upstreamLastAttempt = 0;
bool gpsSeen = false, networkTime = false;
uint32_t lastGpsMillis = 0, baseUncertaintyUs = 1000000;
uint8_t clockStratum = 1, clockLeap = 0;
uint32_t resetAt = 0;
bool resetPending = false;
String otaPassword, otaError;
constexpr char DEFAULT_OTA_PASSWORD[] = "gpsntp-update";
std::unique_ptr<UpdaterClass> otaWriter;
unsigned otaFiles = 0;
bool otaEnded = false, otaRestartPending = false;
uint32_t otaLastActivity = 0, otaRestartAt = 0;
String apName;
bool portal = false, mdns = false;
bool wifiWasConnected = false;
uint32_t connectedSince = 0;
uint32_t lastPortalAttempt = 0, lastDiagnostic = 0;
bool portalAttempted = false;
uint32_t disconnectedAt = 0, lastRetry = 0;
uint32_t referenceSeconds = 0, referenceMicros = 0, lastSyncMillis = 0;
uint32_t requests = 0;
bool clockValid = false;
constexpr uint32_t MAX_AGE_MS = 60000;
constexpr uint32_t NTP_EPOCH = 2208988800UL;

// Gregorian UTC calendar -> Unix seconds, independent of local timezone.
uint32_t unixSeconds(int year, unsigned month, unsigned day, unsigned h, unsigned m, unsigned s) {
  year -= month <= 2;
  const int era = year / 400;
  const unsigned yoe = year - era * 400;
  const unsigned doy = (153 * (month > 2 ? month - 3 : month + 9) + 2) / 5 + day - 1;
  const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return uint32_t((int64_t(era) * 146097 + doe - 719468) * 86400 + h * 3600 + m * 60 + s);
}

bool synchronized() { return clockValid && uint32_t(millis() - lastSyncMillis) < MAX_AGE_MS; }
uint32_t uncertaintyUs() {
  // Source-specific engineering allowance plus 100 ppm holdover; not a measured error.
  return baseUncertaintyUs + uint32_t(millis() - lastSyncMillis) / 10;
}
bool gpsFresh() { return gpsSeen && uint32_t(millis() - lastGpsMillis) < 5000; }
void applyGpsReference() {
  gpsCalibrated = gpsCalibration.ready(millis());
  GpsCalibration::correctedAnchor(rawGpsSeconds, rawGpsMicros,
                                  gpsCalibrated ? gpsCalibration.offsetUs : 0,
                                  referenceSeconds, referenceMicros);
  lastSyncMillis = lastGpsMillis; clockValid = true; networkTime = false;
  baseUncertaintyUs = gpsCalibrated ? gpsCalibration.uncertaintyUs + gpsCalibration.age(millis()) / 10 : 1000000;
  clockStratum = gpsCalibrated ? gpsCalibration.stratum : 1;
  clockLeap = gpsCalibrated ? calibrationLeap : 0;
  if (gpsCalibrated) referenceAddress = calibrationAddress;
}
bool saveFallback(const String &host) {
  // Dedicated EEPROM sector: version marker plus a NUL-terminated host name.
  EEPROM.write(0, 0xA7);
  for (size_t i = 0; i < 64; ++i) EEPROM.write(i + 1, i < host.length() ? host[i] : 0);
  return EEPROM.commit();
}
void loadFallback() {
  EEPROM.begin(136);
  if (EEPROM.read(0) != 0xA7) return;
  char host[64];
  for (size_t i = 0; i < sizeof(host); ++i) host[i] = EEPROM.read(i + 1);
  host[63] = 0;
  if (Fallback::validHost(host)) { fallbackHost = host; fallbackState = "bereit"; }
}
bool validOtaPassword(const String &password) {
  if (password.length() < 8 || password.length() > 64) return false;
  for (size_t i = 0; i < password.length(); ++i)
    if (uint8_t(password[i]) < 33 || uint8_t(password[i]) > 126) return false;
  return true;
}
bool saveOtaPassword(const String &password) {
  uint8_t previous[66];
  for (unsigned i = 0; i < sizeof(previous); ++i) previous[i] = EEPROM.read(68 + i);
  EEPROM.write(68, 0xB6); EEPROM.write(69, password.length());
  for (unsigned i = 0; i < 64; ++i) EEPROM.write(70 + i, i < password.length() ? password[i] : 0);
  if (EEPROM.commit()) return true;
  for (unsigned i = 0; i < sizeof(previous); ++i) EEPROM.write(68 + i, previous[i]);
  return false;
}
void loadOtaPassword() {
  uint8_t length = EEPROM.read(69);
  String password;
  if (EEPROM.read(68) == 0xB6 && length >= 8 && length <= 64)
    for (unsigned i = 0; i < length; ++i) password += char(EEPROM.read(70 + i));
  if (!validOtaPassword(password)) {
    // Migrate the former randomly generated password to the documented default once.
    password = DEFAULT_OTA_PASSWORD;
    if (!saveOtaPassword(password)) { Serial.println("[OTA] Passwort konnte nicht gespeichert werden; OTA deaktiviert."); return; }
  }
  otaPassword = password;
  Serial.println(otaPassword == DEFAULT_OTA_PASSWORD ? "[OTA] Standardpasswort aktiv (siehe README)." : "[OTA] Eigenes OTA-Passwort aktiv.");
}
void put32(uint8_t *p, uint32_t v) {
  p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}
void timestamp(uint8_t *p) {
  if (!clockValid) { memset(p, 0, 8); return; }
  uint32_t elapsed = micros() - referenceMicros;
  put32(p, referenceSeconds + NTP_EPOCH + elapsed / 1000000UL);
  put32(p + 4, (uint64_t(elapsed % 1000000UL) << 32) / 1000000UL);
}
void readGps() {
  // Decode the ISR buffer before checking whether it overflowed.
  gpsSerial.available();
  bool overflow = gpsSerial.overflow();
  if (overflow) ++gpsOverflows;
  gpsTiming.service(micros(), overflow);
  // Bound work per loop so DNS, HTTP and NTP remain responsive.
  for (unsigned i = 0; i < 256 && gpsSerial.available(); ++i) {
    char c = gpsSerial.read();
    uint32_t receivedAt = micros();
    gpsTiming.observe(c, receivedAt);
    if (!gps.encode(c) || !gpsTiming.isRmc()) continue;
    if (!gpsTiming.usable(receivedAt)) { ++gpsTimingRejected; continue; }
    if (!gps.time.isUpdated() || !gps.date.isUpdated() || !gps.date.isValid() || !gps.time.isValid() ||
        gps.date.age() > 1500 || gps.time.age() > 500 ||
        !gps.location.isValid() || gps.location.age() > 2000 ||
        gps.date.year() < 2024 || gps.date.year() > 2099 || gps.time.second() > 59) continue;
    uint32_t epoch = unixSeconds(gps.date.year(), gps.date.month(), gps.date.day(),
                                 gps.time.hour(), gps.time.minute(), gps.time.second());
    // GGA and RMC may describe the same second. Do not reset the phase twice.
    if (gpsSeen && epoch == rawGpsSeconds) continue;
    gpsWireCorrectionUs = gpsTiming.correctionUs();
    uint32_t anchor = receivedAt - gpsWireCorrectionUs - gps.time.centisecond() * 10000UL;
    if (gpsSeen) {
      int64_t phaseError = int64_t(uint32_t(anchor - rawGpsMicros)) -
                           (int64_t(epoch) - rawGpsSeconds) * 1000000;
      if (!gpsFresh() || phaseError < -20000 || phaseError > 20000) gpsCalibration.reset();
    }
    rawGpsSeconds = epoch; rawGpsMicros = anchor;
    gpsSeen = true; lastGpsMillis = millis();
    applyGpsReference();
  }
}
void pollFallback() {
  if (!fallbackHost.length() || WiFi.status() != WL_CONNECTED) {
    upstreamPending = false;
    upstreamAttempted = false;
    return;
  }
  if (upstreamPending) {
    int size = upstream.parsePacket();
    if (size) {
      uint32_t receivedMicros = micros();
      uint8_t packet[48] = {};
      int count = upstream.read(packet, sizeof(packet));
      while (upstream.available()) upstream.read();
      Fallback::Sample sample;
      if (upstream.remoteIP() == upstreamAddress && upstream.remotePort() == 123 &&
          count == 48 && Fallback::decode(packet, size, upstreamNonce,
                                         receivedMicros - upstreamSentMicros, sample)) {
        lastNtpDelayUs = sample.delayUs;
        ntpMeasured = true;
        if (gpsFresh()) {
          if (uint32_t(millis() - lastGpsMillis) < 1500) {
            int64_t rawTime = int64_t(rawGpsSeconds) * 1000000 + uint32_t(receivedMicros - rawGpsMicros);
            int64_t ntpTime = int64_t(sample.seconds) * 1000000 + sample.fractionUs;
            if (gpsCalibration.add(ntpTime - rawTime, sample.delayUs, sample.uncertaintyUs,
                                   sample.stratum, millis(), uint32_t(upstreamAddress), sample.leap)) {
              calibrationAddress = IPAddress(gpsCalibration.referenceId);
              calibrationLeap = gpsCalibration.leapIndicator;
              applyGpsReference();
              fallbackState = gpsCalibrated ? "GPS-Kalibrierung aktiv" : "sammle GPS-Vergleichsmessungen";
            } else fallbackState = "Vergleich zu ungenau; Messung verworfen";
          } else fallbackState = "GPS fuer Vergleich zu alt";
        } else {
          referenceSeconds = sample.seconds;
          referenceMicros = receivedMicros - sample.fractionUs;
          lastSyncMillis = millis(); clockValid = true; networkTime = true; gpsCalibrated = false;
          baseUncertaintyUs = sample.uncertaintyUs;
          clockStratum = sample.stratum; clockLeap = sample.leap;
          referenceAddress = upstreamAddress;
          fallbackState = "Zeit empfangen";
        }
        upstreamAttempted = true;
        upstreamPending = false;
      } else fallbackState = "ungueltige Antwort";
    }
    if (upstreamPending && uint32_t(micros() - upstreamSentMicros) > 1500000) {
      upstreamPending = false; fallbackState = "keine gueltige Antwort (Timeout)";
    }
    return;
  }
  if (upstreamAttempted && uint32_t(millis() - upstreamLastAttempt) < 30000) return;
  upstreamAttempted = true; upstreamLastAttempt = millis();
  fallbackState = "Server wird aufgeloest";
  // DNS resolution is limited to one second; packet reception itself is non-blocking.
  if (!WiFi.hostByName(fallbackHost.c_str(), upstreamAddress, 1000)) {
    fallbackState = "DNS-Aufloesung fehlgeschlagen"; return;
  }
  if (upstreamAddress == WiFi.localIP() || upstreamAddress == WiFi.softAPIP() ||
      upstreamAddress[0] == 127 || upstreamAddress[0] == 0 || upstreamAddress[0] >= 224 ||
      upstreamAddress[3] == 255) {
    fallbackState = "ungueltige oder eigene Serveradresse"; return;
  }
  // Rebind to discard stale packets. Requests use an unpredictable originate cookie.
  upstream.stop();
  if (!upstream.begin(uint16_t(49152 + (os_random() % 16000)))) {
    fallbackState = "UDP-Port nicht verfuegbar"; return;
  }
  uint8_t packet[48] = {}; packet[0] = 0x23;
  put32(upstreamNonce, os_random()); put32(upstreamNonce + 4, os_random());
  memcpy(packet + 40, upstreamNonce, 8);
  upstreamSentMicros = micros();
  if (!upstream.beginPacket(upstreamAddress, 123) || upstream.write(packet, sizeof(packet)) != sizeof(packet) || !upstream.endPacket()) {
    fallbackState = "Senden fehlgeschlagen"; return;
  }
  upstreamPending = true; fallbackState = "warte auf Antwort";
}
void serveNtp() {
  int size = ntp.parsePacket();
  if (!size) return;
  uint8_t receiveTime[8]; timestamp(receiveTime);
  uint8_t packet[48] = {};
  int count = ntp.read(packet, sizeof(packet));
  while (ntp.available()) ntp.read();
  unsigned version = (packet[0] >> 3) & 7;
  if (size < 48 || count < 48 || (packet[0] & 7) != 3 || version < 3 || version > 4) return;
  uint8_t reply[48] = {};
  bool valid = synchronized();
  reply[0] = (valid ? (clockLeap << 6) : 0xC0) | (version << 3) | 4;
  reply[1] = valid ? clockStratum : 16;
  reply[2] = 6;
  reply[3] = networkTime || gpsCalibrated ? uint8_t(-10) : 0;
  put32(reply + 8, valid ? (uint64_t(uncertaintyUs()) * 65536 / 1000000) : 0xFFFF0000UL);
  if (networkTime || gpsCalibrated) for (unsigned i = 0; i < 4; ++i) reply[12 + i] = referenceAddress[i];
  else memcpy(reply + 12, "GPS", 3);
  if (clockValid) put32(reply + 16, referenceSeconds + NTP_EPOCH);
  memcpy(reply + 24, packet + 40, 8);
  memcpy(reply + 32, receiveTime, 8);
  timestamp(reply + 40);
  ntp.beginPacket(ntp.remoteIP(), ntp.remotePort());
  ntp.write(reply, sizeof(reply)); ntp.endPacket(); ++requests;
}

const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="de"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>GPS NTP</title>
<style>body{font:17px system-ui;background:#101b2d;color:#edf4ff;max-width:760px;margin:40px auto;padding:20px}section{background:#1c2c43;padding:24px;border-radius:16px;margin:18px 0}h1{margin-bottom:6px}dl{display:grid;grid-template-columns:1fr 1fr;gap:14px}dd{margin:0;text-align:right}input,button{font:inherit;padding:12px;box-sizing:border-box;border-radius:8px;border:0;margin:6px 0;width:100%}button{background:#72dcbd;color:#10251f;cursor:pointer}small{color:#bac8db}#message{color:#72dcbd}</style>
<h1>GPS → NTP</h1><small>Wemos D1 mini · u-blox NEO-6M · UTC</small>
<section><h2 id="state">Status wird geladen …</h2><dl id="values"></dl><small>Die Unsicherheit ist eine konservative Planungsannahme, keine gemessene Genauigkeit. Ohne PPS können NMEA-Verzögerungen die Zeit verschieben.</small></section>
<section><h2>Notfall-NTP und GPS-Kalibrierung</h2><form id="fallback"><label>NTP-Server (Hostname oder IPv4)<input id="fallbackHost" name="host" list="ntpServers" maxlength="63" placeholder="Leer = deaktiviert" autocomplete="off"></label><datalist id="ntpServers"><option value="pool.ntp.org"><option value="de.pool.ntp.org"></datalist><button>Notfall-NTP speichern</button></form><p id="fallbackMessage" role="status"></p><small>Der Server wird alle 30 Sekunden abgefragt: bei GPS-Empfang zur Kalibrierung, bei GPS-Ausfall als Ersatz. Ab drei stabilen Vergleichsmessungen wird die GPS-Zeit korrigiert. Ein naher, genauer NTP-Server verbessert die Voraussetzungen. Leer speichern deaktiviert auch die Kalibrierung. Millisekundengenauigkeit ist nicht garantiert.</small></section>
<section><h2>Firmware aktualisieren</h2><form id="ota"><label>Firmware (.bin für D1 mini)<input id="firmware" type="file" accept=".bin" required></label><label>OTA-Passwort<input id="otaPassword" type="password" required autocomplete="off"></label><button>Firmware hochladen und neu starten</button></form><progress id="otaProgress" value="0" max="100" style="width:100%" hidden></progress><p id="otaMessage" role="status"></p><small>Das Standardpasswort steht in der README und kann unten geändert werden. WLAN und Notfall-NTP bleiben erhalten. Während des Updates die Stromversorgung aufrechterhalten.</small></section>
<section><h2>OTA-Passwort ändern</h2><form id="changeOtaPassword"><label>Aktuelles Passwort<input id="currentOtaPassword" type="password" required autocomplete="current-password"></label><label>Neues Passwort<input name="password" id="newOtaPassword" type="password" minlength="8" maxlength="64" pattern="[!-~]{8,64}" required autocomplete="new-password"></label><label>Neues Passwort wiederholen<input name="confirmation" type="password" minlength="8" maxlength="64" required autocomplete="new-password"></label><button>OTA-Passwort speichern</button></form><p id="passwordMessage" role="status"></p><small>8–64 Zeichen: Buchstaben ohne Umlaute, Ziffern und ASCII-Sonderzeichen, keine Leerzeichen. Die Änderung gilt sofort und bleibt nach Neustart und WLAN-Reset erhalten.</small></section>
<section><h2>WLAN zurücksetzen</h2><p>Löscht die gespeicherten WLAN-Zugangsdaten und startet das Gerät neu. Danach ist die Einrichtung wieder über den Access Point möglich. Der Notfall-NTP bleibt gespeichert.</p><button id="reset" style="background:#ffb1a8">WLAN löschen und neu starten</button><p id="resetMessage" role="status"></p></section>
<script>
const fields=[['utc','Zeit (UTC)'],['source','Aktive Zeitquelle'],['stratum','NTP-Stratum'],['satellites','Satelliten im Fix'],['visible','Satelliten sichtbar'],['hdop','HDOP (Position)'],['uncertainty','Geschätzte Unsicherheit'],['calibration','GPS-Kalibrierung'],['gpsOffset','GPS-Korrektur durch NTP'],['ntpDelay','NTP-Netzlaufzeit'],['calibrationSpread','Streuung der Vergleiche'],['wireCorrection','UART-Zeitkorrektur'],['timingRejected','Verzögerte GPS-Sätze verworfen'],['gpsOverflows','GPS-Pufferüberläufe'],['age','Letzte Zeitübernahme'],['fallbackHost','Notfall-NTP'],['fallbackState','Ersatzserver-Status'],['ip','WLAN-IP / NTP-Adresse'],['ap','Einrichtungs-AP'],['rssi','WLAN-Signal'],['requests','NTP-Antworten']];
let configLoaded=false,resetting=false,updating=false;
async function refresh(){if(resetting||updating)return;try{let s=await(await fetch('/api/status',{cache:'no-store'})).json();document.querySelector('#state').textContent=s.synced?'Zeit verfügbar: '+s.source:'Nicht synchronisiert – warte auf Zeitquelle';if(!configLoaded){document.querySelector('#fallbackHost').value=s.fallbackHost==='deaktiviert'?'':s.fallbackHost;configLoaded=true}let dl=document.querySelector('#values');dl.replaceChildren();for(let[k,label]of fields){let dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=label;dd.textContent=s[k];dl.append(dt,dd)}}catch(e){document.querySelector('#state').textContent='Verbindung unterbrochen'}}
document.querySelector('#ota').onsubmit=e=>{e.preventDefault();let file=document.querySelector('#firmware').files[0];if(!file||!file.size||!file.name.toLowerCase().endsWith('.bin')){document.querySelector('#otaMessage').textContent='Bitte eine nicht leere .bin-Datei auswählen.';return}if(!confirm('Firmware jetzt installieren und Gerät neu starten?'))return;updating=true;document.querySelectorAll('button').forEach(b=>b.disabled=true);let progress=document.querySelector('#otaProgress'),message=document.querySelector('#otaMessage');progress.hidden=false;progress.value=0;message.textContent='Firmware wird hochgeladen …';let xhr=new XMLHttpRequest();xhr.open('POST','/update');xhr.timeout=180000;xhr.setRequestHeader('X-Device-Action','update');xhr.setRequestHeader('X-Update-Password',document.querySelector('#otaPassword').value);xhr.setRequestHeader('X-Firmware-Size',String(file.size));xhr.upload.onprogress=e=>{if(e.lengthComputable){progress.value=Math.round(e.loaded/e.total*100);message.textContent=progress.value===100?'Upload vollständig. Firmware wird geprüft …':'Upload: '+progress.value+' %'}};const unlock=()=>{updating=false;document.querySelectorAll('button').forEach(b=>b.disabled=false)};xhr.onload=()=>{document.querySelector('#otaPassword').value='';message.textContent=xhr.responseText;if(xhr.status===200){resetting=true;message.textContent+=' Seite nach dem Neustart neu laden.'}else unlock()};xhr.onerror=xhr.ontimeout=xhr.onabort=()=>{message.textContent='Keine Bestätigung erhalten. Verbindung prüfen und Seite neu laden, bevor du erneut aktualisierst.';unlock()};let data=new FormData();data.append('firmware',file);xhr.send(data)};
document.querySelector('#changeOtaPassword').onsubmit=async e=>{e.preventDefault();let form=e.target,data=new FormData(form),message=document.querySelector('#passwordMessage');if(data.get('password')!==data.get('confirmation')){message.textContent='Die neuen Passwörter stimmen nicht überein.';return}let button=form.querySelector('button');button.disabled=true;try{let r=await fetch('/ota-password',{method:'POST',headers:{'X-Device-Action':'change-password','X-Update-Password':document.querySelector('#currentOtaPassword').value},body:new URLSearchParams(data)});message.textContent=await r.text();if(r.ok){form.reset();document.querySelector('#otaPassword').value=''}}catch(e){message.textContent='Keine Bestätigung erhalten. Bei erneutem Versuch gegebenenfalls das neue Passwort verwenden.'}finally{button.disabled=false}};
async function postForm(form,path,message,action){let button=form.querySelector('button');button.disabled=true;try{let r=await fetch(path,{method:'POST',headers:action?{'X-Device-Action':action}:{},body:new URLSearchParams(new FormData(form))});document.querySelector(message).textContent=await r.text();if(r.ok)refresh()}catch(e){document.querySelector(message).textContent='Verbindung unterbrochen'}finally{button.disabled=false}}
document.querySelector('#fallback').onsubmit=e=>{e.preventDefault();postForm(e.target,'/fallback','#fallbackMessage','configure')};
document.querySelector('#reset').onclick=async()=>{if(!confirm('WLAN-Zugangsdaten wirklich löschen und neu starten?'))return;let button=document.querySelector('#reset');button.disabled=true;try{let r=await fetch('/reset-wifi',{method:'POST',headers:{'X-Device-Action':'reset'}});document.querySelector('#resetMessage').textContent=await r.text();if(r.ok){resetting=true;document.querySelector('#state').textContent='Neustart – bitte mit dem Einrichtungs-AP verbinden.'}else button.disabled=false}catch(e){document.querySelector('#resetMessage').textContent='Keine Bestätigung erhalten. Verbindung prüfen.';button.disabled=false}};
refresh();setInterval(refresh,2000);
</script></html>)HTML";

const char SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="de"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>WLAN einrichten · GPS NTP</title>
<style>body{font:17px system-ui;background:#101b2d;color:#edf4ff;max-width:560px;margin:40px auto;padding:20px}section{background:#1c2c43;padding:24px;border-radius:16px}input,button,select{font:inherit;padding:12px;box-sizing:border-box;border-radius:8px;border:0;margin:8px 0 18px;width:100%}button{background:#72dcbd;color:#10251f;cursor:pointer}button:disabled{opacity:.5;cursor:wait}small{color:#bac8db}a{color:#72dcbd}p{overflow-wrap:anywhere}</style>
<h1>WLAN einrichten</h1><p>Verbinde deinen GPS-NTP-Server mit deinem WLAN.</p><section>
<label for="networks">Verfügbare Netzwerke</label><select id="networks"><option value="">Netzwerke werden gesucht …</option></select><button id="scan" type="button">Erneut suchen</button><p id="scanState" role="status"></p>
<form id="wifi"><label>Netzwerkname (SSID)<input id="ssid" name="ssid" maxlength="32" required autocomplete="off" placeholder="Netz auswählen oder manuell eingeben"></label><label>WLAN-Passwort<input id="password" name="password" type="password" maxlength="64" autocomplete="new-password"></label><label><input id="showPassword" type="checkbox" style="width:auto;margin:0 8px 16px 0">Passwort anzeigen</label><button id="connect">Speichern und verbinden</button></form><p id="message" role="status"></p><p id="success" hidden><a id="dashboard" href="/dashboard">Zur Statusseite</a><br><small>Falls der Einrichtungs-AP schließt: mit deinem WLAN verbinden und die angezeigte Adresse öffnen.</small></p><small>Versteckte Netzwerke können manuell eingegeben werden. Bei offenen Netzwerken bleibt das Passwort leer.</small></section>
<script>
let scanning=false,connecting=false,finished=false,scanTimer;
const $=s=>document.querySelector(s);
function controls(){$('#connect').disabled=scanning||connecting;$('#scan').disabled=scanning||connecting}
async function scan(restart=false){if(connecting)return;scanning=true;controls();$('#scanState').textContent='Suche nach WLAN-Netzen …';try{let r=await fetch('/api/networks'+(restart?'?rescan=1':''),{cache:'no-store'});if(r.status===202){scanTimer=setTimeout(()=>scan(),1000);return}if(!r.ok)throw Error(await r.text());let networks=await r.json();$('#networks').replaceChildren(new Option('Netz auswählen oder SSID manuell eingeben',''));const seen=new Set();networks.sort((a,b)=>b.rssi-a.rssi);for(let n of networks){if(!n.ssid||seen.has(n.ssid))continue;seen.add(n.ssid);let option=new Option(n.ssid+' ('+n.rssi+' dBm, '+(n.open?'offen':'verschlüsselt')+')',n.ssid);option.dataset.open=n.open?'1':'0';$('#networks').append(option)}$('#scanState').textContent=seen.size+' Netzwerke gefunden.'}catch(e){$('#scanState').textContent='Suche fehlgeschlagen: '+e.message}scanning=false;controls()}
$('#scan').onclick=()=>{clearTimeout(scanTimer);scan(true)};
$('#networks').onchange=()=>{if($('#networks').value){$('#ssid').value=$('#networks').value;$('#password').value='';$('#password').focus()}};
$('#showPassword').onchange=e=>{$('#password').type=e.target.checked?'text':'password'};
$('#wifi').onsubmit=async e=>{e.preventDefault();connecting=true;controls();$('#message').textContent='Verbindung wird aufgebaut …';try{let r=await fetch('/wifi',{method:'POST',headers:{'X-Device-Action':'wifi'},body:new URLSearchParams(new FormData(e.target))});$('#message').textContent=await r.text();if(!r.ok){connecting=false;controls()}}catch(e){connecting=false;controls();$('#message').textContent='Verbindung unterbrochen. Bitte erneut versuchen.'}};
async function status(){try{let s=await(await fetch('/api/wifi',{cache:'no-store'})).json();if(s.connected&&!finished){finished=true;$('#message').textContent='Verbunden! Bitte zum eigenen WLAN wechseln und die Statusseite öffnen: http://'+s.ip+'/';$('#dashboard').href='http://'+s.ip+'/';$('#success').hidden=false;if(!s.setupClient)setTimeout(()=>location.replace('/dashboard'),1800)}else if(connecting&&s.failed){connecting=false;controls();$('#message').textContent='Verbindung fehlgeschlagen. Netzwerkname und Passwort prüfen und erneut versuchen.'}}catch(e){}if(!finished)setTimeout(status,2000)}
scan();status();
</script></html>)HTML";

String jsonString(const String &value) {
  String result = "\"";
  const char hex[] = "0123456789abcdef";
  for (size_t i = 0; i < value.length(); ++i) {
    uint8_t c = value[i];
    if (c == '"' || c == '\\') { result += '\\'; result += char(c); }
    else if (c < 32) { result += "\\u00"; result += hex[c >> 4]; result += hex[c & 15]; }
    else result += char(c);
  }
  return result + "\"";
}
bool setupClient() {
  // Select by the address this HTTP connection reached, not station connectivity.
  return portal && web.client().localIP() == WiFi.softAPIP();
}
bool otaAllowed(const char *action = "update") {
  return WiFi.status() == WL_CONNECTED && !setupClient() && !resetPending &&
         !otaRestartPending && otaPassword.length() &&
         web.header("X-Device-Action") == action && web.header("X-Update-Password") == otaPassword;
}
void uploadFirmware() {
  if (!otaAllowed()) return;
  HTTPUpload &upload = web.upload();
  otaLastActivity = millis();
  if (upload.status == UPLOAD_FILE_START) {
    if (++otaFiles != 1) { otaError = "Nur eine Firmware-Datei pro Update erlaubt."; otaWriter.reset(); return; }
    otaError = ""; otaEnded = false;
    String filename = upload.filename; filename.toLowerCase();
    String sizeHeader = web.header("X-Firmware-Size");
    bool numeric = sizeHeader.length() > 0 && sizeHeader.length() <= 8;
    for (size_t i = 0; i < sizeHeader.length(); ++i) if (sizeHeader[i] < '0' || sizeHeader[i] > '9') numeric = false;
    uint32_t size = numeric ? strtoul(sizeHeader.c_str(), nullptr, 10) : 0;
    uint32_t freeSpace = ESP.getFreeSketchSpace();
    uint32_t maximum = freeSpace > 0x1000 ? (freeSpace - 0x1000) & 0xFFFFF000 : 0;
    if (upload.name != "firmware" || !filename.endsWith(".bin") || size < 16 || size > maximum) {
      otaError = "Ungueltige Firmware-Datei oder Dateigroesse (D1-mini-.bin erforderlich)."; return;
    }
    otaWriter.reset(new UpdaterClass());
    if (!otaWriter->begin(size, U_FLASH)) { otaError = otaWriter->getErrorString(); otaWriter.reset(); }
    else Serial.printf("[OTA] Upload gestartet: %lu Bytes\n", static_cast<unsigned long>(size));
  } else if (upload.status == UPLOAD_FILE_WRITE && otaWriter && !otaError.length()) {
    if (otaWriter->write(upload.buf, upload.currentSize) != upload.currentSize) {
      otaError = otaWriter->getErrorString(); otaWriter.reset();
    }
  } else if (upload.status == UPLOAD_FILE_END && otaWriter && !otaError.length()) {
    otaEnded = true; // Commit only after the entire multipart request has finished.
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    otaWriter.reset(); otaFiles = 0; otaEnded = false;
    otaError = "Upload abgebrochen."; Serial.println("[OTA] Upload abgebrochen.");
  }
  yield();
}
void finishFirmware() {
  web.sendHeader("Cache-Control", "no-store");
  if (!otaAllowed()) { web.send(403, "text/plain; charset=utf-8", "Update nur ueber WLAN-IP mit gueltigem OTA-Passwort erlaubt."); return; }
  bool success = otaFiles == 1 && otaEnded && otaWriter && !otaError.length();
  if (success && !otaWriter->end()) { otaError = otaWriter->getErrorString(); success = false; }
  otaWriter.reset(); otaFiles = 0; otaEnded = false;
  if (!success) {
    web.send(400, "text/plain; charset=utf-8", "Update fehlgeschlagen: " + (otaError.length() ? otaError : String("Firmware unvollstaendig oder fehlt.")));
    otaError = ""; return;
  }
  Serial.println("[OTA] Firmware geprueft. Neustart.");
  web.sendHeader("Connection", "close");
  web.send(200, "text/plain; charset=utf-8", "Firmware installiert. Das Geraet startet neu.");
  otaRestartAt = millis(); otaRestartPending = true;
}
void home() {
  web.sendHeader("Cache-Control", "no-store");
  web.send_P(200, "text/html; charset=utf-8",
             !setupClient() && WiFi.status() == WL_CONNECTED ? PAGE : SETUP_PAGE);
}
void networks() {
  if (!portal) { web.send(403, "text/plain", "WLAN-Suche nur im Setup-Modus."); return; }
  int count = WiFi.scanComplete();
  if (web.arg("rescan") == "1" && count != WIFI_SCAN_RUNNING) { WiFi.scanDelete(); count = WIFI_SCAN_FAILED; }
  web.sendHeader("Cache-Control", "no-store");
  if (count == WIFI_SCAN_FAILED) {
    if (WiFi.scanNetworks(true) == WIFI_SCAN_FAILED) {
      web.send(503, "text/plain; charset=utf-8", "WLAN-Suche konnte nicht gestartet werden. Bitte erneut versuchen."); return;
    }
    web.send(202, "application/json", "{\"scanning\":true}"); return;
  }
  if (count == WIFI_SCAN_RUNNING) { web.send(202, "application/json", "{\"scanning\":true}"); return; }
  String result = "[";
  for (int i = 0; i < count && i < 30; ++i) {
    if (i) result += ',';
    result += "{\"ssid\":" + jsonString(WiFi.SSID(i)) + ",\"rssi\":" + String(WiFi.RSSI(i)) +
              ",\"open\":" + (WiFi.encryptionType(i) == ENC_TYPE_NONE ? "true" : "false") + "}";
  }
  web.send(200, "application/json", result + "]");
}

void startPortal() {
  if (portal) return;
  if (portalAttempted && uint32_t(millis() - lastPortalAttempt) < 5000) return;
  portalAttempted = true; lastPortalAttempt = millis();
  Serial.printf("[WLAN] Starte Einrichtungs-AP: %s\n", apName.c_str());
  const IPAddress apIP(192, 168, 4, 1), mask(255, 255, 255, 0);
  if (!WiFi.mode(WIFI_AP_STA) || !WiFi.softAPConfig(apIP, apIP, mask) ||
      !WiFi.softAP(apName.c_str(), "gpssetup8266", 1, false, 4)) {
    Serial.println("[WLAN] AP-Start fehlgeschlagen; neuer Versuch in 5 Sekunden."); return;
  }
  if (!dns.start(53, "*", WiFi.softAPIP())) {
    Serial.println("[WLAN] DNS-Start fehlgeschlagen; Setup direkt per http://192.168.4.1/ oeffnen.");
  }
  portal = true;
  Serial.printf("Setup: %s / gpssetup8266 / 192.168.4.1\n", apName.c_str());
}
void status() {
  String utc = "—";
  if (synchronized()) {
    time_t now = referenceSeconds + uint32_t(micros() - referenceMicros) / 1000000;
    struct tm calendar; gmtime_r(&now, &calendar);
    char buffer[32]; strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &calendar); utc = buffer;
  }
  String json = "{\"synced\":" + String(synchronized() ? "true" : "false");
  auto field = [&json](const char *key, const String &value) { json += ",\"" + String(key) + "\":\"" + value + "\""; };
  field("utc", utc);
  field("source", synchronized() ? (networkTime ? "Notfall-NTP" : gpsCalibrated ? "GPS + NTP-Kalibrierung" : "GPS (NMEA)") : "keine");
  field("stratum", synchronized() ? String(clockStratum) : "16 (unsynchronisiert)");
  field("fallbackHost", fallbackHost.length() ? fallbackHost : "deaktiviert");
  field("fallbackState", !fallbackHost.length() ? "deaktiviert" :
        WiFi.status() != WL_CONNECTED ? "warte auf WLAN" : fallbackState);
  field("satellites", gps.satellites.isValid() && gps.satellites.age() < 5000 ? String(gps.satellites.value()) : "—");
  field("visible", satellitesInView.isValid() && satellitesInView.age() < 5000 ? String(satellitesInView.value()) : "—");
  field("hdop", gps.hdop.isValid() && gps.hdop.age() < 5000 ? String(gps.hdop.hdop(), 1) : "—");
  field("uncertainty", synchronized() ? String(uncertaintyUs() / 1000.0, 2) + (networkTime || gpsCalibrated ? " ms (NTP-Schaetzung)" : " ms (ohne PPS)") : "unbekannt");
  field("calibration", gpsCalibration.ready(millis()) ? "bereit" : "nicht bereit (" + String(gpsCalibration.sampleCount()) + "/5 Messungen)");
  field("gpsOffset", gpsCalibrated ? String(gpsCalibration.offsetUs / 1000.0, 2) + " ms" : "—");
  field("ntpDelay", ntpMeasured ? String(lastNtpDelayUs / 1000.0, 2) + " ms" : "—");
  field("calibrationSpread", gpsCalibration.ready(millis()) ? String(gpsCalibration.spreadUs / 1000.0, 2) + " ms" : "—");
  field("wireCorrection", synchronized() && !networkTime ? String(gpsWireCorrectionUs / 1000.0, 2) + " ms" : "—");
  field("timingRejected", String(gpsTimingRejected));
  field("gpsOverflows", String(gpsOverflows));
  field("age", clockValid ? String((millis() - lastSyncMillis) / 1000) + " s" : "—");
  field("ip", WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "nicht verbunden");
  field("ap", portal ? WiFi.softAPIP().toString() : "aus");
  field("rssi", WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + " dBm" : "—");
  field("requests", String(requests)); json += "}";
  web.sendHeader("Cache-Control", "no-store"); web.send(200, "application/json", json);
}
void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[BOOT] GPS-NTP startet");
  Serial.printf("[BOOT] Reset-Ursache: %s\n", ESP.getResetReason().c_str());
  gpsSerial.begin(9600, SWSERIAL_8N1, D5, -1, false, 128, 1024);
  loadFallback();
  loadOtaPassword();
  WiFi.persistent(false); WiFi.mode(WIFI_STA); WiFi.hostname("gps-ntp");
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoReconnect(true); WiFi.begin();
  apName = "GPS-NTP-" + String(ESP.getChipId(), HEX);
  disconnectedAt = millis();
  if (WiFi.SSID().length() == 0) {
    Serial.println("[WLAN] Keine gespeicherten Zugangsdaten: Setup sofort starten.");
    startPortal();
  } else {
    Serial.println("[WLAN] Verbinde mit gespeichertem WLAN; Setup-Fallback nach 20 Sekunden.");
  }
  web.on("/", HTTP_GET, home);
  web.on("/dashboard", HTTP_GET, home);
  web.on("/setup", HTTP_GET, [] {
    if (portal || WiFi.status() != WL_CONNECTED) { web.sendHeader("Cache-Control", "no-store"); web.send_P(200, "text/html; charset=utf-8", SETUP_PAGE); }
    else { web.sendHeader("Location", "/"); web.send(302, "text/plain", ""); }
  });
  web.on("/api/networks", HTTP_GET, networks);
  web.on("/api/wifi", HTTP_GET, [] {
    bool connected = WiFi.status() == WL_CONNECTED;
    uint32_t attemptAge = millis() - lastRetry;
    bool failed = !connected && (attemptAge >= 20000 || (attemptAge >= 3000 &&
                  (WiFi.status() == WL_CONNECT_FAILED || WiFi.status() == WL_NO_SSID_AVAIL)));
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", String("{\"connected\":") + (connected ? "true" : "false") +
             ",\"failed\":" + (failed ? "true" : "false") + ",\"setupClient\":" + (setupClient() ? "true" : "false") +
             ",\"ip\":" + jsonString(connected ? WiFi.localIP().toString() : "") + "}");
  });
  web.on("/api/status", HTTP_GET, status);
  web.collectHeaders("X-Device-Action", "X-Update-Password", "X-Firmware-Size");
  web.on("/update", HTTP_POST, finishFirmware, uploadFirmware);
  web.on("/ota-password", HTTP_POST, [] {
    web.sendHeader("Cache-Control", "no-store");
    if (!otaAllowed("change-password")) { web.send(403, "text/plain; charset=utf-8", "Aktuelles OTA-Passwort falsch oder Zugriff nicht ueber WLAN-IP."); return; }
    if (otaFiles) { web.send(409, "text/plain", "Firmware-Upload noch aktiv. Bitte kurz warten."); return; }
    String password = web.arg("password");
    if (!validOtaPassword(password) || password != web.arg("confirmation")) {
      web.send(400, "text/plain; charset=utf-8", "Passwoerter muessen uebereinstimmen: 8–64 druckbare ASCII-Zeichen ohne Leerzeichen."); return;
    }
    if (!saveOtaPassword(password)) { web.send(500, "text/plain", "Passwort konnte nicht gespeichert werden."); return; }
    otaPassword = password;
    web.send(200, "text/plain; charset=utf-8", "OTA-Passwort gespeichert. Fuer Updates und weitere Aenderungen gilt ab sofort das neue Passwort.");
  });
  web.on("/fallback", HTTP_POST, [] {
    if (web.header("X-Device-Action") != "configure") { web.send(403, "text/plain", "Aktion nicht erlaubt."); return; }
    String host = web.arg("host"); host.trim();
    if (host.length() && !Fallback::validHost(host.c_str())) {
      web.send(400, "text/plain; charset=utf-8", "Bitte einen Hostnamen oder eine IPv4-Adresse ohne Port/Pfad eingeben (max. 63 Zeichen)."); return;
    }
    if (!saveFallback(host)) { web.send(500, "text/plain", "Speichern fehlgeschlagen."); return; }
    fallbackHost = host; upstreamPending = false; upstreamAttempted = false; upstream.stop();
    gpsCalibration.reset(); ntpMeasured = false;
    fallbackState = host.length() ? "bereit" : "deaktiviert";
    if (networkTime) clockValid = false;
    else if (gpsSeen) applyGpsReference();
    web.send(200, "text/plain; charset=utf-8", host.length() ? "Notfall-NTP gespeichert." : "Notfall-NTP deaktiviert.");
  });
  web.on("/reset-wifi", HTTP_POST, [] {
    if (web.header("X-Device-Action") != "reset") { web.send(403, "text/plain", "Aktion nicht erlaubt."); return; }
    web.send(200, "text/plain; charset=utf-8", "WLAN wird geloescht. Neustart: danach mit GPS-NTP-AP verbinden (Passwort gpssetup8266). Portal: http://192.168.4.1");
    resetAt = millis(); resetPending = true;
  });
  web.on("/wifi", HTTP_POST, [] {
    if (web.header("X-Device-Action") != "wifi") { web.send(403, "text/plain", "Aktion nicht erlaubt."); return; }
    if (!portal) { web.send(403, "text/plain", "WLAN-Einrichtung nur im AP-Modus erlaubt."); return; }
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) { web.send(409, "text/plain; charset=utf-8", "WLAN-Suche laeuft noch. Bitte kurz warten."); return; }
    String ssid = web.arg("ssid"), password = web.arg("password");
    if (ssid.length() == 0 || ssid.length() > 32 || password.length() > 64 ||
        (password.length() > 0 && password.length() < 8)) {
      web.send(400, "text/plain; charset=utf-8", "SSID oder Passwort ungueltig (Passwort: 8–64 Zeichen oder leer)."); return;
    }
    web.send(200, "text/plain; charset=utf-8", "Gespeichert. Verbindung wird aufgebaut; Status zeigt die neue IP.");
    WiFi.persistent(true); WiFi.begin(ssid.c_str(), password.c_str()); WiFi.persistent(false);
    disconnectedAt = millis(); lastRetry = millis();
  });
  web.onNotFound([] {
    if (setupClient()) {
      web.sendHeader("Cache-Control", "no-store");
      web.sendHeader("Location", "http://192.168.4.1/setup", true); web.send(302, "text/plain", "");
    }
    else web.send(404, "text/plain", "Nicht gefunden");
  });
  web.begin();
  Serial.printf("[BOOT] HTTP bereit; NTP-Port: %s\n", ntp.begin(123) ? "bereit" : "Fehler");
}
void loop() {
  if (otaRestartPending) {
    if (uint32_t(millis() - otaRestartAt) >= 1000) ESP.restart();
    yield(); return;
  }
  if (otaFiles && uint32_t(millis() - otaLastActivity) > 30000) {
    otaWriter.reset(); otaFiles = 0; otaEnded = false;
    Serial.println("[OTA] Unvollstaendiger Upload verworfen (Timeout).");
  }
  if (resetPending && uint32_t(millis() - resetAt) >= 500) {
    WiFi.persistent(true); WiFi.disconnect(true); WiFi.persistent(false);
    ESP.restart(); return;
  }
  readGps();
  if (!networkTime && gpsCalibrated && !gpsCalibration.ready(millis())) applyGpsReference();
  serveNtp();
  pollFallback();
  if (WiFi.status() == WL_CONNECTED) {
    if (!wifiWasConnected) {
      connectedSince = millis(); wifiWasConnected = true;
      Serial.printf("[WLAN] Verbunden. Statusseite: http://%s/\n", WiFi.localIP().toString().c_str());
    }
    disconnectedAt = millis();
    if (!mdns) { mdns = MDNS.begin("gps-ntp"); if (mdns) { MDNS.addService("http", "tcp", 80); MDNS.addService("ntp", "udp", 123); } }
    // Keep setup accessible for 60 s after connection, then close the AP.
    if (portal && millis() - connectedSince > 60000) { dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); WiFi.scanDelete(); portal = false; }
  } else {
    wifiWasConnected = false;
    if (millis() - disconnectedAt > 20000) startPortal();
    if (millis() - lastRetry > 30000) { WiFi.reconnect(); lastRetry = millis(); }
    if (mdns) { MDNS.close(); mdns = false; }
  }
  if (portal) dns.processNextRequest();
  readGps(); serveNtp();
  web.handleClient();
  readGps(); serveNtp(); if (mdns) MDNS.update(); yield();
  if (uint32_t(millis() - lastDiagnostic) >= 10000) {
    lastDiagnostic = millis();
    Serial.printf("[STATUS] Laufzeit=%lu s WLAN=%d AP=%s AP-IP=%s GPS-Zeichen=%lu Heap=%lu\n",
                  millis() / 1000UL, int(WiFi.status()), portal ? apName.c_str() : "aus",
                  WiFi.softAPIP().toString().c_str(), static_cast<unsigned long>(gps.charsProcessed()),
                  static_cast<unsigned long>(ESP.getFreeHeap()));
  }
}
