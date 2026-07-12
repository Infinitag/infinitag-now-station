# CLAUDE.md – infinitag-now-station

Firmware der Wand-Station des Infinitag-Now-Systems (Solo-Projekt von
Tobias Stewen; Halloween-Schießbude, infrastrukturloses ESP-NOW-Setup).

## Sprache & Stil

Antworten/Doku Deutsch, Code-Kommentare Englisch. Optionen vorschlagen,
nicht nur ausführen; größere Umbauten erst absprechen.

## Verbindliche Doku (VOR Änderungen lesen!)

- `12-refactor-station-v2.md` der Wissensbasis (Master:
  `/Volumes/Basteln/Infinitag/wissensbasis/`, Kopie in
  `infinitag-now/docs/`) – Hardware-Entscheidungen Station V2
  (GPIO-Plan v7, Audio-Kette, Mute-Strategie B, LittleFS, ESP-NOW § 3).
- `PROTOCOL.md` im Repo `infinitag-now-core` – Funkprotokoll.
  **Protokolländerungen nur dort** (Code + Spec + Tests in einem Commit).
- Doc 14/17: Bring-up-Erkenntnisse zum Lochraster-Prototyp.

## Build

```bash
pio run -t upload      # UART-Flash (CDC-Boot machte Reset-Loops, s. platformio.ini)
pio run -t uploadfs    # data/-WAVs nach LittleFS
```

Die Core-Lib kommt per `lib_deps`-**Symlink** auf die lokale Arbeitskopie
(`repos/infinitag-now-core`; GitHub-Tag als Alternative in der
`platformio.ini` kommentiert). Arduino-Core 2.x (ledcSetup/ledcAttachPin-
API, alte esp_now-Callback-Signatur) – nicht ungefragt auf 3.x heben.

## Architektur

`src/main.cpp` = Bring-up-Sketch (I²S/WAV, OLED SH1106, NeoPixel am Stab,
IR-Burst 38 kHz, Laser, 4 Tasten + Stab-Trigger). Darauf aufgesetzt:
`src/NowStation.*` (ESP-NOW-Gerätelogik v0x02: Discovery, Identify, CFG,
Test-Sound, HIT_REPORT per MAC, UPDATE_BEGIN) und
`src/StationSettings.*` (NVS: volumePct, ledReady, ledBusy).

Wichtige Verhaltensregeln aus dem Bring-up (nicht kaputt machen):
- I²S streamt DAUERHAFT; Mute nur über XSMT (Pop-Vermeidung).
- Stab-LED: Identify-Weiß > busy (ledBusy, Default Rot) > bereit
  (ledReady, Default Grün) – `updateStatusLed()`, auch direkt vor/nach
  `playWav()` gerufen, weil das blockiert.
- `playWav()` blockiert – ESP-NOW-Pakete werden erst danach verarbeitet
  (RX-Ringpuffer fängt 8 Pakete; bekannte Einschränkung V0.1).
- `UPDATE_BEGIN` → `runUpdateMode()`: blockierender SoftAP-Updater
  (`WebUpdateService` aus dem Core), endet immer in `ESP.restart()`.

## Stand 2026-07-12 (FW 0.2.0) / nächste Schritte

- **Protokoll v0x02:** keine Stations-ID mehr (MAC = Identität, OLED
  zeigt MAC-Suffix), Setup-Flow entfernt, HIT_REPORT filtert auf eigene
  MAC im Payload. OTA-Update per SoftAP; Partitionstabelle hat zwei
  3-MB-OTA-Slots. **Auf Hardware ungetestet.** Erster Test gegen die
  Config-Box: Discovery → Identify → CFG_WRITE (Volume/LED-Farben) →
  Sound-Test → Selbsttest → Update (OTA).
- Danach: Sound-Anzahl dynamisch melden (offener Protokollpunkt),
  Target-Firmware (`infinitag-now-target`) für HIT_REPORT-Tests.
- Sound-Indizes sind 0-basiert (0..14); die Config-Box begrenzt aktuell
  hart auf 0..13 (`SOUND_ID_MAX`) – offener Punkt.
- FW-Version (`STATION_FW_*` in `NowStation.h`) bei JEDEM geflashten
  Stand hochzählen – der Versions-Check der Config-Box vergleicht sie.

## Lizenz

CC BY-NC-SA 4.0 – Tobias Stewen.
