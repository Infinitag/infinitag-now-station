#include "StationSettings.h"

#include <Preferences.h>

static const char *NVS_NAMESPACE = "inow-station";

void StationSettings::load() {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/true);
  volumePct = prefs.getUChar("vol", 50);
  ledReady = prefs.getUChar("ledrdy", 0x02);
  ledBusy = prefs.getUChar("ledbsy", 0x01);
  prefs.end();
}

void StationSettings::save() const {
  Preferences prefs;
  prefs.begin(NVS_NAMESPACE, /*readOnly=*/false);
  prefs.putUChar("vol", volumePct);
  prefs.putUChar("ledrdy", ledReady);
  prefs.putUChar("ledbsy", ledBusy);
  prefs.end();
}
