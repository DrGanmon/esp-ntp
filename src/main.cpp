#include <Arduino.h>
#include "board_support.h"
#include <DNSServer.h>
#include <WiFiUdp.h>
#include <TinyGPSPlus.h>
#include <time.h>
#include <EEPROM.h>
#include <memory>
#include "ntp_fallback.h"
#include "nmea_timing.h"
#include "gps_calibration.h"
#include "satellite_view.h"
#include "pps_clock.h"
#include "ip_config.h"
#include "network_page.h"
#include "precision_capture.h"
#include "precision_ntp.h"
#include "frequency_discipline.h"
#ifndef GPS_PPS_ENABLED
#define GPS_PPS_ENABLED 1
#endif
PpsClock ppsClock;
FrequencyDiscipline frequencyDiscipline;
bool ppsTime = false;
bool ppsBlocked = false;
uint32_t ppsBlockedAt = 0;
#if GPS_PPS_ENABLED
volatile uint32_t ppsEdgeMicros = 0, ppsSequence = 0;
#ifdef ESP32
volatile uint32_t ppsCapturedTicks = 0;
bool IRAM_ATTR onCapturedPps(mcpwm_unit_t, mcpwm_capture_channel_id_t, const cap_event_data_t *event, void *) {
  portENTER_CRITICAL_ISR(&ppsMux);
  ppsCapturedTicks = event->cap_value; ++ppsSequence;
  portEXIT_CRITICAL_ISR(&ppsMux);
  return false;
}
#endif
void IRAM_ATTR onPps() {
#ifdef ESP32
  portENTER_CRITICAL_ISR(&ppsMux);
#endif
  ppsEdgeMicros = micros(); ++ppsSequence;
#ifdef ESP32
  portEXIT_CRITICAL_ISR(&ppsMux);
#endif
}
#endif
void capturePps() {
#if GPS_PPS_ENABLED
#ifdef ESP32
  portENTER_CRITICAL(&ppsMux);
#else
  noInterrupts();
#endif
  uint32_t edge = ppsEdgeMicros, sequence = ppsSequence;
#ifdef ESP32
  uint32_t ticks = ppsCapturedTicks;
#endif
#ifdef ESP32
  portEXIT_CRITICAL(&ppsMux);
#else
  interrupts();
#endif
#ifdef ESP32
  if (hardwareCapture && sequence) edge = capturedEdgeMicros(ticks);
#endif
  if (sequence) ppsClock.pulse(edge, sequence);
#endif
}

