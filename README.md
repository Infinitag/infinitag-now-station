# Infinitag Now – Station

**Die Soundstation eines Zauberstab-Spiels:** Kinder wirken mit einem
Zauberstab Zaubersprüche auf Ziele – trifft der Zauber, spielt diese
Station den passenden Sound-Effekt über einen 12-V-Verstärker. Alles
funkt über ESP-NOW, ganz ohne WLAN-Router, Server oder App – und läuft
damit überall, wo gespielt wird: zum Beispiel als Halloween-Zauberstand
im Vorgarten, auf dem Kindergeburtstag oder der Gartenparty.

![Plattform](https://img.shields.io/badge/Plattform-ESP32--S3-blue)
![Framework](https://img.shields.io/badge/Framework-Arduino%20%2F%20PlatformIO-orange)
![Funk](https://img.shields.io/badge/Funk-ESP--NOW-purple)
![Lizenz](https://img.shields.io/badge/Lizenz-CC%20BY--NC--SA%204.0-lightgrey)

<!-- TODO: Hero-Foto der Station/des Zauberstands einfügen:
     ![Infinitag-Station](docs/station.jpg) -->

## Features

- **Zauberstab-Anschluss:** Der Trigger löst den Zauber aus (IR-Impuls,
  38 kHz) samt Sound; Laser-Zielhilfe, RGBW-LEDs im Stab zeigen den
  Status – Grün = bereit für den nächsten Zauber, Rot = beschäftigt
  (Farben frei konfigurierbar)
- **Satter Sound:** WAV-Wiedergabe über I²S (PCM5102A-DAC → TPA3110-Amp,
  12 V), 15 frei austauschbare Sounds im internen Flash (LittleFS, ab
  Werk Halloween-Effekte), pop-freies Muting
- **Funkgesteuert:** Treffer-Meldungen der Targets, Konfiguration,
  Sound-Test und Selbsttest laufen über das
  [Infinitag-Now-Protokoll](https://github.com/Infinitag/infinitag-now-core)
  (ESP-NOW, Kanal 1, keine Infrastruktur nötig)
- **Kabellose Updates:** Firmware-Update per Browser über einen
  SoftAP-Update-Modus – ausgelöst von der Config-Box, kein Aufschrauben,
  kein USB-Kabel
- **Fernwartung:** Selbsttest (Sound, LEDs, Laser, IR-Selbstempfang,
  Trigger) komplett über die Config-Box ausführbar

## Das Infinitag-Now-System

| Gerät | Repo | Aufgabe |
|---|---|---|
| **Station** | dieses Repo | Sound + Zauberstab (Zauber-Auslösung, Laser, Status-LEDs) |
| **Config-Box** | [infinitag-now-config](https://github.com/Infinitag/infinitag-now-config) | Handheld-Konfigurator: Discovery, Einstellungen, Updates, Live-Monitor |
| **Targets** | infinitag-now-target *(in Arbeit)* | IR-Empfänger an den Zielen, melden Treffer per Funk |
| Protokoll-Lib | [infinitag-now-core](https://github.com/Infinitag/infinitag-now-core) | Paketformat, `EspNowService`, SoftAP-Updater, `PROTOCOL.md` |
| Doku | [infinitag-now](https://github.com/Infinitag/infinitag-now) | Wissensbasis (Hardware, Protokoll, Konzepte) |

Geräte identifizieren sich allein über ihre MAC-Adresse – auspacken,
einschalten, auf der Config-Box „Neu suchen", fertig. Kein Pairing,
keine ID-Vergabe.

## Hardware

ESP32-S3-DevKitC-1 (N16R8) auf eigenem PCB, PCM5102A-DAC + TPA3110-
Verstärker (12 V), SK6812-RGBW-LEDs, TSAL6200-IR-Sender + TSOP4838-
Selbsttest-Empfänger, Laser-Diode, 1,3"-OLED (SH1106, optionales
Service-Modul). Details, Schaltplan-Entscheidungen und GPIO-Plan v7:
Doc 12 der [Wissensbasis](https://github.com/Infinitag/infinitag-now).

## Loslegen

```bash
pio run -t upload        # Firmware flashen (UART)
pio run -t uploadfs      # WAV-Sounds nach LittleFS (data/)
pio device monitor       # Log, 115200 Baud
```

Nur das allererste Flashen braucht USB – danach kommen Updates über
die Luft (siehe unten). Die Sounds (22 kHz, mono, 16 bit) liegen in
`data/` und landen per `uploadfs` im Flash.

## Updates

Jede Version gibt es als [GitHub-Release](../../releases) mit fertiger
`infinitag-station-vX.Y.Z.bin`. Einspielen ohne Kabel:

1. Config-Box → Station wählen → **„Update (OTA)"**
2. Die Station öffnet ein WLAN `infinitag-sta-XXXXXX` (Stab pulsiert blau)
3. Mit Laptop/Handy verbinden, `.bin` auf `http://192.168.4.1` hochladen
4. Die Station prüft das Image, startet neu – die Config-Box meldet
   „Update OK" samt neuer Version

Ein abgebrochener Upload kann nicht booten; ohne Upload kehrt die
Station nach 5 Minuten automatisch zur alten Firmware zurück.

## Konfiguration

Alles Einstellbare läuft über die Config-Box (Funk, persistiert im NVS
der Station): Lautstärke, Stab-Statusfarben (`LED bereit` / `LED aktiv`,
alle 15 R/G/B/W-Kombinationen). Dazu Sound-Test und kompletter
Selbsttest aus der Ferne. Das Funkprotokoll ist in
[`PROTOCOL.md`](https://github.com/Infinitag/infinitag-now-core/blob/main/PROTOCOL.md)
spezifiziert (v0x02).

## Entwicklung

| Pfad | Inhalt |
|---|---|
| `src/main.cpp` | Bring-up-Sketch: I²S/WAV-Player, OLED, NeoPixel, IR, Laser, Tasten, Update-Modus |
| `src/NowStation.*` | ESP-NOW-Gerätelogik (Discovery, CFG, Hit, Selbsttest, Update) |
| `src/StationSettings.*` | Persistente Einstellungen (NVS) |
| `data/` | WAV-Sounds für LittleFS |
| `partitions_16MB.csv` | 2× 3-MB-OTA-Slots + große LittleFS-Partition |

Änderungen laufen über Pull Requests (Squash-Merge, Typ-Label –
Template liegt in `.github/`). Releases entstehen bewusst über
`release.sh` (baut, taggt, erstellt das GitHub-Release inkl. `.bin`).
Protokolländerungen gehören ins Core-Repo. Arduino-Core 2.x ist
bewusst gepinnt.

## Lizenz

[CC BY-NC-SA 4.0](https://creativecommons.org/licenses/by-nc-sa/4.0/) – Tobias Stewen.
Ursprung des Namens und der Idee: das Lasertag-Projekt
[Infinitag](https://github.com/Infinitag) (2017); Infinitag Now ist eine
komplette Neuentwicklung als Zauberstab-Spiel, entstanden für einen
Halloween-Zauberstand.
