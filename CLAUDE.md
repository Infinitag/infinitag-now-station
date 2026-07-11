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

Die Core-Lib kommt per `lib_deps`-Git-URL (privates Repo, Git braucht
Org-Zugriff). Arduino-Core 2.x (ledcSetup/ledcAttachPin-API, alte
esp_now-Callback-Signatur) – nicht ungefragt auf 3.x heben.

## Architektur

`src/main.cpp` = Bring-up-Sketch (I²S/WAV, OLED SH1106, NeoPixel am Stab,
IR-Burst 38 kHz, Laser, 4 Tasten + Stab-Trigger). Darauf aufgesetzt:
`src/NowStation.*` (ESP-NOW-Gerätelogik: Discovery, Identify, CFG, Setup,
Test-Sound, HIT_REPORT) und `src/StationSettings.*` (NVS).

Wichtige Verhaltensregeln aus dem Bring-up (nicht kaputt machen):
- I²S streamt DAUERHAFT; Mute nur über XSMT (Pop-Vermeidung).
- NeoPixel: Identify-Weiß > Setup-Lila > Farb-Sichttest (Prioritätskette
  am Ende von `loop()`).
- Trigger ist im Setup-Modus Bestätigungstaster, sonst Schuss (IR+Sound).
- `playWav()` blockiert – ESP-NOW-Pakete werden erst danach verarbeitet
  (RX-Ringpuffer fängt 8 Pakete; bekannte Einschränkung V0.1).

## Stand 2026-07-11 / nächste Schritte

- ESP-NOW-Logik geschrieben, **auf Hardware ungetestet**. Erster Test:
  gegen die Config-Box (Discovery → Identify → CFG_WRITE → Sound-Test,
  dann Setup-Flow per Trigger).
- Danach: Sound-Anzahl dynamisch melden (offener Protokollpunkt),
  Target-Firmware (`infinitag-now-target`) für HIT_REPORT-Tests.
- Sound-Indizes sind 0-basiert (0..14); die Config-Box begrenzt aktuell
  hart auf 0..13 (`SOUND_ID_MAX`) – offener Punkt.

## Lizenz

CC BY-NC-SA 4.0 – Tobias Stewen.
