#include "NowStation.h"

using namespace inow;

// All Infinitag-Now devices are pinned to this WiFi channel (PROTOCOL.md).
static constexpr uint8_t ESPNOW_CHANNEL = 1;

// Station firmware version, reported in DISCOVER_REPLY.
static constexpr uint8_t FW_MAJOR = 0;
static constexpr uint8_t FW_MINOR = 1;
static constexpr uint8_t FW_PATCH = 0;

bool NowStation::begin(StationSettings *settings, PlayFn playFn,
                       uint8_t numSounds, ConfigChangedFn onConfigChanged) {
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
}

bool NowStation::consumeDirty() {
  const bool d = _dirty;
  _dirty = false;
  return d;
}

void NowStation::sendDiscoverReply(const uint8_t mac[6], uint8_t token) {
  DiscoverReply r;
  r.fw_major = FW_MAJOR;
  r.fw_minor = FW_MINOR;
  r.fw_patch = FW_PATCH;
  r.rssi_self = 0;  // RSSI not available via the plain recv callback
  r.uptime_min = (uint16_t)((millis() - _bootMs) / 60000UL);

  StationConfig c;
  c.station_id = _settings->stationId;
  c.volume_pct = _settings->volumePct;
  c.default_setup_sound = _settings->setupSound;
  r.config_blob_len = STATION_BLOB_SIZE;
  encodeStationConfig(c, r.config_blob);

  Packet p;
  init(p, MSG_DISCOVER_REPLY, DEV_STATION);
  p.station_id = _settings->stationId;
  p.token = token;  // echo protection
  encodeDiscoverReply(r, p.payload);
  _net.send(mac, p);
}

void NowStation::sendAck(const uint8_t mac[6], uint8_t status) {
  Packet p;
  init(p, MSG_CFG_ACK, DEV_STATION);
  p.station_id = _settings->stationId;
  p.payload[0] = status;
  _net.send(mac, p);
}

void NowStation::confirmSetup() {
  if (!setupActive()) return;

  uint8_t newId = _setupPendingId;
  if (newId == 0) newId = (_settings->stationId == 0) ? 1 : _settings->stationId;

  _settings->stationId = newId;
  _settings->save();
  _setupUntil = 0;
  _dirty = true;

  Packet p;
  init(p, MSG_SETUP_TAKE, DEV_STATION);
  p.station_id = newId;
  p.payload[0] = newId;
  _net.sendBroadcast(p);

  Serial.printf("[NOW] Setup bestaetigt: Station-ID = %u\n", newId);
  if (_play && _settings->setupSound < _numSounds) _play(_settings->setupSound);
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
      if (c.station_id < 1 || c.station_id > 99 || c.volume_pct > 100 ||
          c.default_setup_sound >= _numSounds) {
        sendAck(rx.mac, ACK_NACK_VALIDATION);
        break;
      }
      _settings->stationId = c.station_id;
      _settings->volumePct = c.volume_pct;
      _settings->setupSound = c.default_setup_sound;
      _settings->save();
      if (_onConfigChanged) _onConfigChanged();
      _dirty = true;
      sendAck(rx.mac, ACK_OK);
      Serial.printf("[NOW] CFG_WRITE: id=%u vol=%u%% setupSnd=%u\n",
                    c.station_id, c.volume_pct, c.default_setup_sound);
      break;
    }

    case MSG_CFG_TEST_SOUND:
      if (_play && p.payload[0] < _numSounds) {
        Serial.printf("[NOW] Test-Sound %u\n", p.payload[0]);
        _play(p.payload[0]);
      }
      break;

    case MSG_HIT_REPORT:
      // Broadcast from a target; only react if it is addressed to our id.
      if (_settings->stationId != 0 && p.station_id == _settings->stationId &&
          _play && p.payload[0] < _numSounds) {
        Serial.printf("[NOW] HIT_REPORT: Target %u -> Sound %u\n", p.target_id,
                      p.payload[0]);
        _play(p.payload[0]);
      }
      break;

    case MSG_SETUP_BEGIN:
      // header.station_id = id to assign (0 = keep current), payload[0] = timeout s
      _setupUntil = millis() + (uint32_t)p.payload[0] * 1000UL;
      _setupPendingId = p.station_id;
      _dirty = true;
      Serial.printf("[NOW] SETUP_BEGIN: pending id=%u, timeout=%us\n",
                    _setupPendingId, p.payload[0]);
      break;

    case MSG_SETUP_TAKE:
      // Another station confirmed -> leave setup mode silently.
      if (setupActive()) {
        _setupUntil = 0;
        _dirty = true;
        Serial.println("[NOW] SETUP_TAKE von anderer Station -> IDLE");
      }
      break;

    default:
      break;
  }
}
