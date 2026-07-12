// Persistent station configuration (NVS via Preferences).
// Fields mirror the station config blob, see infinitag-now-core PROTOCOL.md.
// Since protocol v0x02 the station has no user-assigned id anymore – the
// eFuse MAC is the identity.

#pragma once
#include <Arduino.h>

struct StationSettings {
  uint8_t volumePct = 50;   // 0..100, percent of the speaker-safe volume limit
  uint8_t ledReady = 0x02;  // wand LED mask "ready" (bit0=R,1=G,2=B,3=W), default green
  uint8_t ledBusy = 0x01;   // wand LED mask "busy" (audio playing), default red

  void load();
  void save() const;
};
