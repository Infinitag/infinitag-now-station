// Device-side Infinitag-Now protocol handling for the station (v0x02):
// DISCOVER_REPLY, IDENTIFY, CFG_WRITE/CFG_ACK, CFG_TEST_SOUND, HIT_REPORT
// (MAC-routed) and UPDATE_BEGIN. Uses the shared EspNowService from
// infinitag-now-core. See PROTOCOL.md in that repo.

#pragma once
#include <Arduino.h>

#include "EspNowService.h"
#include "InfinitagNow.h"
#include "StationSettings.h"

// Station firmware version, reported in DISCOVER_REPLY and shown on the
// update page. Bump on every flashed release, the config box compares it.
static constexpr uint8_t STATION_FW_MAJOR = 0;
static constexpr uint8_t STATION_FW_MINOR = 3;
static constexpr uint8_t STATION_FW_PATCH = 2;

// Hardware hooks for the remote self-test (implemented in main.cpp).
struct DebugHooks {
  void (*ledTest)();              // run LED test pattern (may block ~2 s)
  void (*laserPulse)(uint8_t s);  // laser on, auto-off after s seconds
  bool (*irBurst)(uint8_t ms);    // send IR burst, return TSOP self-check
};

class NowStation {
 public:
  // playFn: plays a sound by 0-based index (bounds already checked here).
  // onConfigChanged: apply freshly written settings (e.g. volume) in main.
  using PlayFn = void (*)(uint8_t soundIdx);
  using ConfigChangedFn = void (*)();

  bool begin(StationSettings *settings, PlayFn playFn, uint8_t numSounds,
             ConfigChangedFn onConfigChanged, const DebugHooks *hooks = nullptr);

  // Call every loop() iteration: drains the RX queue, handles timeouts.
  void loop();

  // True while an IDENTIFY window is running (LED override: white pulse).
  bool identifyActive() const { return millis() < _identifyUntil; }

  // Access for the ESP-NOW push receiver (raw-frame hook + acks).
  EspNowService *net() { return &_net; }

  // PUSH_BEGIN/PUSH_END control packets are forwarded here (set by main;
  // bridges to the EspNowPushReceiver).
  using PushControlFn = void (*)(const RxPacket &rx);
  void setPushHandler(PushControlFn fn) { _onPush = fn; }

  // UPDATE_BEGIN received: returns the requested timeout in minutes exactly
  // once, 0 = nothing pending. Caller must then enter the SoftAP update
  // mode (blocking) and reboot afterwards.
  uint8_t consumeUpdateRequest();

  // Trigger pressed while a DBG_TRIGGER test is armed: reports OK to the
  // config box and returns true (main must then NOT fire a shot).
  bool consumeTriggerTest();

  // True once after a state change that warrants a display redraw.
  bool consumeDirty();

  const uint8_t *ownMac() { return _net.ownMac(); }

 private:
  void handlePacket(const RxPacket &rx);
  void sendDiscoverReply(const uint8_t mac[6], uint8_t token);
  void sendAck(const uint8_t mac[6], uint8_t status);
  void handleDebugCmd(const RxPacket &rx);
  void sendDebugResult(const uint8_t mac[6], uint8_t test, uint8_t result);

  EspNowService _net;
  StationSettings *_settings = nullptr;
  PlayFn _play = nullptr;
  ConfigChangedFn _onConfigChanged = nullptr;
  uint8_t _numSounds = 0;

  const DebugHooks *_hooks = nullptr;
  PushControlFn _onPush = nullptr;

  // armed DBG_TRIGGER test
  uint32_t _trigTestUntil = 0;
  uint8_t _trigTestMac[6] = {0};

  uint32_t _identifyUntil = 0;
  uint8_t _updateReqMin = 0;  // pending UPDATE_BEGIN timeout, 0 = none
  uint32_t _bootMs = 0;
  bool _dirty = false;
};