// GPS TX -> board-specific RX pin. All GPIO signals must be 3.3 V.
#ifdef ESP32
HardwareSerial gpsSerial(2);
#else
SoftwareSerial gpsSerial(GPS_RX_PIN, -1);
#endif
TinyGPSPlus gps;
SatelliteView satelliteView;
NmeaTiming gpsTiming;
uint32_t gpsWireCorrectionUs = 0, gpsTimingRejected = 0, gpsOverflows = 0;
GpsCalibration gpsCalibration;
uint32_t rawGpsSeconds = 0, rawGpsMicros = 0, lastNtpDelayUs = 0;
bool gpsCalibrated = false, ntpMeasured = false;
IPAddress calibrationAddress;
uint8_t calibrationLeap = 0;
DeviceWebServer web(80);
DNSServer dns;
#ifdef ESP32
PrecisionNtp ntp;
#else
WiFiUDP ntp;
#endif
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
std::unique_ptr<FirmwareUpdater> otaWriter;
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
bool useDhcp = true, networkRestartPending = false;
uint32_t networkRestartAt = 0;
IPAddress staticIp, staticGateway, staticMask, staticDns;
void networkLoop();
#ifdef ESP32
void timingTask(void *);
void webTask(void *);
#endif
uint32_t ipNumber(const IPAddress &ip) {
  return (uint32_t(ip[0]) << 24) | (uint32_t(ip[1]) << 16) | (uint32_t(ip[2]) << 8) | ip[3];
}
void loadNetworkConfig() {
  if (EEPROM.read(144) != 0xC3) return;
  uint8_t checksum = 0;
  for (unsigned i = 145; i < 162; ++i) checksum ^= EEPROM.read(i);
  if (checksum != EEPROM.read(162)) return;
  if (EEPROM.read(145) != 1) return;
  IPAddress *addresses[] = {&staticIp, &staticGateway, &staticMask, &staticDns};
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned j = 0; j < 4; ++j) (*addresses[i])[j] = EEPROM.read(146 + i * 4 + j);
  useDhcp = !IpConfig::valid(ipNumber(staticIp), ipNumber(staticGateway), ipNumber(staticMask), ipNumber(staticDns));
}
bool saveNetworkConfig(bool dhcp, const IPAddress &ip, const IPAddress &gateway,
                       const IPAddress &mask, const IPAddress &resolver) {
  uint8_t previous[19];
  for (unsigned i = 0; i < sizeof(previous); ++i) previous[i] = EEPROM.read(144 + i);
  EEPROM.write(144, 0xC3); EEPROM.write(145, dhcp ? 0 : 1);
  const IPAddress addresses[] = {ip, gateway, mask, resolver};
  for (unsigned i = 0; i < 4; ++i)
    for (unsigned j = 0; j < 4; ++j) EEPROM.write(146 + i * 4 + j, addresses[i][j]);
  uint8_t checksum = 0;
  for (unsigned i = 145; i < 162; ++i) checksum ^= EEPROM.read(i);
  EEPROM.write(162, checksum);
  if (EEPROM.commit()) return true;
  for (unsigned i = 0; i < sizeof(previous); ++i) EEPROM.write(144 + i, previous[i]);
  return false;
}

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
  ppsTime = ppsClock.locked(clockMicros());
  if (ppsTime) {
    referenceSeconds = ppsClock.referenceSeconds; referenceMicros = ppsClock.referenceMicros;
    lastSyncMillis = lastGpsMillis; clockValid = true; networkTime = false; gpsCalibrated = false;
    baseUncertaintyUs = 2000; clockStratum = 1; clockLeap = 0;
    return;
  }
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
  EEPROM.begin(192);
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
ClockMapping clockMapping(uint32_t now) {
  ClockMapping result;
  result.seconds = referenceSeconds; result.micros = referenceMicros; result.valid = synchronized();
  result.ratePpb = ppsTime && frequencyDiscipline.ready(now) ? frequencyDiscipline.ratePpb : 0;
  return result;
}
void timestamp(uint8_t *p, uint32_t now) { clockMapping(now).timestamp(p, now); }
void readGps() {
  // Decode the ISR buffer before checking whether it overflowed.
  gpsSerial.available();
#ifdef ESP32
  bool overflow = gpsRxOverflow.exchange(false);
#else
  bool overflow = gpsSerial.overflow();
#endif
  if (overflow) { ++gpsOverflows; satelliteView.invalidateCycle(); }
  gpsTiming.service(clockMicros(), overflow);
  // Bound work per loop so DNS, HTTP and NTP remain responsive.
  for (unsigned i = 0; i < 256 && gpsSerial.available(); ++i) {
    char c = gpsSerial.read();
    uint32_t receivedAt = clockMicros();
    gpsTiming.observe(c, receivedAt);
    satelliteView.encode(c, millis());
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
    capturePps();
    if (ppsBlocked && uint32_t(millis() - ppsBlockedAt) >= 60000) ppsBlocked = false;
    bool ppsAssociated = false;
    if (!ppsBlocked) ppsAssociated = ppsClock.associate(epoch, receivedAt - gpsWireCorrectionUs, gps.time.centisecond());
    else ppsClock.reset();
    if (ppsAssociated) frequencyDiscipline.observe(epoch, ppsClock.referenceMicros);
    else frequencyDiscipline.reset();
    gpsSeen = true; lastGpsMillis = millis();
    applyGpsReference();
  }
}
void pollFallback() {
  if (!fallbackHost.length() || !networkConnected()) {
    upstreamPending = false;
    upstreamAttempted = false;
    return;
  }
  if (upstreamPending) {
    StateGuard guard;
    int size = upstream.parsePacket();
    if (size) {
      uint32_t receivedMicros = clockMicros();
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
          if (ppsTime) {
            int64_t ppsNow = int64_t(referenceSeconds) * 1000000 + uint32_t(receivedMicros - referenceMicros);
            int64_t difference = int64_t(sample.seconds) * 1000000 + sample.fractionUs - ppsNow;
            if (sample.uncertaintyUs < 100000 && (difference < -500000 || difference > 500000)) {
              ppsBlocked = true; ppsBlockedAt = millis();
              ppsClock.reset(); frequencyDiscipline.reset(); gpsCalibration.reset(); applyGpsReference();
              fallbackState = "PPS-Sekundenzuordnung widerspricht NTP";
            } else fallbackState = "PPS aktiv; NTP-Plausibilitaetsvergleich";
          } else if (uint32_t(millis() - lastGpsMillis) < 1500) {
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
          lastSyncMillis = millis(); clockValid = true; networkTime = true; gpsCalibrated = false; ppsTime = false;
          baseUncertaintyUs = sample.uncertaintyUs;
          clockStratum = sample.stratum; clockLeap = sample.leap;
          referenceAddress = upstreamAddress;
          fallbackState = "Zeit empfangen";
        }
        upstreamAttempted = true;
        upstreamPending = false;
      } else fallbackState = "ungueltige Antwort";
    }
    if (upstreamPending && uint32_t(clockMicros() - upstreamSentMicros) > 1500000) {
      upstreamPending = false; fallbackState = "keine gueltige Antwort (Timeout)";
    }
    return;
  }
  if (upstreamAttempted && uint32_t(millis() - upstreamLastAttempt) < 30000) return;
  upstreamAttempted = true; upstreamLastAttempt = millis();
  fallbackState = "Server wird aufgeloest";
  // ESP8266: bounded DNS call; ESP32: asynchronous worker on the web/network core.
  int resolved = resolveHost(fallbackHost.c_str(), upstreamAddress);
  if (!resolved) { upstreamAttempted = false; return; }
  if (resolved < 0) {
    fallbackState = "DNS-Aufloesung fehlgeschlagen"; return;
  }
  if (upstreamAddress == networkIP() || upstreamAddress == WiFi.softAPIP() ||
      upstreamAddress[0] == 127 || upstreamAddress[0] == 0 || upstreamAddress[0] >= 224 ||
      upstreamAddress[3] == 255) {
    fallbackState = "ungueltige oder eigene Serveradresse"; return;
  }
  // Rebind to discard stale packets. Requests use an unpredictable originate cookie.
  upstream.stop();
  if (!upstream.begin(uint16_t(49152 + (randomWord() % 16000)))) {
    fallbackState = "UDP-Port nicht verfuegbar"; return;
  }
  uint8_t packet[48] = {}; packet[0] = 0x23;
  put32(upstreamNonce, randomWord()); put32(upstreamNonce + 4, randomWord());
  memcpy(packet + 40, upstreamNonce, 8);
  upstreamSentMicros = clockMicros();
  if (!upstream.beginPacket(upstreamAddress, 123) || upstream.write(packet, sizeof(packet)) != sizeof(packet) || !upstream.endPacket()) {
    fallbackState = "Senden fehlgeschlagen"; return;
  }
  upstreamPending = true; fallbackState = "warte auf Antwort";
}
void serveNtp() {
#ifndef ESP32
  uint32_t pollStarted = clockMicros();
#endif
  int size = ntp.parsePacket();
  if (!size) return;
#ifdef ESP32
  uint32_t receivedAt = ntp.receivedAt();
#else
  uint32_t receivedAt = pollStarted + uint32_t(clockMicros() - pollStarted) / 2;
#endif
  uint8_t receiveTime[8]; timestamp(receiveTime, receivedAt);
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
  reply[3] = ppsTime ? uint8_t(-16) : networkTime || gpsCalibrated ? uint8_t(-10) : 0;
  put32(reply + 8, valid ? (uint64_t(uncertaintyUs()) * 65536 / 1000000) : 0xFFFF0000UL);
  if (networkTime || gpsCalibrated) for (unsigned i = 0; i < 4; ++i) reply[12 + i] = referenceAddress[i];
  else memcpy(reply + 12, "GPS", 3);
  if (clockValid) put32(reply + 16, referenceSeconds + NTP_EPOCH);
  memcpy(reply + 24, packet + 40, 8);
  memcpy(reply + 32, receiveTime, 8);
  if (!ntp.beginPacket(ntp.remoteIP(), ntp.remotePort())) return;
#ifdef ESP32
  ntp.setMapping(clockMapping(clockMicros())); // Transmit timestamp is set in the lwIP send callback.
#else
  timestamp(reply + 40, clockMicros());
#endif
  if (ntp.write(reply, sizeof(reply)) == sizeof(reply) && ntp.endPacket()) ++requests;
}

