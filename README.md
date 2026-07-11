# infinitag-now-station

Firmware der **Wand-Station** für Infinitag Now: ESP32-S3 mit I²S-Audio
(PCM5102A → TPA3110), LittleFS-Sounds, Stab-Anschluss (Trigger, IR, Laser,
NeoPixel) und **ESP-NOW-Gerätelogik** für Config-Box und Targets.

Basis ist der Hardware-Bring-up-Sketch (Doc 12/14/17 der Wissensbasis,
Stand 2026-06-21), erweitert um das Infinitag-Now-Protokoll aus
[`infinitag-now-core`](https://github.com/Infinitag/infinitag-now-core).

**Stand:** ESP-NOW-Logik geschrieben 2026-07-11, auf Hardware ungetestet.

## Bauen & Flashen

```bash
pio run -t upload        # Firmware (ESP32-S3-DevKitC-1, UART-Flash)
pio run -t uploadfs      # WAV-Sounds nach LittleFS (data/)
pio device monitor
```

## ESP-NOW-Funktionen (Geräteseite)

| Nachricht | Verhalten |
|---|---|
| `DISCOVER_REQ` | antwortet mit `DISCOVER_REPLY` (FW-Version, Uptime, Config-Blob, Token-Echo) |
| `IDENTIFY` | Stab-LEDs pulsen weiß (selbstlöschendes 700-ms-Fenster) |
| `CFG_WRITE` | validiert + persistiert ID/Volume/Setup-Sound ins NVS, antwortet `CFG_ACK` |
| `CFG_TEST_SOUND` | spielt den Sound ab (ohne Persistierung) |
| `HIT_REPORT` | spielt den Sound, wenn `station_id` im Header der eigenen entspricht |
| `SETUP_BEGIN` | Setup-Modus: LEDs lila, Trigger = „diese ID übernehmen", dann `SETUP_TAKE`-Broadcast |

Details: `PROTOCOL.md` im Core-Repo. Persistenz: `src/StationSettings.*`
(NVS, Namespace `inow-station`). Protokoll-Handling: `src/NowStation.*`.
Der restliche `src/main.cpp` ist weiterhin der Bring-up-Sketch mit
Bedien-UI (K1 Volume, K2 Sound, K3 Laser, K4 Vorhören, Trigger = Schuss).

## Struktur

| Pfad | Inhalt |
|---|---|
| `src/main.cpp` | Bring-up-Sketch: I²S/WAV-Player, OLED, NeoPixel, IR, Laser, Tasten + Now-Integration |
| `src/NowStation.*` | ESP-NOW-Gerätelogik (siehe Tabelle oben) |
| `src/StationSettings.*` | Persistente Config im NVS |
| `data/` | 15 WAV-Sounds (22 kHz mono 16 bit) für LittleFS |
| `partitions_16MB.csv` | Partitionstabelle (16 MB Flash, große LittleFS-Partition) |

## Lizenz

CC BY-NC-SA 4.0 – Tobias Stewen.
