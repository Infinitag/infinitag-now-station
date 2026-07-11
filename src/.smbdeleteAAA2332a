#include "StationSettings.h"

#include <Preferences.h>

static const char *NVS_NAMESPACE = "inow-station";

void StationSettings::load() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  stationId = prefs.getUChar("id", 0);
  volumePct = prefs.getUChar("vol", 50);
  setupSound = prefs.getUChar("snd", 13);
  prefs.end();
}

void StationSettings::save() const {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
  prefs.putUChar("id", stationId);
  prefs.putUChar("vol", volumePct);
  prefs.putUChar("snd", setupSound);
  prefs.end();
}