const char PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="de"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>GPS NTP</title>
<style>body{font:17px system-ui;background:#101b2d;color:#edf4ff;max-width:760px;margin:40px auto;padding:20px}section{background:#1c2c43;padding:24px;border-radius:16px;margin:18px 0}h1{margin-bottom:6px}dl{display:grid;grid-template-columns:1fr 1fr;gap:14px}dd{margin:0;text-align:right}input,button{font:inherit;padding:12px;box-sizing:border-box;border-radius:8px;border:0;margin:6px 0;width:100%}button{background:#72dcbd;color:#10251f;cursor:pointer}small{color:#bac8db}#message{color:#72dcbd}</style>
<h1>GPS → NTP</h1><small>GPS-Zeitserver · u-blox NEO-6M · UTC</small><p><a style="color:#72dcbd" href="/network">IP-Konfiguration (DHCP / statisch)</a></p>
<section><h2 id="state">Status wird geladen …</h2><dl id="values"></dl><small>Die Unsicherheit ist eine konservative Planungsannahme, keine gemessene Genauigkeit. Ohne PPS können NMEA-Verzögerungen die Zeit verschieben.</small></section>
<section><h2>Position und Satellitenhimmel</h2><dl id="position"></dl><small>Koordinaten: WGS84. Nachkommastellen sind keine Genauigkeitsgarantie. Höhe über dem mittleren Meeresspiegel.</small><svg id="sky" viewBox="0 0 400 400" role="img" aria-label="Satellitenpositionen am Himmel" style="display:block;width:100%;max-width:440px;margin:auto"></svg><p id="skyState" role="status"></p><small>Norden oben, Osten rechts. Außenkreis: Horizont (0°), Mitte: Zenit (90°). Grün: Signalwert vorhanden; grau: kein Signalwert gemeldet. Die Ansicht zeigt Richtungen vom Empfänger aus, keine Satellitenentfernungen.</small><div style="overflow-x:auto"><table style="width:100%;text-align:left"><thead><tr><th>PRN</th><th>Azimut</th><th>Elevation</th><th>C/N₀</th></tr></thead><tbody id="satelliteRows"></tbody></table></div></section>
<section><h2>Notfall-NTP und GPS-Kalibrierung</h2><form id="fallback"><label>NTP-Server (Hostname oder IPv4)<input id="fallbackHost" name="host" list="ntpServers" maxlength="63" placeholder="Leer = deaktiviert" autocomplete="off"></label><datalist id="ntpServers"><option value="pool.ntp.org"><option value="de.pool.ntp.org"></datalist><button>Notfall-NTP speichern</button></form><p id="fallbackMessage" role="status"></p><small>Der Server wird alle 30 Sekunden abgefragt: bei GPS-Empfang zur Kalibrierung, bei GPS-Ausfall als Ersatz. Ab drei stabilen Vergleichsmessungen wird die GPS-Zeit korrigiert. Ein naher, genauer NTP-Server verbessert die Voraussetzungen. Leer speichern deaktiviert auch die Kalibrierung. Millisekundengenauigkeit ist nicht garantiert.</small></section>
<section><h2>Firmware aktualisieren</h2><form id="ota"><label>Firmware (.bin für dieses Board)<input id="firmware" type="file" accept=".bin" required></label><label>OTA-Passwort<input id="otaPassword" type="password" required autocomplete="off"></label><button>Firmware hochladen und neu starten</button></form><progress id="otaProgress" value="0" max="100" style="width:100%" hidden></progress><p id="otaMessage" role="status"></p><small>Das Standardpasswort steht in der README und kann unten geändert werden. WLAN und Notfall-NTP bleiben erhalten. Während des Updates die Stromversorgung aufrechterhalten.</small></section>
<section><h2>OTA-Passwort ändern</h2><form id="changeOtaPassword"><label>Aktuelles Passwort<input id="currentOtaPassword" type="password" required autocomplete="current-password"></label><label>Neues Passwort<input name="password" id="newOtaPassword" type="password" minlength="8" maxlength="64" pattern="[!-~]{8,64}" required autocomplete="new-password"></label><label>Neues Passwort wiederholen<input name="confirmation" type="password" minlength="8" maxlength="64" required autocomplete="new-password"></label><button>OTA-Passwort speichern</button></form><p id="passwordMessage" role="status"></p><small>8–64 Zeichen: Buchstaben ohne Umlaute, Ziffern und ASCII-Sonderzeichen, keine Leerzeichen. Die Änderung gilt sofort und bleibt nach Neustart und WLAN-Reset erhalten.</small></section>
<section><h2>WLAN zurücksetzen</h2><p>Löscht die gespeicherten WLAN-Zugangsdaten und startet das Gerät neu. Danach ist die Einrichtung wieder über den Access Point möglich. Der Notfall-NTP bleibt gespeichert.</p><button id="reset" style="background:#ffb1a8">WLAN löschen und neu starten</button><p id="resetMessage" role="status"></p></section>
<script>
const fields=[['utc','Zeit (UTC)'],['source','Aktive Zeitquelle'],['stratum','NTP-Stratum'],['satellites','Satelliten im Fix'],['visible','Satelliten sichtbar'],['hdop','HDOP (Position)'],['uncertainty','Geschätzte Unsicherheit'],['calibration','GPS-Kalibrierung'],['gpsOffset','GPS-Korrektur durch NTP'],['ntpDelay','NTP-Netzlaufzeit'],['calibrationSpread','Streuung der Vergleiche'],['wireCorrection','UART-Zeitkorrektur'],['timingRejected','Verzögerte GPS-Sätze verworfen'],['gpsOverflows','GPS-Pufferüberläufe'],['age','Letzte Zeitübernahme'],['fallbackHost','Notfall-NTP'],['fallbackState','Ersatzserver-Status'],['ip','LAN-/WLAN-IP / NTP-Adresse'],['ap','Einrichtungs-AP'],['rssi','WLAN-Signal'],['requests','NTP-Antworten']];
fields.splice(3,0,['pps','PPS (optional)'],['capture','PPS-Zeiterfassung'],['frequency','Frequenzkorrektur'],['frequencyJitter','PPS-Intervallstreuung'],['ntpTimestamps','NTP-Zeitstempel'],['ntpDrops','NTP verworfen / Sendefehler'],['board','Board'],['interface','Netzwerkschnittstelle'],['ipMode','IP-Konfiguration']);
let configLoaded=false,resetting=false,updating=false;
const svgNS='http://www.w3.org/2000/svg';
function skyNode(tag,attrs,text){let el=document.createElementNS(svgNS,tag);for(let[k,v]of Object.entries(attrs))el.setAttribute(k,v);if(text!==undefined)el.textContent=text;document.querySelector('#sky').append(el);return el}
function drawSky(s){document.querySelector('#sky').replaceChildren();for(let radius of [170,113.33,56.67])skyNode('circle',{cx:200,cy:200,r:radius,fill:'none',stroke:'#52657e'});skyNode('path',{d:'M30 200H370 M200 30V370',stroke:'#52657e'});for(let[label,x,y]of [['N',200,18],['O',388,206],['S',200,395],['W',12,206]])skyNode('text',{x,y,fill:'#edf4ff','text-anchor':'middle'},label);for(let sat of s.satellites){if(sat.azimuth===null||sat.elevation===null)continue;let angle=sat.azimuth*Math.PI/180,radius=170*(90-sat.elevation)/90,x=200+radius*Math.sin(angle),y=200-radius*Math.cos(angle);let point=skyNode('circle',{cx:x,cy:y,r:8,fill:sat.snr>0?'#72dcbd':'#8d99aa'});let title=document.createElementNS(svgNS,'title');title.textContent='PRN '+sat.id+' · '+sat.azimuth+'° · '+sat.elevation+'°';point.append(title);skyNode('text',{x:x+10,y:y-8,fill:'#edf4ff','font-size':12},String(sat.id))}let rows=document.querySelector('#satelliteRows');rows.replaceChildren();for(let sat of s.satellites){let tr=document.createElement('tr');for(let value of [sat.id,sat.azimuth===null?'—':sat.azimuth+'°',sat.elevation===null?'—':sat.elevation+'°',sat.snr===null?'—':sat.snr+' dB-Hz']){let td=document.createElement('td');td.textContent=value;tr.append(td)}rows.append(tr)}document.querySelector('#skyState').textContent=s.fresh?(s.satellites.length+' Satelliten gemeldet'):'Warte auf aktuelle Satellitendaten …'}
async function refreshSky(){if(resetting||updating)return;try{let s=await(await fetch('/api/sky',{cache:'no-store'})).json();drawSky(s);let dl=document.querySelector('#position');dl.replaceChildren();for(let[label,value]of [['Breitengrad',s.positionValid?s.latitude.toFixed(6)+'°':'kein aktueller Fix'],['Längengrad',s.positionValid?s.longitude.toFixed(6)+'°':'—'],['Höhe',s.altitude===null?'—':s.altitude.toFixed(1)+' m'],['Alter der Position',s.positionAge===null?'—':(s.positionAge/1000).toFixed(1)+' s']]){let dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=label;dd.textContent=value;dl.append(dt,dd)}}catch(e){document.querySelector('#position').textContent='Position nicht erreichbar';drawSky({fresh:false,satellites:[]});document.querySelector('#skyState').textContent='Satellitendaten nicht erreichbar'}}
async function refresh(){if(resetting||updating)return;try{let s=await(await fetch('/api/status',{cache:'no-store'})).json();document.querySelector('#state').textContent=s.synced?'Zeit verfügbar: '+s.source:'Nicht synchronisiert – warte auf Zeitquelle';if(!configLoaded){document.querySelector('#fallbackHost').value=s.fallbackHost==='deaktiviert'?'':s.fallbackHost;configLoaded=true}let dl=document.querySelector('#values');dl.replaceChildren();for(let[k,label]of fields){let dt=document.createElement('dt'),dd=document.createElement('dd');dt.textContent=label;dd.textContent=s[k];dl.append(dt,dd)}}catch(e){document.querySelector('#state').textContent='Verbindung unterbrochen'}}
document.querySelector('#ota').onsubmit=e=>{e.preventDefault();let file=document.querySelector('#firmware').files[0];if(!file||!file.size||!file.name.toLowerCase().endsWith('.bin')){document.querySelector('#otaMessage').textContent='Bitte eine nicht leere .bin-Datei auswählen.';return}if(!confirm('Firmware jetzt installieren und Gerät neu starten?'))return;updating=true;document.querySelectorAll('button').forEach(b=>b.disabled=true);let progress=document.querySelector('#otaProgress'),message=document.querySelector('#otaMessage');progress.hidden=false;progress.value=0;message.textContent='Firmware wird hochgeladen …';let xhr=new XMLHttpRequest();xhr.open('POST','/update');xhr.timeout=180000;xhr.setRequestHeader('X-Device-Action','update');xhr.setRequestHeader('X-Update-Password',document.querySelector('#otaPassword').value);xhr.setRequestHeader('X-Firmware-Size',String(file.size));xhr.upload.onprogress=e=>{if(e.lengthComputable){progress.value=Math.round(e.loaded/e.total*100);message.textContent=progress.value===100?'Upload vollständig. Firmware wird geprüft …':'Upload: '+progress.value+' %'}};const unlock=()=>{updating=false;document.querySelectorAll('button').forEach(b=>b.disabled=false)};xhr.onload=()=>{document.querySelector('#otaPassword').value='';message.textContent=xhr.responseText;if(xhr.status===200){resetting=true;message.textContent+=' Seite nach dem Neustart neu laden.'}else unlock()};xhr.onerror=xhr.ontimeout=xhr.onabort=()=>{message.textContent='Keine Bestätigung erhalten. Verbindung prüfen und Seite neu laden, bevor du erneut aktualisierst.';unlock()};let data=new FormData();data.append('firmware',file);xhr.send(data)};
document.querySelector('#changeOtaPassword').onsubmit=async e=>{e.preventDefault();let form=e.target,data=new FormData(form),message=document.querySelector('#passwordMessage');if(data.get('password')!==data.get('confirmation')){message.textContent='Die neuen Passwörter stimmen nicht überein.';return}let button=form.querySelector('button');button.disabled=true;try{let r=await fetch('/ota-password',{method:'POST',headers:{'X-Device-Action':'change-password','X-Update-Password':document.querySelector('#currentOtaPassword').value},body:new URLSearchParams(data)});message.textContent=await r.text();if(r.ok){form.reset();document.querySelector('#otaPassword').value=''}}catch(e){message.textContent='Keine Bestätigung erhalten. Bei erneutem Versuch gegebenenfalls das neue Passwort verwenden.'}finally{button.disabled=false}};
async function postForm(form,path,message,action){let button=form.querySelector('button');button.disabled=true;try{let r=await fetch(path,{method:'POST',headers:action?{'X-Device-Action':action}:{},body:new URLSearchParams(new FormData(form))});document.querySelector(message).textContent=await r.text();if(r.ok)refresh()}catch(e){document.querySelector(message).textContent='Verbindung unterbrochen'}finally{button.disabled=false}}
document.querySelector('#fallback').onsubmit=e=>{e.preventDefault();postForm(e.target,'/fallback','#fallbackMessage','configure')};
document.querySelector('#reset').onclick=async()=>{if(!confirm('WLAN-Zugangsdaten wirklich löschen und neu starten?'))return;let button=document.querySelector('#reset');button.disabled=true;try{let r=await fetch('/reset-wifi',{method:'POST',headers:{'X-Device-Action':'reset'}});document.querySelector('#resetMessage').textContent=await r.text();if(r.ok){resetting=true;document.querySelector('#state').textContent='Neustart – bitte mit dem Einrichtungs-AP verbinden.'}else button.disabled=false}catch(e){document.querySelector('#resetMessage').textContent='Keine Bestätigung erhalten. Verbindung prüfen.';button.disabled=false}};
refresh();refreshSky();setInterval(()=>{refresh();refreshSky()},2000);
</script></html>)HTML";

