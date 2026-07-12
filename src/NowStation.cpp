#include "NowStation.h"

using namespace inow;

// All Infinitag-Now devices are pinned to this WiFi channel (PROTOCOL.md).
static constexpr uint8_t ESPNOW_CHANNEL = 1;

bool NowStation::begin(StationSettings *settings, PlayFn playFn,
                       uint8_t numSounds, ConfigChangedFn onConfigChanged,
                       const DebugHooks *hooks) {
  _hooks = hooks;
  _settings = settings;
  _play = playFn;
  _numSounds = numSounds;
  _onConfigChanged = onConfigChanged;
  _bootMs = millis();
  return _net.begin(ESPNOW_CHANNEL);
}

void NowStation::loop() {
  RxPacket rx;
  while (_net.receive(rx)) handlePacket(rx);

  // armed trigger test timed out?
  if (_trigTestUntil != 0 && millis() >= _trigTestUntil) {
    _trigTestUntil = 0;
    sendDebugResult(_trigTestMac, DBG_TRIGGER, DBG_RES_TIMEOUT);
    Serial.println("[NOW] Trigger-Test: TIMEOUT");
    _dirty = true;
  }
}

uint8_t NowStation::consumeUpdateRequest() {
  const uint8_t m = _updateReqMin;
  _updateReqMin = 0;
  return m;
}

bool NowStation::consumeTriggerTest() {
  if (_trigTestUntil == 0 || millis() >= _trigTestUntil) return false;
  _trigTestUntil = 0;
  sendDebugResult(_trigTestMac, DBG_TRIGGER, DBG_RES_OK);
  Serial.println("[NOW] Trigger-Test: OK (gedrueckt)");
  _dirty = true;
  return true;
}

void NowStation::sendDebugResult(const uint8_t mac[6], uint8_t test,
                                 uint8_t result) {
  Packet p;
  init(p, MSG_DEBUG_RESULT, DEV_STATION);
  p.payload[0] = test;
  p.payload[1] = result;
  _net.send(mac, p);
}

void NowStation::handleDebugCmd(const RxPacket &rx) {
  const uint8_t test = rx.pkt.payload[0];
  const uint8_t param = rx.pkt.payload[1];
  Serial.printf("[NOW] DEBUG_CMD: Test %u, Param %u\n", test, param);

  switch (test) {
    case DBG_SOUND:
      if (_play && param < _numSounds) {
        _play(param);  // blocking; result marks "done playing"
        sendDebugResult(rx.mac, test, DBG_RES_OK);
      } else {
        sendDebugResult(rx.mac, test, DBG_RES_FAIL);
      }
      break;

    case DBG_LED:
      if (_hooks && _hooks->ledTest) {
        _hooks->ledTest();
        sendDebugResult(rx.mac, test, DBG_RES_OK);
      } else {
        sendDebugResult(rx.mac, test, DBG_RES_UNSUPPORTED);
      }
      break;

    case DBG_LASER:
      if (_hooks && _hooks->laserPulse) {
        uint8_t s = param == 0 ? 2 : param;
        if (s > 10) s = 10;  // safety clamp
        _hooks->laserPulse(s);
        sendDebugResult(rx.mac, test, DBG_RES_OK);
      } else {
        sendDebugResult(rx.mac, test, DBG_RES_UNSUPPORTED);
      }
      break;

    case DBG_IR:
      if (_hooks && _hooks->irBurst) {
        const bool ok = _hooks->irBurst(param == 0 ? 1 : param);
        sendDebugResult(rx.mac, test, ok ? DBG_RES_OK : DBG_RES_FAIL);
      } else {
        sendDebugResult(rx.mac, test, DBG_RES_UNSUPPORTED);
      }
      break;

    case DBG_TRIGGER: {
      const uint8_t s = param == 0 ? 10 : param;
      _trigTestUntil = millis() + (uint32_t)s * 1000UL;
      memcpy(_trigTestMac, rx.mac, 6);
      _dirty = true;  // display may show "Trigger druecken!"
      break;
    }

    default:
      sendDebugResult(rx.mac, test, DBG_RES_UNSUPPORTED);
      break;
  }
}

bool NowStation::consumeDirty() {
  const bool d = _dirty;
  _dirty = false;
  return d;
}

