#pragma once
#ifdef ESP32
#include <lwip/udp.h>
#include <lwip/pbuf.h>
#include <lwip/tcpip.h>
#include <freertos/queue.h>
#include <atomic>
#include <new>
#include "precision_capture.h"
#include "clock_math.h"

// Raw UDP callbacks run in the lwIP thread. They never take the application's clock mutex.
class PrecisionNtp {
 public:
  bool begin(uint16_t port) {
    queue = xQueueCreate(12, sizeof(Rx));
    initialized = xSemaphoreCreateBinary(); listenPort = port;
    if (!queue || !initialized) return false;
    if (tcpip_callback([](void *arg) {
      auto *self = static_cast<PrecisionNtp *>(arg);
      self->pcb = udp_new_ip_type(IPADDR_TYPE_V4);
      if (self->pcb && udp_bind(self->pcb, IP_ANY_TYPE, self->listenPort) != ERR_OK) {
        udp_remove(self->pcb); self->pcb = nullptr;
      }
      if (self->pcb) udp_recv(self->pcb, receive, self);
      xSemaphoreGive(self->initialized);
    }, this) != ERR_OK) return false;
    xSemaphoreTake(initialized, portMAX_DELAY);
    vSemaphoreDelete(initialized); initialized = nullptr;
    return pcb != nullptr;
  }
  int parsePacket() {
    if (!queue) return 0;
    for (unsigned i = 0; i < 12 && xQueueReceive(queue, &current, 0) == pdTRUE; ++i) {
      if (uint32_t(clockMicros() - current.at) > 20000) { ++drops; continue; }
      unread = current.length < 48 ? current.length : 48;
      return current.length;
    }
    return 0;
  }
  int read(uint8_t *out, size_t length) {
    size_t n = length < unread ? length : unread;
    memcpy(out, current.bytes + (current.length < 48 ? current.length : 48) - unread, n);
    unread -= n; return n;
  }
  int read() { uint8_t byte; return read(&byte, 1) ? byte : -1; }
  int available() const { return unread; }
  IPAddress remoteIP() const { return IPAddress(ip4_addr_get_u32(ip_2_ip4(&current.address))); }
  uint16_t remotePort() const { return current.port; }
  uint32_t receivedAt() const { return current.at; }
  int beginPacket(IPAddress, uint16_t) { return pcb ? 1 : 0; }
  size_t write(const uint8_t *bytes, size_t length) {
    if (length != 48) return 0;
    memcpy(reply, bytes, 48); return length;
  }
  void setMapping(const ClockMapping &value) { mapping = value; }
  int endPacket() {
    auto *job = new (std::nothrow) Tx;
    if (!job) { ++drops; return 0; }
    job->owner = this; job->address = current.address; job->port = current.port;
    job->createdAt = clockMicros();
    job->mapping = mapping; memcpy(job->bytes, reply, 48);
    if (tcpip_try_callback([](void *arg) {
      auto *job = static_cast<Tx *>(arg);
      if (uint32_t(clockMicros() - job->createdAt) > 20000) {
        ++job->owner->drops; delete job; return;
      }
      pbuf *packet = pbuf_alloc(PBUF_TRANSPORT, 48, PBUF_RAM);
      if (packet) {
        job->mapping.timestamp(job->bytes + 40, clockMicros());
        if (pbuf_take(packet, job->bytes, 48) != ERR_OK ||
            udp_sendto(job->owner->pcb, packet, &job->address, job->port) != ERR_OK) ++job->owner->drops;
        pbuf_free(packet);
      } else ++job->owner->drops;
      delete job;
    }, job) != ERR_OK) { delete job; ++drops; return 0; }
    return 1;
  }
  uint32_t dropped() const { return drops.load(); }
 private:
  struct Rx { uint8_t bytes[48]; ip_addr_t address; uint16_t port, length; uint32_t at; };
  struct Tx { uint8_t bytes[48]; ip_addr_t address; uint16_t port; uint32_t createdAt; ClockMapping mapping; PrecisionNtp *owner; };
  static void receive(void *arg, udp_pcb *, pbuf *packet, const ip_addr_t *address, u16_t port) {
    uint32_t at = clockMicros();
    auto *self = static_cast<PrecisionNtp *>(arg);
    if (packet) {
      Rx request = {}; request.at = at; request.address = *address; request.port = port; request.length = packet->tot_len;
      if (request.length >= 48) {
        pbuf_copy_partial(packet, request.bytes, 48, 0);
        if (xQueueSend(self->queue, &request, 0) != pdTRUE) ++self->drops;
      }
      pbuf_free(packet);
    }
  }
  udp_pcb *pcb = nullptr;
  QueueHandle_t queue = nullptr;
  SemaphoreHandle_t initialized = nullptr;
  uint16_t listenPort = 123;
  Rx current = {};
  size_t unread = 0;
  uint8_t reply[48] = {};
  ClockMapping mapping;
  std::atomic<uint32_t> drops{0};
};
#endif
