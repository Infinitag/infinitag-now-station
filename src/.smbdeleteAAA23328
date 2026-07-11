// Persistent station configuration (NVS via Preferences).
// Fields mirror the station config blob, see infinitag-now-core PROTOCOL.md.

#pragma once
#include <Arduino.h>

struct StationSettings {
  uint8_t stationId = 0;    // 1..99, 0 = unset ("??" on the config box)
  uint8_t volumePct = 50;   // 0..100, percent of the speaker-safe volume limit
  uint8_t setupSound = 13;  // 0-based index into the sound list

  void load();
  void save() const;
};
