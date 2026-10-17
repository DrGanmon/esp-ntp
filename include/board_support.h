#pragma once
#include <Arduino.h>
#ifdef ESP32
#include <WiFi.h>
#include <ETH.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_system.h>
#include <esp_netif.h>
#include <esp_netif_net_stack.h>
#include <lwip/netif.h>
#include <lwip/tcpip.h>
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
using DeviceWebServer = WebServer;
class FirmwareUpdater : public UpdateClass {
 public:
  ~FirmwareUpdater() { if (isRunning()) abort(); }
  String getErrorString() { return String(errorString()); }
};
inline void selectDefaultNetwork(bool ethernet) {
  esp_netif_t *interface = esp_netif_get_handle_from_ifkey(ethernet ? "ETH_DEF" : "WIFI_STA_DEF");
  if (interface) {
    void *implementation = esp_netif_get_netif_impl(interface);
    if (implementation) tcpip_callback([](void *arg) { netif_set_default(static_cast<netif *>(arg)); }, implementation);
  }
}
constexpr int GPS_RX_PIN = 14, GPS_PPS_PIN = 33;
constexpr const char *BOARD_NAME = "WT32-ETH01";
constexpr auto OPEN_ENCRYPTION = WIFI_AUTH_OPEN;
inline uint32_t deviceId() { return uint32_t(ESP.getEfuseMac()); }
inline uint32_t randomWord() { return esp_random(); }
inline uint32_t firmwareCapacity() {
  const esp_partition_t *partition = esp_ota_get_next_update_partition(nullptr);
  return partition ? partition->size : 0;
}
inline bool ethernetConnected() { return ETH.linkUp() && uint32_t(ETH.localIP()) != 0; }
inline String resetReason() { return String(int(esp_reset_reason())); }
SemaphoreHandle_t clockMutex = nullptr;
class StateGuard {
  bool held = true;
 public:
  StateGuard() { xSemaphoreTakeRecursive(clockMutex, portMAX_DELAY); }
  void unlock() { if (held) { held = false; xSemaphoreGiveRecursive(clockMutex); } }
  ~StateGuard() { unlock(); }
};
portMUX_TYPE ppsMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> gpsRxOverflow{false};
std::atomic<int> resolverState{0};
char resolverHost[64] = {};
IPAddress resolverAddress;
uint32_t resolverUpdated = 0;
inline void resolverTask(void *) {
  IPAddress result;
  bool success = WiFi.hostByName(resolverHost, result);
  resolverAddress = result; resolverUpdated = millis();
  resolverState.store(success ? 2 : 3);
  vTaskDelete(nullptr);
}
// 0: pending, 1: resolved, -1: failure. DNS never blocks GPS or the web loop.
inline int resolveHost(const char *host, IPAddress &result) {
  if (result.fromString(host)) return 1;
  int state = resolverState.load();
  if (state >= 2 && (strcmp(host, resolverHost) || uint32_t(millis() - resolverUpdated) > 300000)) {
    resolverState.store(0); state = 0;
  }
  if (!state) {
    strlcpy(resolverHost, host, sizeof(resolverHost)); resolverState.store(1);
    if (xTaskCreatePinnedToCore(resolverTask, "dns", 4096, nullptr, 1, nullptr, 0) != pdPASS) {
      resolverUpdated = millis(); resolverState.store(3); return -1;
    }
    return 0;
  }
  if (state == 1) return 0;
  if (state == 3) {
    if (uint32_t(millis() - resolverUpdated) > 30000) resolverState.store(0);
    return -1;
  }
  result = resolverAddress; return 1;
}
#else
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <SoftwareSerial.h>
#include <Updater.h>
using DeviceWebServer = ESP8266WebServer;
using FirmwareUpdater = UpdaterClass;
constexpr int GPS_RX_PIN = D5, GPS_PPS_PIN = D6;
constexpr const char *BOARD_NAME = "Wemos D1 mini";
constexpr auto OPEN_ENCRYPTION = ENC_TYPE_NONE;
inline uint32_t deviceId() { return ESP.getChipId(); }
inline uint32_t randomWord() { return os_random(); }
inline uint32_t firmwareCapacity() {
  uint32_t freeSpace = ESP.getFreeSketchSpace();
  return freeSpace > 0x1000 ? (freeSpace - 0x1000) & 0xFFFFF000 : 0;
}
inline bool ethernetConnected() { return false; }
inline String resetReason() { return ESP.getResetReason(); }
class StateGuard { public: ~StateGuard() {} void unlock() {} };
inline int resolveHost(const char *host, IPAddress &result) { return WiFi.hostByName(host, result, 1000) ? 1 : -1; }
#endif
inline bool networkConnected() { return ethernetConnected() || WiFi.status() == WL_CONNECTED; }
inline IPAddress networkIP() {
#ifdef ESP32
  if (ethernetConnected()) return ETH.localIP();
#endif
  return WiFi.localIP();
}