void NowStation::sendDiscoverReply(const uint8_t mac[6], uint8_t token) {
  DiscoverReply r;
  r.fw_major = STATION_FW_MAJOR;
  r.fw_minor = STATION_FW_MINOR;
  r.fw_patch = STATION_FW_PATCH;
  r.rssi_self = 0;  // RSSI not available via the plain recv callback
  r.uptime_min = (uint16_t)((millis() - _bootMs) / 60000UL);

  StationConfig c;
  c.volume_pct = _settings->volumePct;
  c.led_ready = _settings->ledReady;
  c.led_busy = _settings->ledBusy;
  r.config_blob_len = STATION_BLOB_SIZE;
  encodeStationConfig(c, r.config_blob);

  Packet p;
  init(p, MSG_DISCOVER_REPLY, DEV_STATION);
  p.token = token;  // echo protection
  encodeDiscoverReply(r, p.payload);
  _net.send(mac, p);
}

void NowStation::sendAck(const uint8_t mac[6], uint8_t status) {
  Packet p;
  init(p, MSG_CFG_ACK, DEV_STATION);
  p.payload[0] = status;
  _net.send(mac, p);
}

void NowStation::handlePacket(const RxPacket &rx) {
  const Packet &p = rx.pkt;

  switch (p.msg_type) {
    case MSG_DISCOVER_REQ: {
      const uint8_t filter = p.payload[0];
      if (filter == DEV_STATION || filter == DEV_ANY) {
        sendDiscoverReply(rx.mac, p.token);
      }
      break;
    }

    case MSG_IDENTIFY:
      // payload[0] = duration in 100 ms units (self-clearing window)
      _identifyUntil = millis() + (uint32_t)p.payload[0] * 100UL;
      break;

    case MSG_CFG_WRITE: {
      if (p.device_type != DEV_STATION) break;
      StationConfig c;
      decodeStationConfig(p.payload, STATION_BLOB_SIZE, c);
      if (c.volume_pct > 100 || c.led_ready > LED_MASK_MAX ||
          c.led_busy > LED_MASK_MAX) {
        sendAck(rx.mac, ACK_NACK_VALIDATION);
        break;
      }
      _settings->volumePct = c.volume_pct;
      _settings->ledReady = c.led_ready;
      _settings->ledBusy = c.led_busy;
      _settings->save();
      if (_onConfigChanged) _onConfigChanged();
      _dirty = true;
      sendAck(rx.mac, ACK_OK);
      Serial.printf("[NOW] CFG_WRITE: vol=%u%% ledRdy=0x%X ledBsy=0x%X\n",
                    c.volume_pct, c.led_ready, c.led_busy);
      break;
    }

    case MSG_CFG_TEST_SOUND:
      if (_play && p.payload[0] < _numSounds) {
        Serial.printf("[NOW] Test-Sound %u\n", p.payload[0]);
        _play(p.payload[0]);
      }
      break;

    case MSG_HIT_REPORT: {
      // Broadcast from a target; play only if it is addressed to our MAC.
      uint8_t dest[6];
      uint8_t sound = 0;
      decodeHitReport(p.payload, dest, sound);
      if (memcmp(dest, _net.ownMac(), 6) == 0 && _play && sound < _numSounds) {
        Serial.printf("[NOW] HIT_REPORT von %02X%02X%02X -> Sound %u\n",
                      rx.mac[3], rx.mac[4], rx.mac[5], sound);
        _play(sound);
      }
      break;
    }

    case MSG_UPDATE_BEGIN: {
      // Ack first (the send is queued before we tear ESP-NOW down in main).
      Packet ack;
      init(ack, MSG_UPDATE_ACK, DEV_STATION);
      ack.payload[0] = 0;
      _net.send(rx.mac, ack);
      _updateReqMin = p.payload[0] == 0 ? 5 : p.payload[0];
      _dirty = true;
      Serial.printf("[NOW] UPDATE_BEGIN: SoftAP-Update-Modus, %u min\n",
                    _updateReqMin);
      break;
    }

    case MSG_DEBUG_CMD:
      if (p.device_type == DEV_STATION || p.device_type == DEV_ANY)
        handleDebugCmd(rx);
      break;

    case MSG_PUSH_BEGIN:
    case MSG_PUSH_END:
      if (_onPush) _onPush(rx);
      break;

    default:
      break;
  }
}
