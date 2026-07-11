// Device-side Infinitag-Now protocol handling for the station:
// DISCOVER_REPLY, IDENTIFY, CFG_WRITE/CFG_ACK, SETUP_BEGIN/SETUP_TAKE,
// CFG_TEST_SOUND and HIT_REPORT. Uses the shared EspNowService from
// infinitag-now-core. See PROTOCOL.md in that repo.

#pragma once
#include <Arduino.h>

#include "EspNowService.h"
#include "InfinitagNow.h"
#include "StationSettings.h"

class NowStation {
 public:
  // playFn: plays a sound by 0-based index (bounds already checked here).
  // onConfigChanged: apply freshly written settings (e.g. volume) in main.
  using PlayFn = void (*)(uint8_t soundIdx);
  using ConfigChangedFn = void (*)();

  bool begin(StationSettings *settings, PlayFn playFn, uint8_t numSounds,
             ConfigChangedFn onConfigChanged);

  // Call every loop() iteration: drains the RX queue, handles timeouts.
  void loop();

  // True while an IDENTIFY window is running (LED override: white pulse).
  bool identifyActive() const { return millis() < _identifyUntil; }

  // True while SETUP_BEGIN mode is active (LED override: purple, trigger
  // acts as "confirm" instead of firing).
  bool setupActive() const { return millis() < _setupUntil; }

  // Trigger pressed while setupActive(): persist the pending id, broadcast
  // SETUP_TAKE, play the setup sound.
  void confirmSetup();

  // True once after a state change that warrants a display redraw.
  bool consumeDirty();

  const uint8_t *ownMac() { return _net.ownMac(); }

 private:
  void handlePacket(const RxPacket &rx);
  void sendDiscoverReply(const uint8_t mac[6], uint8_t token);
  void sendAck(const uint8_t mac[6], uint8_t status);

  EspNowService _net;
  StationSettings *_settings = nullptr;
  PlayFn _play = nullptr;
  ConfigChangedFn _onConfigChanged = nullptr;
  uint8_t _numSounds = 0;

  uint32_t _identifyUntil = 0;
  uint32_t _setupUntil = 0;
  uint8_t _setupPendingId = 0;  // id offered in SETUP_BEGIN header (0 = keep)
  uint32_t _bootMs = 0;
  bool _dirty = false;
};