const char SETUP_PAGE[] PROGMEM = R"HTML(<!doctype html><html lang="de"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>WLAN einrichten · GPS NTP</title>
<style>body{font:17px system-ui;background:#101b2d;color:#edf4ff;max-width:560px;margin:40px auto;padding:20px}section{background:#1c2c43;padding:24px;border-radius:16px}input,button,select{font:inherit;padding:12px;box-sizing:border-box;border-radius:8px;border:0;margin:8px 0 18px;width:100%}button{background:#72dcbd;color:#10251f;cursor:pointer}button:disabled{opacity:.5;cursor:wait}small{color:#bac8db}a{color:#72dcbd}p{overflow-wrap:anywhere}</style>
<h1>WLAN einrichten</h1><p><a href="/network">Ethernet / IP-Konfiguration</a></p><p>Verbinde deinen GPS-NTP-Server mit deinem WLAN.</p><section>
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
  return networkConnected() && !setupClient() && !resetPending &&
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
    uint32_t maximum = firmwareCapacity();
    if (upload.name != "firmware" || !filename.endsWith(".bin") || size < 16 || size > maximum) {
      otaError = "Ungueltige Firmware-Datei oder Dateigroesse (passende Board-Firmware erforderlich)."; return;
    }
    otaWriter.reset(new FirmwareUpdater());
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
  if (!otaAllowed()) { web.send(403, "text/plain; charset=utf-8", "Update nur ueber LAN-/WLAN-IP mit gueltigem OTA-Passwort erlaubt."); return; }
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
             !setupClient() && networkConnected() ? PAGE : SETUP_PAGE);
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
              ",\"open\":" + (WiFi.encryptionType(i) == OPEN_ENCRYPTION ? "true" : "false") + "}";
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
  StateGuard guard;
  String utc = "—";
  if (synchronized()) {
    uint32_t mono = clockMicros();
    time_t now = clockMapping(mono).unixMicrosAt(mono) / 1000000;
    struct tm calendar; gmtime_r(&now, &calendar);
    char buffer[32]; strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &calendar); utc = buffer;
  }
  String json = "{\"synced\":" + String(synchronized() ? "true" : "false");
  auto field = [&json](const char *key, const String &value) { json += ",\"" + String(key) + "\":\"" + value + "\""; };
  field("utc", utc);
  field("source", synchronized() ? (ppsTime ? "GPS + PPS" : networkTime ? "Notfall-NTP" : gpsCalibrated ? "GPS + NTP-Kalibrierung" : "GPS (NMEA)") : "keine");
  field("pps", ppsBlocked ? "UTC-Zuordnung widerspricht NTP; 60 s gesperrt" : ppsTime ? "synchronisiert" : ppsClock.detected(clockMicros()) ? "Impulse erkannt; warte auf stabile UTC-Zuordnung" : "kein PPS erkannt (optional; Pins siehe README)");
  field("capture", hardwareCapture ? "MCPWM-Hardware (WT32)" : "GPIO-Interrupt / micros");
  field("frequency", ppsTime && frequencyDiscipline.ready(clockMicros()) ? String(frequencyDiscipline.ratePpb / 1000.0, 3) + " ppm (lokaler Gangfehler)" : "lernt (" + String(frequencyDiscipline.progress()) + "/16 Intervalle)");
  field("frequencyJitter", frequencyDiscipline.ready(clockMicros()) ? String(frequencyDiscipline.jitterUs) + " us" : "—");
