# Sounds für die Station

Die WAV-Dateien selbst sind **nicht Teil dieses Repos** (Drittmaterial,
siehe [LICENSE](../LICENSE)) – sie liegen nur lokal in diesem Ordner und
werden per `pio run -t uploadfs` ins LittleFS der Station geschoben.

Welche Sounds es gibt (ID → Datei → Anzeigename), steht zentral im
**Sound-Katalog** des Core-Repos:
[`SoundCatalog.h`](https://github.com/Infinitag/infinitag-now-core/blob/main/src/SoundCatalog.h).
Aus dieser Tabelle nimmt die Station die Dateipfade und die Config-Box
die Namen, die sie in ihren Menüs anzeigt.

## Eigene Sounds hinzufügen oder ersetzen

**1. Datei vorbereiten** – die Station spielt WAVs mit exakt diesem
Format ab (andere Formate werden übersprungen bzw. klingen falsch):

- **WAV (PCM), 22.050 Hz, mono, 16 bit**
- Umwandeln z. B. mit ffmpeg:

  ```bash
  ffmpeg -i quelle.mp3 -ac 1 -ar 22050 -sample_fmt s16 -c:a pcm_s16le /pfad/zu/data/16_mein_sound.wav
  ```

- Dateiname klein, ohne Leerzeichen/Umlaute, mit fortlaufender Nummer
  (`NN_kurzname.wav`) – die Nummer ist reine Konvention für die
  Sortierung, maßgeblich ist der Katalog.

**2. Datei in diesen Ordner legen** (`data/`).

**3. Katalog anpassen** – im Core-Repo in `src/SoundCatalog.h` eine
Zeile ergänzen oder ändern:

```cpp
{"/16_mein_sound.wav", "MeinSnd"},   // Anzeigename max. 8 Zeichen!
```

Die Position in der Tabelle ist die **Sound-ID**, die auch Targets in
ihrer Konfiguration speichern – bestehende Einträge deshalb möglichst
nicht umsortieren oder löschen, sondern hinten anhängen bzw. in-place
ersetzen.

**4. Einspielen** – Reihenfolge beachten, damit IDs, Namen und Dateien
zusammenpassen:

```bash
pio run -t uploadfs     # neue WAVs in die Station (USB)
pio run -t upload       # Station-Firmware mit neuem Katalog
```

Anschließend die **Config-Box** neu bauen/updaten (gleicher Katalog aus
dem Core), sonst zeigt sie für neue IDs `?` an. Beide Geräte lassen sich
auch per OTA aktualisieren – nur die WAVs brauchen einmal das USB-Kabel,
LittleFS wird (noch) nicht über die Luft befüllt.

**5. Testen** – Config-Box → Station → „Sound testen": neuen Eintrag
auswählen (Name erscheint neben der ID) und abspielen.

## Platz

Die LittleFS-Partition ist ~10 MB groß; die 15 Standard-Sounds belegen
rund 4,4 MB. Als Faustregel: 1 Sekunde Audio ≈ 43 KB.

## Eingebetteter Schuss-Sound (nicht in diesem Ordner)

Der Trigger-Sound liegt NICHT im LittleFS, sondern als PCM-Array in der
Firmware (`src/ShotSound.h`), damit er jedes OTA-/Funk-Update automatisch
mitmacht:

| Sound | Quelle | Lizenz |
| --- | --- | --- |
| "Enchant" (Zauber-Schuss) | qubodup, https://freesound.org/s/202147/ | CC BY 4.0 (gemixt aus CC0-Quellen: freesound 157338/symphoid, 180745/Selector) |