#ifdef ESP32
  field("ntpTimestamps", "Empfang/Sendeuebergabe im lwIP-Stack (Software)");
  field("ntpDrops", String(ntp.dropped()));
#else
  field("ntpTimestamps", "UDP-Abholung / Software");
  field("ntpDrops", "—");
#endif
  field("stratum", synchronized() ? String(clockStratum) : "16 (unsynchronisiert)");
  field("fallbackHost", fallbackHost.length() ? fallbackHost : "deaktiviert");
  field("fallbackState", !fallbackHost.length() ? "deaktiviert" :
        !networkConnected() ? "warte auf Netzwerk" : fallbackState);
  field("satellites", gps.satellites.isValid() && gps.satellites.age() < 5000 ? String(gps.satellites.value()) : "—");
  field("visible", satelliteView.fresh(millis()) ? String(satelliteView.count()) : "—");
  field("hdop", gps.hdop.isValid() && gps.hdop.age() < 5000 ? String(gps.hdop.hdop(), 1) : "—");
  field("uncertainty", synchronized() ? String(uncertaintyUs() / 1000.0, 2) + (ppsTime ? " ms (PPS-Annahme, ungemessen)" : networkTime || gpsCalibrated ? " ms (NTP-Schaetzung)" : " ms (ohne PPS)") : "unbekannt");
  field("calibration", gpsCalibration.ready(millis()) ? "bereit" : "nicht bereit (" + String(gpsCalibration.sampleCount()) + "/5 Messungen)");
  field("gpsOffset", gpsCalibrated ? String(gpsCalibration.offsetUs / 1000.0, 2) + " ms" : "—");
  field("ntpDelay", ntpMeasured ? String(lastNtpDelayUs / 1000.0, 2) + " ms" : "—");
  field("calibrationSpread", gpsCalibration.ready(millis()) ? String(gpsCalibration.spreadUs / 1000.0, 2) + " ms" : "—");
  field("wireCorrection", synchronized() && !networkTime ? String(gpsWireCorrectionUs / 1000.0, 2) + " ms" : "—");
  field("timingRejected", String(gpsTimingRejected));
  field("gpsOverflows", String(gpsOverflows));
  field("age", clockValid ? String((millis() - lastSyncMillis) / 1000) + " s" : "—");
  field("ip", networkConnected() ? networkIP().toString() : "nicht verbunden");
  field("board", BOARD_NAME);
  field("interface", ethernetConnected() ? "Ethernet" : "WLAN");
  field("ipMode", useDhcp ? "DHCP" : "statisch (primaere Schnittstelle)");
  field("ap", portal ? WiFi.softAPIP().toString() : "aus");
  field("rssi", WiFi.status() == WL_CONNECTED ? String(WiFi.RSSI()) + " dBm" : "—");
  field("requests", String(requests)); json += "}";
  guard.unlock();
  web.sendHeader("Cache-Control", "no-store"); web.send(200, "application/json", json);
}
void setup() {
#ifdef ESP32
  clockMutex = xSemaphoreCreateRecursiveMutex();
  if (!clockMutex) { delay(1000); ESP.restart(); return; }
#endif
  Serial.begin(115200);
  delay(200);
  Serial.println("\n[BOOT] GPS-NTP startet");
  Serial.printf("[BOOT] Reset-Ursache: %s\n", resetReason().c_str());
#ifdef ESP32
  gpsSerial.setRxBufferSize(2048);
  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX_PIN, -1);
  gpsSerial.setRxFIFOFull(1);
  gpsSerial.setRxTimeout(1);
  gpsSerial.onReceiveError([](hardwareSerial_error_t error) {
    if (error == UART_BUFFER_FULL_ERROR || error == UART_FIFO_OVF_ERROR) gpsRxOverflow.store(true);
  });
#else
  gpsSerial.begin(9600, SWSERIAL_8N1, GPS_RX_PIN, -1, false, 128, 1024);
#endif
#if GPS_PPS_ENABLED
  pinMode(GPS_PPS_PIN, INPUT_PULLUP);
#ifdef ESP32
  if (!beginPrecisionCapture(GPS_PPS_PIN, onCapturedPps)) {
    Serial.println("[GPS] Hardware-Capture fehlgeschlagen; GPIO-Interrupt als Rueckfall.");
    attachInterrupt(digitalPinToInterrupt(GPS_PPS_PIN), onPps, RISING);
  } else Serial.println("[GPS] MCPWM-Hardware-Capture aktiv (80 MHz).");
#else
  attachInterrupt(digitalPinToInterrupt(GPS_PPS_PIN), onPps, RISING);
#endif
  Serial.printf("[GPS] PPS GPIO%d; GPS-TX an GPIO%d.\n", GPS_PPS_PIN, GPS_RX_PIN);
#endif
  loadFallback();
  loadOtaPassword();
  loadNetworkConfig();
  WiFi.persistent(false); WiFi.mode(WIFI_STA);
#ifdef ESP32
  WiFi.setHostname("gps-ntp"); WiFi.setSleep(false);
#else
  WiFi.hostname("gps-ntp"); WiFi.setSleepMode(WIFI_NONE_SLEEP);
#endif
#ifndef ESP32
  if (!useDhcp) WiFi.config(staticIp, staticGateway, staticMask, staticDns);
  else WiFi.config(IPAddress(0U), IPAddress(0U), IPAddress(0U));
#else
  if (!ETH.begin(1, 16, 23, 18, ETH_PHY_LAN8720, ETH_CLOCK_GPIO0_IN)) Serial.println("[ETH] Start fehlgeschlagen.");
  else {
    ETH.setHostname("gps-ntp");
    if (!useDhcp && !ETH.config(staticIp, staticGateway, staticMask, staticDns)) Serial.println("[ETH] Statische Konfiguration fehlgeschlagen.");
  }
#endif
  WiFi.setAutoReconnect(true); WiFi.begin();
  apName = "GPS-NTP-" + String(deviceId(), HEX);
  disconnectedAt = millis();
  if (WiFi.SSID().length() == 0) {
    Serial.println("[WLAN] Keine gespeicherten Zugangsdaten: Setup sofort starten.");
    startPortal();
  } else {
    Serial.println("[WLAN] Verbinde mit gespeichertem WLAN; Setup-Fallback nach 20 Sekunden.");
  }
#ifdef ESP32
  startPortal(); // Recovery window even if a wrong static address looks connected.
#else
  if (!useDhcp) startPortal();
#endif
  web.on("/", HTTP_GET, home);
  web.on("/dashboard", HTTP_GET, home);
  web.on("/setup", HTTP_GET, [] {
    if (portal || !networkConnected()) { web.sendHeader("Cache-Control", "no-store"); web.send_P(200, "text/html; charset=utf-8", SETUP_PAGE); }
    else { web.sendHeader("Location", "/"); web.send(302, "text/plain", ""); }
  });
  web.on("/api/networks", HTTP_GET, networks);
  web.on("/api/wifi", HTTP_GET, [] {
    bool connected = networkConnected();
    uint32_t attemptAge = millis() - lastRetry;
    bool failed = !connected && (attemptAge >= 20000 || (attemptAge >= 3000 &&
                  (WiFi.status() == WL_CONNECT_FAILED || WiFi.status() == WL_NO_SSID_AVAIL)));
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json", String("{\"connected\":") + (connected ? "true" : "false") +
             ",\"failed\":" + (failed ? "true" : "false") + ",\"setupClient\":" + (setupClient() ? "true" : "false") +
             ",\"ip\":" + jsonString(connected ? networkIP().toString() : "") + "}");
  });
  web.on("/api/status", HTTP_GET, status);
  web.on("/network", HTTP_GET, [] {
    web.sendHeader("Cache-Control", "no-store"); web.send_P(200, "text/html; charset=utf-8", NETWORK_PAGE);
  });
  web.on("/api/network", HTTP_GET, [] {
    String json = "{\"board\":" + jsonString(BOARD_NAME) + ",\"target\":" + jsonString(
#ifdef ESP32
      "Ethernet (WLAN-Reserve verwendet DHCP)"
#else
      "WLAN"
#endif
    ) + ",\"current\":" + jsonString(networkConnected() ? networkIP().toString() : "nicht verbunden") +
      ",\"mode\":" + jsonString(useDhcp ? "dhcp" : "static") + ",\"ip\":" + jsonString(staticIp.toString()) +
      ",\"mask\":" + jsonString(staticMask.toString()) + ",\"gateway\":" + jsonString(staticGateway.toString()) +
      ",\"dns\":" + jsonString(staticDns.toString()) + "}";
    web.sendHeader("Cache-Control", "no-store"); web.send(200, "application/json", json);
  });
  web.on("/network", HTTP_POST, [] {
    if (networkRestartPending || resetPending || otaRestartPending || otaFiles) { web.send(409, "text/plain", "Andere Aktion laeuft bereits."); return; }
    if (!otaPassword.length() || web.header("X-Device-Action") != "network" || web.header("X-Update-Password") != otaPassword) {
      web.send(403, "text/plain", "Gueltiges OTA-Passwort erforderlich."); return;
    }
    String mode = web.arg("mode");
    if (mode != "dhcp" && mode != "static") { web.send(400, "text/plain", "Ungueltiger IP-Modus."); return; }
    bool dhcp = mode == "dhcp";
    IPAddress ip, gateway, mask, resolver;
    if (!dhcp && (!ip.fromString(web.arg("ip")) || !gateway.fromString(web.arg("gateway")) ||
        !mask.fromString(web.arg("mask")) || !resolver.fromString(web.arg("dns")) ||
        !IpConfig::valid(ipNumber(ip), ipNumber(gateway), ipNumber(mask), ipNumber(resolver)))) {
      web.send(400, "text/plain; charset=utf-8", "Ungueltige IPv4-Konfiguration. Gateway muss im selben Subnetz liegen; Netz-/Broadcastadressen und das Setup-Netz 192.168.4.0/24 sind nicht erlaubt."); return;
    }
    if (!saveNetworkConfig(dhcp, ip, gateway, mask, resolver)) { web.send(500, "text/plain", "Speichern fehlgeschlagen."); return; }
    web.send(200, "text/plain; charset=utf-8", dhcp ? "DHCP gespeichert. Neustart; neue IP im Router oder seriellen Monitor nachsehen." : "Statische IP gespeichert. Neustart; danach http://" + ip.toString() + "/ oeffnen.");
    networkRestartAt = millis(); networkRestartPending = true;
  });
  web.on("/api/sky", HTTP_GET, [] {
    StateGuard guard;
    bool positionValid = gps.location.isValid() && gps.location.age() < 5000;
    bool fresh = satelliteView.fresh(millis());
    String json = String("{\"positionValid\":") + (positionValid ? "true" : "false") +
      ",\"latitude\":" + (positionValid ? String(gps.location.lat(), 6) : String("null")) +
      ",\"longitude\":" + (positionValid ? String(gps.location.lng(), 6) : String("null")) +
      ",\"altitude\":" + (positionValid && gps.altitude.isValid() && gps.altitude.age() < 5000 ? String(gps.altitude.meters(), 1) : String("null")) +
      ",\"positionAge\":" + (gps.location.isValid() ? String(gps.location.age()) : String("null")) +
      ",\"fresh\":" + (fresh ? "true" : "false") + ",\"satellites\":[";
    if (fresh) for (unsigned i = 0; i < satelliteView.count(); ++i) {
      const auto &sat = satelliteView.at(i);
      if (i) json += ',';
      json += "{\"id\":" + String(sat.id) + ",\"elevation\":" + (sat.elevation < 0 ? String("null") : String(sat.elevation)) +
              ",\"azimuth\":" + (sat.azimuth < 0 ? String("null") : String(sat.azimuth)) +
              ",\"snr\":" + (sat.snr < 0 ? String("null") : String(sat.snr)) + "}";
    }
    guard.unlock();
    web.sendHeader("Cache-Control", "no-store"); web.send(200, "application/json", json + "]}");
  });
#ifdef ESP32
  const char *headers[] = {"X-Device-Action", "X-Update-Password", "X-Firmware-Size"};
  web.collectHeaders(headers, 3);
#else
  web.collectHeaders("X-Device-Action", "X-Update-Password", "X-Firmware-Size");
#endif
  web.on("/update", HTTP_POST, finishFirmware, uploadFirmware);
  web.on("/ota-password", HTTP_POST, [] {
    web.sendHeader("Cache-Control", "no-store");
    if (!otaAllowed("change-password")) { web.send(403, "text/plain; charset=utf-8", "Aktuelles OTA-Passwort falsch oder Zugriff nicht ueber LAN-/WLAN-IP."); return; }
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
    StateGuard guard;
    gpsCalibration.reset(); ntpMeasured = false;
    fallbackState = host.length() ? "bereit" : "deaktiviert";
    if (networkTime) clockValid = false;
    else if (gpsSeen) applyGpsReference();
    guard.unlock();
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
#ifdef ESP32
  if (xTaskCreatePinnedToCore(timingTask, "gps-ntp", 8192, nullptr, 3, nullptr, 1) != pdPASS ||
      xTaskCreatePinnedToCore(webTask, "web-config", 12288, nullptr, 1, nullptr, 0) != pdPASS) {
    Serial.println("[BOOT] Task-Start fehlgeschlagen."); delay(1000); ESP.restart();
  }
#endif
}
void timingStep() {
  StateGuard guard;
  capturePps();
  if (ppsTime && !ppsClock.locked(clockMicros())) { frequencyDiscipline.reset(); applyGpsReference(); }
  readGps();
  if (!networkTime && gpsCalibrated && !gpsCalibration.ready(millis())) applyGpsReference();
  serveNtp();
}
void networkLoop() {
#ifdef ESP32
  static bool usedEthernet = false;
  static bool usedWifi = false;
  bool preferEthernet = ethernetConnected();
  bool connectedWifi = WiFi.status() == WL_CONNECTED;
  if (preferEthernet != usedEthernet || connectedWifi != usedWifi) {
    selectDefaultNetwork(preferEthernet);
    usedEthernet = preferEthernet;
    usedWifi = connectedWifi;
    upstream.stop(); upstreamPending = false; upstreamAttempted = false;
  }
#endif
  if (networkRestartPending) {
    if (uint32_t(millis() - networkRestartAt) >= 1000) ESP.restart();
    delay(1); return;
  }
  if (otaRestartPending) {
    if (uint32_t(millis() - otaRestartAt) >= 1000) ESP.restart();
    yield(); return;
  }
  if (otaFiles && uint32_t(millis() - otaLastActivity) > 30000) {
    otaWriter.reset(); otaFiles = 0; otaEnded = false;
    Serial.println("[OTA] Unvollstaendiger Upload verworfen (Timeout).");
  }
  if (resetPending && uint32_t(millis() - resetAt) >= 500) {
    WiFi.persistent(true);
#ifdef ESP32
    WiFi.disconnect(false, true);
#else
    WiFi.disconnect(true);
#endif
    WiFi.persistent(false);
    ESP.restart(); return;
  }
#ifndef ESP32
  timingStep();
#endif
  pollFallback();
  if (networkConnected()) {
    if (!wifiWasConnected) {
      connectedSince = millis(); wifiWasConnected = true;
      Serial.printf("[WLAN] Verbunden. Statusseite: http://%s/\n", networkIP().toString().c_str());
    }
    disconnectedAt = millis();
    if (!mdns) { mdns = MDNS.begin("gps-ntp"); if (mdns) { MDNS.addService("http", "tcp", 80); MDNS.addService("ntp", "udp", 123); } }
    // Keep setup accessible for 60 s after connection, then close the AP.
    if (portal && millis() - connectedSince > 60000) { dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA); WiFi.scanDelete(); portal = false; }
  } else {
    wifiWasConnected = false;
    if (millis() - disconnectedAt > 20000) startPortal();
    if (millis() - lastRetry > 30000) { WiFi.reconnect(); lastRetry = millis(); }
    if (mdns) {
#ifdef ESP32
      MDNS.end();
#else
      MDNS.close();
#endif
      mdns = false;
    }
  }
  if (portal) dns.processNextRequest();
#ifndef ESP32
  timingStep();
#endif
  web.handleClient();
#ifndef ESP32
  timingStep();
  if (mdns) MDNS.update();
#endif
  yield();
  if (uint32_t(millis() - lastDiagnostic) >= 10000) {
    lastDiagnostic = millis();
    StateGuard guard;
    unsigned long gpsCharacters = gps.charsProcessed();
    guard.unlock();
    Serial.printf("[STATUS] Laufzeit=%lu s WLAN=%d AP=%s AP-IP=%s GPS-Zeichen=%lu Heap=%lu\n",
                  millis() / 1000UL, int(WiFi.status()), portal ? apName.c_str() : "aus",
                  WiFi.softAPIP().toString().c_str(), gpsCharacters,
                  static_cast<unsigned long>(ESP.getFreeHeap()));
  }
}
#ifdef ESP32
void timingTask(void *) { for (;;) { timingStep(); vTaskDelay(1); } }
void webTask(void *) { for (;;) { networkLoop(); vTaskDelay(1); } }
void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }
#else
void loop() { networkLoop(); }
#endif
