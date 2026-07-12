/**
 * Infinitag Station V2 – Test-Sketch mit OLED + 4 Tasten
 *
 * Zweck dieses Sketches:
 *   Manueller Audio-Test mit Bedien-UI. Tasten K1/K2 cyceln Volume bzw.
 *   Sound, K3 toggelt den Laser, K4 spielt den ausgewählten Sound vor
 *   (ohne IR-Burst). Der Stab-Trigger feuert IR + Sound = realer Schuss.
 *
 *   Das ist NICHT der finale Stations-Code, sondern ein Hardware-Bring-up.
 *
 * GPIO-Plan v7 (alle in diesem Sketch verwendeten Pins):
 *   GPIO8  → OLED I²C SDA
 *   GPIO9  → OLED I²C SCL
 *   GPIO15 → PCM5102A BCK   (I²S Bit-Clock)
 *   GPIO16 → PCM5102A LCK   (I²S Word-Select / LR-Clock)
 *   GPIO17 → PCM5102A DIN   (I²S Daten)
 *   GPIO12 → PCM5102A XSMT  (HIGH = aktiv, LOW = Soft-Mute)
 *   GPIO4  → Stab-Trigger   (active-LOW, INPUT_PULLUP, Doc 14 Punkt 2.3)
 *   GPIO35 → Taste K1  (Volume cyclen, +VOLUME_STEP → wrap auf VOLUME_MIN)
 *   GPIO36 → Taste K2  (Sound cyclen, +1 → wrap; KEIN Abspielen)
 *   GPIO37 → Taste K3  (Laser-Toggle – siehe Doc 14, 2.5)
 *   GPIO38 → Taste K4  (aktuellen Sound vorhoeren, kein IR-Burst)
 *   GPIO7  → Laser-Treiber (BC546C Low-Side-Switch, active-HIGH)
 *
 * OLED-Modul: 1,3" SH1106 (128x64) mit integrierter 4-Tasten-Platine,
 *   Pin-Reihenfolge auf dem Modul (von Modul-Rand aus):
 *   GND · VCC · SCL · SDA · K4 · K3 · K2 · K1
 *   Tasten schalten gegen GND, interne Pullups am ESP aktivieren.
 *
 * Mute-Strategie (Plan A) bleibt identisch zum vorigen Audio-Test:
 *   I²S läuft dauerhaft, DAC wird über XSMT gemutet/aktiviert.
 */

#include <Arduino.h>
#include "driver/i2s.h"
#include <LittleFS.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <Adafruit_NeoPixel.h>

#include "NowStation.h"
#include "StationSettings.h"
#include "WebUpdateService.h"

// ── Pins: Audio ──────────────────────────────────────────────────────────────
#define I2S_BCLK     15
#define I2S_LRC      16
#define I2S_DOUT     17
#define XSMT_PIN     12   // PCM5102A Soft-Mute: HIGH = aktiv, LOW = stumm

// ── Pins: OLED I²C ───────────────────────────────────────────────────────────
#define OLED_SDA      8
#define OLED_SCL      9

// ── Pins: Tasten (active-LOW, interne Pullups) ───────────────────────────────
#define BTN_K1       35   // Volume cyclen (Stufe +1, wrap auf MIN)
#define BTN_K2       36   // Sound cyclen  (Index +1, wrap; KEIN Abspielen)
#define BTN_K3       37   // Laser-Toggle  (an/aus)
#define BTN_K4       38   // Aktuellen Sound vorhoeren (kein IR-Burst)

// ── Pin: Stab-Trigger (active-LOW, INPUT_PULLUP) ─────────────────────────────
// Schaltet gegen GND. Auf dem PCB hängt hier später eine Ader vom USB-C-
// Stab-Connector. Fuer den Bring-up-Test reicht ein Pushbutton zwischen
// GPIO 4 und GND.
#define TRIG_PIN      4   // Stab-Trigger → Test-Sound abspielen

// ── Pin: Laser-Treiber (active-HIGH) ────────────────────────────────────────
// GPIO 7 → 820 Ω → Base BC546C; Emitter → GND; Collector → Laser−; Laser+ → 5 V.
// HIGH = Transistor leitet = Laser AN.
#define LASER_PIN     7

// ── Pin: IR-LED-Treiber (38 kHz Burst via LEDC PWM) ─────────────────────────
// GPIO 5 → 820 Ω → Base BC546C #2; Emitter → GND; Collector → TSAL6200 Kathode;
// TSAL6200 Anode → 33–100 Ω → +5 V. PWM-Modus 38 kHz, 33 % Duty fuer TSOP.
// API-Wahl: ledcSetup/ledcAttachPin (Arduino-ESP32 v2.x). Bei spaeterem
// Wechsel auf v3.x kann auf ledcAttach() vereinfacht werden.
#define IR_PIN          5
#define IR_LEDC_CHANNEL 0    // ledc-Channel 0 (8 Channels frei auf ESP32-S3)
#define IR_FREQ_HZ      38000
#define IR_DUTY_8BIT    85   // 85/256 ≈ 33 % Duty (TSOP-Optimum)

// ── Pin: TSOP4838 (38 kHz IR-Demoduler, OUT ist active-LOW) ─────────────────
// TSOP-OUT → GPIO 10 (INPUT_PULLUP intern, TSOP hat eigene interne Pullup).
// Bei IR-Empfang zieht der TSOP-OUT auf LOW.
#define TSOP_PIN     10

// ── Pin: SK6812 NeoPixel (Plan v7 GPIO 6) ───────────────────────────────────
// DIN → GPIO 6 ueber Level-Shifter U1 (74AHCT125, 3,3→5 V).
//
// Stand 2026-06-21: TEMPORAERER Isolationstest zur Stab-Fehlersuche.
// Die 8 Station-LEDs (H1) sind ENTFERNT und H1 Pin2↔Pin3 gebrueckt, damit
// das Datensignal direkt zum Stab durchlaeuft. Am ersten Stab-Pixel haengt
// EINE SK6812 in der RGBW-Variante (5050, eigener Weiss-Kanal) → daher
// NEO_GRBW (32 Bit/Pixel statt 24). Nur 1 Pixel in der Kette.
// Datenweg: U1 → R5 470Ω → [H1-Bruecke] → D1 ESD → R1 100Ω → 15 cm Kabel →
// Stab R1 150Ω → SK6812-DIN. Spaeter wieder auf produktive Anzahl/Variante.
#define NEOPIXEL_PIN     6
#define NEOPIXEL_COUNT   2     // 2x SK6812 RGBW am Stab
#define NEOPIXEL_BRIGHT  110   // ~43 %, Test: volle 0xFF-Bytes vermeiden (Magenta-Diagnose)

// ── I²S-Konfiguration ────────────────────────────────────────────────────────
#define I2S_PORT        I2S_NUM_0
#define SAMPLE_RATE     22050
#define DMA_BUF_COUNT   8
#define DMA_BUF_LEN     1024   // Samples pro Buffer → ~46 ms Reserve

// ── Speaker-Profile ──────────────────────────────────────────────────────────
// TPA3110 an 12 V / 4 Ω liefert ~12 W RMS (Mono-Bridge / 1 Kanal).
// Der Volume-Faktor gVolume skaliert die *Spannungs*-Amplitude. Die abgegebene
// Leistung steigt quadratisch:  P_out ≈ gVolume² × P_amp_max.
// Daraus folgt: maximales sicheres gVolume = sqrt(P_speaker_max / P_amp_max).
//
// FR 7  → sqrt( 5 / 12) ≈ 0.65  (Limit greift hart, Vollgas zerstoert ihn)
// FR 10 → sqrt(30 / 12) ≈ 1.58  → auf 1.00 gedeckelt (Amp ist die Grenze)
struct SpeakerProfile {
    const char* name;
    float       maxRmsW;     // RMS-Belastbarkeit des Speakers
};

static const float AMP_MAX_RMS_W = 12.0f;   // TPA3110 @ 12 V / 4 Ω

static const SpeakerProfile SPEAKER_FR7  = { "Visaton FR 7/4",  5.0f  };
static const SpeakerProfile SPEAKER_FR10 = { "Visaton FR 10/4", 30.0f };

// ─── HIER aktiven Speaker waehlen ────────────────────────────────────────────
static const SpeakerProfile& SPEAKER = SPEAKER_FR7;
// =============================================================================

// Berechnet das maximale sichere gVolume aus den Speaker-Daten.
// Quadratwurzel sicherstellen, dass es nie ueber 1.0 geht.
static inline float computeMaxVolume(const SpeakerProfile& sp) {
    float ratio = sp.maxRmsW / AMP_MAX_RMS_W;
    if (ratio >= 1.0f) return 1.0f;        // Amp ist die Grenze
    return sqrtf(ratio);                    // Speaker ist die Grenze
}

// ── Lautstärke (zur Laufzeit über K1/K2 verstellbar) ─────────────────────────
// Skala intern 0.0 – VOLUME_MAX, Anzeige in %, Schrittweite 10 % der Skala.
static const float VOLUME_MIN  = 0.00f;
static       float VOLUME_MAX  = 0.65f;     // wird in setup() aus SPEAKER befuellt
static       float VOLUME_STEP = 0.10f;     // dito (10 % von VOLUME_MAX)
static       float gVolume     = 0.05f;     // Start: sehr leise

// ── Test-WAVs (Dateien liegen unter data/, kommen per uploadfs) ──────────────
// Statt zweier fester Konstanten gibt es jetzt eine Liste – K4 cycelt durch
// die Liste und spielt den jeweils ausgewaehlten Sound ab. Der Stab-Trigger
// (GPIO 4) feuert ebenfalls den aktuell ausgewaehlten Sound.
//
// shortName: <= 11 Zeichen → passt mit Index-Praefix in eine 128-px-Zeile
//            bei u8g2_font_6x10_tf (s.u. drawDisplay()).
struct SoundEntry {
    const char* path;
    const char* shortName;
};
static const SoundEntry SOUNDS[] = {
    { "/01_test.wav",             "Test"        },
    { "/02_door_bang.wav",        "Door Bang"   },
    { "/03_boo_and_laugh.wav",    "Boo+Laugh"   },
    { "/04_bubbles.wav",          "Bubbles"     },
    { "/05_cat_meow.wav",         "Cat Meow"    },
    { "/06_daemon_kinderliebe.wav", "Daemon"    },
    { "/07_gears.wav",            "Gears"       },
    { "/08_little_girl.wav",      "LittleGirl"  },
    { "/09_owl_hooting.wav",      "Owl"         },
    { "/10_psycho_sound.wav",     "Psycho"      },
    { "/11_scary_clock.wav",      "ScaryClock"  },
    { "/12_spooky_skeleton.wav",  "Skeleton"    },
    { "/13_werewolf.wav",         "Werewolf"    },
    { "/14_werewolf_growl.wav",   "WerewolfGr"  },
    { "/15_witch.wav",            "Witch"       },
};
static const size_t NUM_SOUNDS = sizeof(SOUNDS) / sizeof(SOUNDS[0]);

// Aktuell ausgewaehlter Sound. K4 cyclet diesen Wert (mod NUM_SOUNDS).
static size_t gSoundIdx = 0;

// Zur Lesbarkeit: aliasse fuer die "klassischen" Test-Sounds, falls aelterer
// Code-Pfad sie noch namentlich braucht. Aktuell genutzt: nur im Boot-Log.
static const char* TEST_WAV      = SOUNDS[0].path;   // /01_test.wav
static const char* DOOR_BANG_WAV = SOUNDS[1].path;   // /02_door_bang.wav

// ── WAV-Header (Standard RIFF, ohne Extra-Chunks) ────────────────────────────
struct WavHeader {
    char     riff[4];        // "RIFF"
    uint32_t fileSize;
    char     wave[4];        // "WAVE"
    char     fmt[4];         // "fmt "
    uint32_t fmtSize;
    uint16_t audioFormat;    // 1 = PCM
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
};

// ── Display ──────────────────────────────────────────────────────────────────
// SH1106 1,3", I²C HW (Hardware-I²C, Wire begint vorher mit SDA/SCL/Freq).
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, /*reset=*/U8X8_PIN_NONE);

// ── NeoPixel-Streifen (SK6812 RGBW, GRBW-Reihenfolge, 800 kHz Bus) ──────────
// SK6812 in der RGBW-Variante (mit eigenem Weiss-Kanal) → NEO_GRBW (4 Byte/Pixel).
Adafruit_NeoPixel strip(NEOPIXEL_COUNT, NEOPIXEL_PIN, NEO_GRBW + NEO_KHZ800);

// ── Button-Debouncing ────────────────────────────────────────────────────────
struct Button {
    uint8_t  pin;
    bool     stableState;     // gefilterter Pegel (true = gedrückt)
    bool     lastSample;      // letzter Roh-Lesewert
    uint32_t lastChangeMs;
};
// gBtns[0..3] = K1..K4 (am OLED-Modul), gBtns[4] = Stab-Trigger an GPIO 4
static Button gBtns[5] = {
    { BTN_K1,   false, false, 0 },
    { BTN_K2,   false, false, 0 },
    { BTN_K3,   false, false, 0 },
    { BTN_K4,   false, false, 0 },
    { TRIG_PIN, false, false, 0 }
};
static const uint32_t DEBOUNCE_MS = 20;

// Liefert true bei steigender Flanke (Drücken erkannt, sauber entprellt).
bool btnPressedEdge(Button& b) {
    bool     raw = (digitalRead(b.pin) == LOW);   // active LOW
    uint32_t now = millis();

    if (raw != b.lastSample) {
        b.lastSample   = raw;
        b.lastChangeMs = now;
    }
    if ((now - b.lastChangeMs) >= DEBOUNCE_MS && raw != b.stableState) {
        b.stableState = raw;
        if (raw) return true;   // Drücken-Edge
    }
    return false;
}

// ── Display-State / Diagnose-Modus ───────────────────────────────────────────
// Drei Zustaende fuer die Rauschen-Diagnose nach Doc 14 Abschnitt 2.1:
//   MUTE        – XSMT LOW, DAC stumm. Worst-Case-Quelle: was rauscht jetzt?
//                 Rauschen hoerbar → Quelle ist NACH dem DAC (TPA, Speaker, GND).
//   IDLE_STREAM – XSMT HIGH, I²S streamt Stille. DAC ist aktiv aber leer.
//                 Rauscht nur in diesem Modus, nicht in MUTE → Quelle ist
//                 der DAC selbst, seine Versorgung oder das I²S-Signal.
//   PLAYING     – normaler Sound-Betrieb (nur waehrend playWav).
enum PlayState : uint8_t { READY, IDLE_STREAM, PLAYING };
static PlayState  gPlayState   = READY;
static uint32_t   gLastPlayMs  = 0;
static uint32_t   gPlayCount   = 0;   // alle Sound-Wiedergaben (egal welche Quelle)
static uint32_t   gTrigCount   = 0;   // nur ueber den Stab-Trigger (GPIO 4)
static uint32_t   gIrCount     = 0;   // gesendete IR-Bursts
static bool       gLastIrOk    = false; // letzter Burst: TSOP hat empfangen?
static bool       gLaserOn     = false;

// ── Infinitag Now: persistente Config + ESP-NOW-Gerätelogik ─────────────────
static StationSettings gSettings;
static NowStation      gNow;

void drawDisplay() {
    // Display ist 128 x 64 Pixel.
    // U8g2 nutzt Baseline-Y; mit u8g2_font_6x10_tf hat eine Zeile rund 10 px
    // Hoehe (Glyph-Top etwa Baseline-7, Descender bis Baseline+2). Mit dem
    // groesseren u8g2_font_7x14B_tf sind es ca. 14 px (Top etwa Baseline-12).
    //
    // Y-Plan:
    //   y= 8  Header-Baseline   (Pixel  1.. 8)
    //   y=10  HLine 1
    //   y=26  "Bereit"-Baseline (7x14B, Pixel 14..26)
    //   y=38  Vol/Plays Baseline (Pixel 31..38)
    //   y=49  Speaker Baseline  (Pixel 42..49)
    //   y=52  HLine 2
    //   y=62  Footer Baseline   (Pixel 55..62)
    u8g2.clearBuffer();

    // Kopf: seit Protokoll v0x02 identifiziert das MAC-Suffix die Station
    // (keine anwender-vergebene ID mehr).
    u8g2.setFont(u8g2_font_6x10_tf);
    char hdr[24];
    const uint8_t* hm = gNow.ownMac();
    snprintf(hdr, sizeof(hdr), "Station V2   %02X%02X%02X", hm[3], hm[4], hm[5]);
    u8g2.drawStr(0, 8, hdr);
    u8g2.drawHLine(0, 10, 128);

    // Status groß. Priorität:
    //   1) Wenn gerade ein Sound spielt → "Playing..."
    //   2) Sonst wenn Diagnose-Mode (selten genutzt) → "DAC idle"
    //   3) Sonst Laser-Zustand: "Laser AN" oder "Bereit"
    u8g2.setFont(u8g2_font_7x14B_tf);
    const char* statusStr;
    if (gPlayState == PLAYING) {
        statusStr = "Playing...";
    } else if (gPlayState == IDLE_STREAM) {
        statusStr = "DAC idle";
    } else if (gLaserOn) {
        statusStr = "Laser AN";
    } else {
        statusStr = "Bereit";
    }
    u8g2.drawStr(0, 26, statusStr);

    // Lautstärke (relativ zum Speaker-Limit, also 0–100 % von VOLUME_MAX)
    // + Plays/Trigger/IR-Counter, kompakt in einer Zeile.
    u8g2.setFont(u8g2_font_6x10_tf);
    char buf[32];
    int volPct = VOLUME_MAX > 0.0f ? (int)((gVolume / VOLUME_MAX) * 100.0f + 0.5f) : 0;
    snprintf(buf, sizeof(buf), "V:%d%%", volPct);
    u8g2.drawStr(0, 38, buf);

    // Counter: P=Plays, T=Trigger, I=IR-Bursts. Hinter I noch ein
    // Mini-Status-Symbol fuer den letzten IR-Burst:
    //   "?" = noch nie gesendet
    //   "+" = letzter Burst, TSOP hat empfangen
    //   "-" = letzter Burst, TSOP hat NICHT empfangen
    const char* irMark = (gIrCount == 0) ? "?" : (gLastIrOk ? "+" : "-");
    snprintf(buf, sizeof(buf), "P:%lu T:%lu I:%lu%s",
        (unsigned long)gPlayCount, (unsigned long)gTrigCount,
        (unsigned long)gIrCount, irMark);
    u8g2.drawStr(38, 38, buf);

    // Statt Speaker-Profil hier jetzt der aktuell ausgewaehlte Sound
    // (1-basierter Index + Kurzname). Speaker-Name steht weiterhin im
    // Boot-Log – auf dem Display ist der Sound zur Laufzeit wichtiger.
    // Format: "S04:Bubbles" – bei 6x10 Font ca. 11 Zeichen, ~66 px
    // breit → laesst rechts genug Platz fuer die IR-Status-Box.
    snprintf(buf, sizeof(buf), "S%02u:%s",
        (unsigned)(gSoundIdx + 1), SOUNDS[gSoundIdx].shortName);
    u8g2.drawStr(0, 49, buf);
    if (gIrCount > 0) {
        // 9x9 Box rechts in der gleichen Zeile, Baseline 49
        const int bx = 116, by = 41, bs = 9;
        if (gLastIrOk) {
            u8g2.drawBox(bx, by, bs, bs);    // gefuellt = empfangen ✓
        } else {
            u8g2.drawFrame(bx, by, bs, bs);  // leer = NICHT empfangen
        }
    }

    // Footer / Bedienhinweis – kompakter Cheatsheet bei 6x10 Font.
    // K1V cyclet Volume, K2S cyclet Sound, K3L = Laser, K4P = vorhoeren.
    // 21 Zeichen passen knapp in 128 px (6 px Glyph × 21 = 126 px).
    u8g2.drawHLine(0, 52, 128);
    u8g2.drawStr(0, 62, "K1V+ K2S+ K3L K4Play");
    u8g2.sendBuffer();
}

// ── I²S initialisieren ───────────────────────────────────────────────────────
void i2s_setup() {
    i2s_config_t cfg = {
        .mode                 = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate          = SAMPLE_RATE,
        .bits_per_sample      = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format       = I2S_CHANNEL_FMT_ONLY_LEFT,   // Mono → linker Kanal
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags     = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count        = DMA_BUF_COUNT,
        .dma_buf_len          = DMA_BUF_LEN,
        .use_apll             = false,         // APLL vorerst aus – stabiler beim Start
        .tx_desc_auto_clear   = true           // DMA-Underrun → Stille statt Müll
    };

    i2s_pin_config_t pins = {
        .mck_io_num     = I2S_PIN_NO_CHANGE,  // PCM5102A braucht kein MCLK (SCK→GND)
        .bck_io_num     = I2S_BCLK,
        .ws_io_num      = I2S_LRC,
        .data_out_num   = I2S_DOUT,
        .data_in_num    = I2S_PIN_NO_CHANGE
    };

    Serial.println("[I2S] driver install...");
    esp_err_t err = i2s_driver_install(I2S_PORT, &cfg, 0, NULL);
    Serial.printf("[I2S] driver install: %s\n", esp_err_to_name(err));

    Serial.println("[I2S] set pin...");
    err = i2s_set_pin(I2S_PORT, &pins);
    Serial.printf("[I2S] set pin: %s\n", esp_err_to_name(err));

    i2s_zero_dma_buffer(I2S_PORT);
    Serial.println("[I2S] bereit");
}

// ── NeoPixel: Boot-Test ──────────────────────────────────────────────────────
// Faerbt alle LEDs nacheinander rot, gruen, blau, weiss (jeweils 400 ms).
// So sieht man bei der Inbetriebnahme sofort: gehen alle LEDs? alle Kanaele?
// SK6812 RGBW: "Weiss" ueber den eigenen W-Kanal (0,0,0,255), NICHT R+G+B –
// so wird auch der vierte Die einzeln geprueft.
void neopixelBootTest() {
    const uint32_t cols[4] = {
        strip.Color(255,   0,   0),        // R
        strip.Color(  0, 255,   0),        // G
        strip.Color(  0,   0, 255),        // B
        strip.Color(  0,   0,   0, 255)    // W ueber eigenen Weiss-Kanal
    };
    const char* names[4] = { "Rot", "Gruen", "Blau", "Weiss(W)" };

    for (int c = 0; c < 4; c++) {
        Serial.printf("[NEO] Boot-Test: %s\n", names[c]);
        for (int i = 0; i < NEOPIXEL_COUNT; i++) strip.setPixelColor(i, cols[c]);
        strip.show();
        delay(400);
    }
    strip.clear();
    strip.show();
}

// ── NeoPixel: alle LEDs auf eine feste Farbe setzen (color = 0 → aus) ───────
// Fuer den K3-Test: LEDs dauerhaft an/aus synchron zum Laser, ohne Animation.
void neopixelSetSolid(uint32_t color) {
    for (int i = 0; i < NEOPIXEL_COUNT; i++) strip.setPixelColor(i, color);
    strip.show();
}

// ── Status-LED: Kanal-Maske → NeoPixel-Farbe ────────────────────────────────
// LED-Maske aus dem Station-Config-Blob (PROTOCOL.md): bit0=R, bit1=G,
// bit2=B, bit3=W. Jeder gesetzte Die voll an – Helligkeit regelt global
// strip.setBrightness().
static uint32_t maskToColor(uint8_t mask) {
    return Adafruit_NeoPixel::Color(
        (mask & 0x01) ? 255 : 0,
        (mask & 0x02) ? 255 : 0,
        (mask & 0x04) ? 255 : 0,
        (mask & 0x08) ? 255 : 0);
}

// ── Status-LED: Stab-Farbe nach Prioritaet setzen ───────────────────────────
// Identify-Blink (weiss, 200 ms) > beschaeftigt (Audio spielt, Default rot)
// > schussbereit (Default gruen). Farben fuer bereit/beschaeftigt kommen
// aus der persistenten Config (ledReady/ledBusy).
// Schreibt nur bei Farbwechsel auf den Bus. Wird im loop() gerufen UND
// direkt vor/nach blockierenden playWav()-Aufrufen, weil loop() waehrend
// der Wiedergabe nicht laeuft.
static void updateStatusLed() {
    static bool     init = false;
    static uint32_t last = 0;

    uint32_t color;
    if (gNow.identifyActive()) {
        // Doc 18 §7: weisses schnelles Pulsen, selbstloeschend nach dem
        // 700-ms-Fenster der Config-Box.
        color = ((millis() / 200) % 2 == 0) ? strip.Color(0, 0, 0, 180) : 0;
    } else if (gPlayState == PLAYING) {
        color = maskToColor(gSettings.ledBusy);
    } else {
        color = maskToColor(gSettings.ledReady);
    }

    if (!init || color != last) {
        init = true;
        last = color;
        neopixelSetSolid(color);
    }
}

// ── NeoPixel: Farb-Palette-Cycle (im Loop aufrufen) ─────────────────────────
// Schaltet ALLE LEDs gemeinsam durch eine volle Farb-Palette (je 'holdMs'):
//   1) Grundfarben + Mischfarben mit W-Kanal AUS:
//      Rot → Rot/Gruen → Gruen → Gruen/Blau → Blau → Blau/Rot → Weiss(W)
//   2) danach dieselben Farben zusaetzlich mit dem W-Kanal AN.
// So prueft man bei der SK6812 RGBW jeden Die einzeln, alle Mischungen und
// das Zusammenspiel RGB + Weiss-Kanal. Farbwerte als Color(r,g,b,w).
void neopixelCyclePalette(uint32_t holdMs) {
    static const uint32_t cols[] = {
        // 1) RGB + Mischfarben, W-Kanal aus
        Adafruit_NeoPixel::Color(255,   0,   0,   0),   // Rot
        Adafruit_NeoPixel::Color(255, 255,   0,   0),   // Rot/Gruen (Gelb)
        Adafruit_NeoPixel::Color(  0, 255,   0,   0),   // Gruen
        Adafruit_NeoPixel::Color(  0, 255, 255,   0),   // Gruen/Blau (Cyan)
        Adafruit_NeoPixel::Color(  0,   0, 255,   0),   // Blau
        Adafruit_NeoPixel::Color(255,   0, 255,   0),   // Blau/Rot (Magenta)
        Adafruit_NeoPixel::Color(  0,   0,   0, 255),   // Weiss (nur W-Kanal)
        // 2) dieselben Mischfarben zusaetzlich mit W-Kanal an
        Adafruit_NeoPixel::Color(255,   0,   0, 255),   // Rot + W
        Adafruit_NeoPixel::Color(255, 255,   0, 255),   // Gelb + W
        Adafruit_NeoPixel::Color(  0, 255,   0, 255),   // Gruen + W
        Adafruit_NeoPixel::Color(  0, 255, 255, 255),   // Cyan + W
        Adafruit_NeoPixel::Color(  0,   0, 255, 255),   // Blau + W
        Adafruit_NeoPixel::Color(255,   0, 255, 255),   // Magenta + W
        Adafruit_NeoPixel::Color(255, 255, 255, 255)    // RGB + W (alles an)
    };
    static const uint8_t NUM_COLS = sizeof(cols) / sizeof(cols[0]);
    static uint8_t        idx     = 0;
    static uint32_t       lastMs  = 0;

    if (millis() - lastMs < holdMs) return;
    lastMs = millis();

    neopixelSetSolid(cols[idx]);
    idx = (idx + 1) % NUM_COLS;
}

// ── NeoPixel: Rainbow-Schritt (im Loop alle ~50 ms aufrufen) ────────────────
// Verschiebt einen Hue-Walk ueber alle LEDs, jede LED hat eine andere Farbe.
void neopixelStep() {
    static uint8_t  hueBase  = 0;
    static uint32_t lastMs   = 0;

    if (millis() - lastMs < 50) return;
    lastMs = millis();

    for (int i = 0; i < NEOPIXEL_COUNT; i++) {
        // Hue 0..65535 spreizen: jeder Pixel ein Stueck weiter im Spektrum,
        // damit man sieht ob jede einzelne LED korrekt adressiert wird.
        uint16_t hue = ((uint16_t)hueBase * 256) + (uint16_t)i * 8192;
        uint32_t col = strip.gamma32(strip.ColorHSV(hue, 255, 180));
        strip.setPixelColor(i, col);
    }
    strip.show();
    hueBase++;
}

// ── IR-Burst senden (Doc 14 Punkt 2.4) ───────────────────────────────────────
// Schaltet GPIO 5 für 'ms' Millisekunden auf 38 kHz PWM mit ~33 % Duty.
// Tastet währenddessen den TSOP-Pin, um lokal zu verifizieren dass der
// Burst sauber rauskommt. Output via Serial: "empfangen" oder "KEIN Empfang".
void sendIrBurst(uint32_t ms) {
    bool     tsopGotIt = false;
    uint32_t start     = millis();

    // Burst an
    ledcWrite(IR_LEDC_CHANNEL, IR_DUTY_8BIT);

    // Während des Bursts den TSOP samplen. Pollabstand 50 µs ist mehr als
    // genug, der TSOP entscheidet sich innerhalb < 1 ms.
    while ((millis() - start) < ms) {
        if (digitalRead(TSOP_PIN) == LOW) {
            tsopGotIt = true;
        }
        delayMicroseconds(50);
    }

    // Burst aus (Duty 0 = Pin LOW permanent)
    ledcWrite(IR_LEDC_CHANNEL, 0);

    gIrCount++;
    gLastIrOk = tsopGotIt;
    Serial.printf("[IR]  Burst #%lu, %lu ms gesendet. TSOP: %s\n",
        (unsigned long)gIrCount, ms,
        tsopGotIt ? "empfangen ✓" : "KEIN Empfang ✗");
}

// ── WAV abspielen ────────────────────────────────────────────────────────────
// Sucht im Header nach dem "data"-Chunk, um Extra-Chunks zu überspringen.
// Skaliert jedes Sample mit gVolume (Laufzeit-Lautstärke).
void playWav(const char* path) {
    File f = LittleFS.open(path, "r");
    if (!f) {
        Serial.printf("[WAV] Datei nicht gefunden: %s\n", path);
        return;
    }

    // RIFF/WAVE-Header lesen
    WavHeader hdr;
    f.read((uint8_t*)&hdr, sizeof(hdr));

    if (strncmp(hdr.riff, "RIFF", 4) != 0 || strncmp(hdr.wave, "WAVE", 4) != 0) {
        Serial.println("[WAV] Kein gueltiges WAV-Format!");
        f.close();
        return;
    }

    Serial.printf("[WAV] %s | %lu Hz | %u Kanal(e) | %u Bit\n",
        path, hdr.sampleRate, hdr.numChannels, hdr.bitsPerSample);

    // Zum "data"-Chunk vorspulen (überspringt ggf. LIST/INFO-Chunks)
    char     chunkId[4];
    uint32_t chunkSize = 0;
    bool     found = false;

    while (f.available()) {
        f.read((uint8_t*)chunkId, 4);
        f.read((uint8_t*)&chunkSize, 4);
        if (strncmp(chunkId, "data", 4) == 0) {
            found = true;
            break;
        }
        f.seek(chunkSize, SeekCur);   // Unbekannten Chunk überspringen
    }

    if (!found) {
        Serial.println("[WAV] Kein data-Chunk gefunden!");
        f.close();
        return;
    }

    Serial.printf("[WAV] Audio-Daten: %lu Bytes (~%.1f Sek.) @ Vol %.2f\n",
        chunkSize,
        (float)chunkSize / (hdr.sampleRate * hdr.numChannels * (hdr.bitsPerSample / 8)),
        gVolume);

    // ── Mute-Strategie Plan A: XSMT-Sequenz ──────────────────────────────────
    // I²S läuft bereits mit Stille → DAC eingerastet, kein Knack beim Einschalten
    delay(5);                          // Sicherheits-Pause (I²S-DMA sicher stabil)
    digitalWrite(XSMT_PIN, HIGH);      // DAC Soft-Fade-In (~2 ms)
    delay(10);                         // Fade-In abwarten, dann Audio

    // ── Audio-Daten in DMA-Buffer schreiben (mit Volume-Skalierung) ───────────
    const size_t BUF = 2048;
    uint8_t      buf[BUF];
    size_t       written = 0;

    while (f.available()) {
        int n = f.read(buf, BUF);
        if (n <= 0) break;

        // Jedes 16-Bit-Sample mit gVolume skalieren → Leistungsbegrenzung
        int16_t* samples = (int16_t*)buf;
        int      count   = n / 2;
        for (int i = 0; i < count; i++) {
            samples[i] = (int16_t)(samples[i] * gVolume);
        }

        i2s_write(I2S_PORT, buf, (size_t)n, &written, portMAX_DELAY);
    }

    f.close();

    // ── Sound-Ende: erst Stille, dann DAC stumm ───────────────────────────────
    i2s_zero_dma_buffer(I2S_PORT);    // I²S streamt jetzt Null-Samples
    delay(5);                          // Stille sicher im DMA-Buffer
    digitalWrite(XSMT_PIN, LOW);      // DAC Soft-Fade-Out aus Stille → lautlos
    Serial.println("[WAV] Fertig.");
}

// ── Infinitag Now: Helfer ────────────────────────────────────────────────────
// Spielt einen Sound per 0-basiertem Index (fuer ESP-NOW-Handler: Test-Sound,
// HIT_REPORT, Setup-Bestaetigung). Gleiches PlayState-Handling wie K4.
static void playSoundByIndex(uint8_t idx) {
    if (idx >= NUM_SOUNDS) return;
    PlayState prev = gPlayState;
    gPlayState = PLAYING;
    drawDisplay();
    updateStatusLed();          // busy-Farbe VOR dem blockierenden playWav
    playWav(SOUNDS[idx].path);
    gPlayCount++;
    gLastPlayMs = millis();
    gPlayState  = prev;
    if (prev == IDLE_STREAM) digitalWrite(XSMT_PIN, HIGH);
    updateStatusLed();          // zurueck auf bereit
    drawDisplay();
}

// Wendet die persistente Config auf die Laufzeit-Variablen an (Volume in %
// des Speaker-Limits). Wird nach Boot und nach jedem CFG_WRITE gerufen.
static void applyStationConfig() {
    gVolume = VOLUME_MAX * ((float)gSettings.volumePct / 100.0f);
}

// ── SoftAP-Firmware-Update (UPDATE_BEGIN via Config-Box) ────────────────────
// Blockierender Modus: ESP-NOW wird beendet, offener AP + Upload-Seite
// (geteiltes WebUpdateService-Modul aus infinitag-now-core). Endet IMMER
// in ESP.restart(): nach erfolgreichem Update in die neue Firmware, nach
// Timeout ohne Upload zurueck in die alte. Ein abgebrochener Upload kann
// nicht booten (Boot-Slot wechselt erst nach validiertem Empfang).
static void runUpdateMode(uint8_t minutes) {
    const uint8_t* m = gNow.ownMac();
    char ap[32];
    snprintf(ap, sizeof(ap), "infinitag-sta-%02X%02X%02X", m[3], m[4], m[5]);
    char ver[16];
    snprintf(ver, sizeof(ver), "%u.%u.%u", STATION_FW_MAJOR, STATION_FW_MINOR,
             STATION_FW_PATCH);

    // UPDATE_ACK ist bereits als ESP-NOW-Sendung eingereiht – kurz warten,
    // bevor der Funk-Stack abgebaut wird.
    delay(100);

    WebUpdateService upd;
    if (!upd.begin(ap, ver)) {
        Serial.println("[UPD] SoftAP-Start fehlgeschlagen -> Reboot");
        ESP.restart();
    }
    Serial.printf("[UPD] Update-Modus: AP %s, http://%s, %u min\n", ap,
                  upd.apIp(), minutes);

    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_7x14B_tf);
    u8g2.drawStr(0, 14, "UPDATE-MODUS");
    u8g2.setFont(u8g2_font_6x10_tf);
    u8g2.drawStr(0, 28, "WLAN:");
    u8g2.drawStr(36, 28, ap);
    u8g2.drawStr(0, 40, "http://192.168.4.1");
    char line[24];
    snprintf(line, sizeof(line), "FW %s  Timeout %umin", ver, minutes);
    u8g2.drawStr(0, 52, line);
    u8g2.sendBuffer();

    const uint32_t deadline = millis() + (uint32_t)minutes * 60000UL;
    bool ledOn = false;
    while (true) {
        upd.loop();

        // Stab pulsiert blau als "im Update-Modus"-Signal
        const bool on = (millis() / 300) % 2 == 0;
        if (on != ledOn) {
            ledOn = on;
            neopixelSetSolid(on ? strip.Color(0, 0, 200) : 0);
        }

        if (upd.updateDone()) {
            u8g2.setFont(u8g2_font_7x14B_tf);
            u8g2.drawStr(0, 63, "OK - Neustart...");
            u8g2.sendBuffer();
            delay(1500);  // Antwortseite noch ausliefern lassen
            ESP.restart();
        }
        if (millis() >= deadline && !upd.uploadActive()) {
            Serial.println("[UPD] Timeout ohne Upload -> Reboot");
            ESP.restart();
        }
        delay(5);
    }
}

// ── Selbsttest-Hooks (DEBUG_CMD via Config-Box, siehe NowStation.h) ─────────
static uint32_t gLaserTestOffMs = 0;   // Auto-Aus fuer den Laser-Testpuls

static void hookLedTest() { neopixelBootTest(); }

static void hookLaserPulse(uint8_t seconds) {
    gLaserOn = true;
    digitalWrite(LASER_PIN, HIGH);
    gLaserTestOffMs = millis() + (uint32_t)seconds * 1000UL;
}

static bool hookIrBurst(uint8_t ms) {
    sendIrBurst(ms);      // setzt gLastIrOk (TSOP-Selbstempfang)
    return gLastIrOk;
}

static const DebugHooks kDebugHooks = {hookLedTest, hookLaserPulse,
                                       hookIrBurst};

// ── Setup ────────────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);

    // Auf USB-CDC warten (macOS braucht nach Reset ~1-2s zum Re-Enumerieren).
    // Timeout nach 5s damit der ESP auch ohne Monitor startet.
    unsigned long t = millis();
    while (!Serial && millis() - t < 5000) delay(10);

    Serial.println("\n=== Infinitag Station V2 – Test-Sketch mit OLED + Tasten ===");

    // XSMT default LOW: DAC stumm → kein Power-On-Pop, auch wenn Amp schon aktiv
    pinMode(XSMT_PIN, OUTPUT);
    digitalWrite(XSMT_PIN, LOW);
    Serial.println("[XSMT] DAC stumm (Soft-Mute aktiv)");

    // Laser default LOW = aus (active-HIGH am BC546C-Basis)
    pinMode(LASER_PIN, OUTPUT);
    digitalWrite(LASER_PIN, LOW);
    Serial.println("[LASER] aus");

    // IR-LED: ledc-Channel auf 38 kHz, 8 bit Resolution, dann an GPIO 5 binden.
    // Default 0 % Duty = Pin LOW = BC546C sperrt = LED aus.
    // (Arduino-ESP32 v2.x API; v3.x: ledcAttach(pin, freq, resolution).)
    double freqActual = ledcSetup(IR_LEDC_CHANNEL, IR_FREQ_HZ, 8);
    ledcAttachPin(IR_PIN, IR_LEDC_CHANNEL);
    ledcWrite(IR_LEDC_CHANNEL, 0);
    Serial.printf("[IR]   ledc CH%d → GPIO%d @ %.0f Hz (Soll %d Hz), 8 bit\n",
        IR_LEDC_CHANNEL, IR_PIN, freqActual, IR_FREQ_HZ);

    // TSOP4838 OUT als Eingang, mit internem Pullup (TSOP hat auch eigenen).
    pinMode(TSOP_PIN, INPUT_PULLUP);
    Serial.printf("[TSOP] GPIO%d als Input mit Pullup\n", TSOP_PIN);

    // NeoPixel-Streifen initialisieren und Boot-Test fahren.
    strip.begin();
    strip.setBrightness(NEOPIXEL_BRIGHT);
    strip.clear();
    strip.show();
    Serial.printf("[NEO]  %d x SK6812 RGB auf GPIO%d, Brightness %d/255\n",
        NEOPIXEL_COUNT, NEOPIXEL_PIN, NEOPIXEL_BRIGHT);
    neopixelBootTest();
    // Danach zeigt der Stab die Statusfarbe (bereit/beschaeftigt), gesetzt
    // von updateStatusLed() – erster Aufruf am Ende von setup(), sobald die
    // persistente Config (ledReady/ledBusy) geladen ist.

    // Buttons + Trigger mit internen Pullups
    for (uint8_t i = 0; i < 5; i++) {
        pinMode(gBtns[i].pin, INPUT_PULLUP);
    }
    Serial.println("[BTN]  K1..K4 + Trigger (GPIO4) mit Pullup gesetzt");

    // Speaker-Profil auswerten → max. zulaessige Lautstaerke berechnen
    VOLUME_MAX  = computeMaxVolume(SPEAKER);
    VOLUME_STEP = VOLUME_MAX * 0.10f;       // 10 %-Schritte der erlaubten Skala
    // Boot-Default: 50 % vom Speaker-Limit – laut genug um im 12-V-Modus
    // ohne Serial-Log was zu hoeren, sicher genug fuer den FR 7 (1,3 W).
    gVolume = VOLUME_MAX * 0.5f;
    Serial.printf("[SPK]  %s, %.0f W RMS @ Amp %.0f W -> Vol-Max %.2f, Step %.2f, Start %.2f\n",
        SPEAKER.name, SPEAKER.maxRmsW, AMP_MAX_RMS_W, VOLUME_MAX, VOLUME_STEP, gVolume);

    // I²C für das OLED-Modul.
    // Bus auf 100 kHz – stabiler ueber Breadboard-Drahtkabel als 400 kHz
    // (kuerzere Kabel + sauberes PCB spaeter wieder auf 400 kHz hochsetzen).
    Wire.begin(OLED_SDA, OLED_SCL, 100000);
    Serial.printf("[I2C]  SDA=GPIO%d  SCL=GPIO%d  @ 100 kHz\n", OLED_SDA, OLED_SCL);

    // OLED starten
    if (!u8g2.begin()) {
        Serial.println("[OLED] u8g2.begin() FEHLER – pruefe Verkabelung / Adresse");
    } else {
        Serial.println("[OLED] u8g2 bereit");
    }

    // SH1106-Spaltenoffset-Korrektur:
    // SH1106 hat 132 Spalten Adressraum, sichtbar 128. NONAME-Treiber setzt
    // x_offset = 2 (Annahme: Sichtfenster = Adressen 2..129). Manche Module
    // (z. B. DST-015-0 Aliexpress) zeigen aber Adressen 0..127 → dann sind
    // links 2 Spalten Muell und rechts werden 2 Spalten abgeschnitten.
    // Falls nach dem Reflash der Muell auf die rechte Seite wandert, einfach
    // wieder auf 2 zuruecksetzen. Auf einem PCB-Modul ggf. anderen U8g2-
    // Konstruktor probieren (_WINSTAR_, _VCOMH0_).
    u8g2.getU8x8()->x_offset = 0;

    u8g2.setBusClock(100000);   // U8g2 nutzt teilweise eigenen Bus-Speed
    u8g2.setContrast(255);
    u8g2.clearDisplay();        // GDRAM komplett loeschen → keine Boot-Muellreste links

    // I²S starten (dauerhafter Stream, niemals stoppen)
    i2s_setup();

    // LittleFS mounten + Inhalt loggen
    if (!LittleFS.begin(true)) {
        Serial.println("[FS]   FEHLER: LittleFS nicht mountbar!");
    } else {
        Serial.println("[FS]   LittleFS gemountet");
        File root = LittleFS.open("/");
        File entry = root.openNextFile();
        bool hasFiles = false;
        while (entry) {
            Serial.printf("[FS]     %s  (%lu Bytes)\n", entry.name(), entry.size());
            entry = root.openNextFile();
            hasFiles = true;
        }
        if (!hasFiles) {
            Serial.println("[FS]     (keine Dateien – 'pio run -t uploadfs' ausfuehren!)");
        }
    }

    // Sound-Bibliothek mitloggen (zusaetzlich zum FS-Listing oben → so sieht
    // man sofort welche Indices fuer K4-Cycle / Trigger zur Verfuegung stehen).
    Serial.printf("[SND]  %u Sounds in Liste:\n", (unsigned)NUM_SOUNDS);
    for (size_t i = 0; i < NUM_SOUNDS; ++i) {
        Serial.printf("[SND]    [%2u] %-12s  %s\n",
            (unsigned)(i + 1), SOUNDS[i].shortName, SOUNDS[i].path);
    }
    Serial.printf("[SND]  Aktuell ausgewaehlt: [%u] %s\n",
        (unsigned)(gSoundIdx + 1), SOUNDS[gSoundIdx].shortName);

    // Infinitag Now: persistente Config laden, anwenden, Funk starten.
    // WICHTIG: nach LittleFS/I2S, damit ein frueh eintreffendes CFG/Test-
    // Paket bereits abspielen kann.
    gSettings.load();
    applyStationConfig();
    if (gNow.begin(&gSettings, playSoundByIndex, (uint8_t)NUM_SOUNDS,
                   applyStationConfig, &kDebugHooks)) {
        const uint8_t* m = gNow.ownMac();
        Serial.printf("[NOW]  ESP-NOW bereit, MAC %02X:%02X:%02X:%02X:%02X:%02X\n",
            m[0], m[1], m[2], m[3], m[4], m[5]);
    } else {
        Serial.println("[NOW]  FEHLER: ESP-NOW-Init fehlgeschlagen!");
    }

    // Erster Display-Refresh + Stab auf Statusfarbe (bereit)
    drawDisplay();
    updateStatusLed();
    Serial.println("[Setup] Bereit. K1 = Volume cyclen, K2 = Sound cyclen,");
    Serial.println("        K3 = Laser, K4 = aktuellen Sound vorhoeren,");
    Serial.println("        Trigger (GPIO4) = IR-Burst + aktuellen Sound abspielen.");
}

// ── Loop ─────────────────────────────────────────────────────────────────────
void loop() {
    bool needRedraw = false;

    // Infinitag Now: RX-Queue abarbeiten (Discovery/Identify/CFG/Hit/Update)
    gNow.loop();
    if (gNow.consumeDirty()) needRedraw = true;

    // UPDATE_BEGIN empfangen? -> blockierender SoftAP-Update-Modus,
    // endet immer in ESP.restart().
    const uint8_t updMin = gNow.consumeUpdateRequest();
    if (updMin != 0) runUpdateMode(updMin);

    // Selbsttest: Laser-Testpuls automatisch beenden
    if (gLaserTestOffMs != 0 && millis() >= gLaserTestOffMs) {
        gLaserTestOffMs = 0;
        gLaserOn = false;
        digitalWrite(LASER_PIN, LOW);
        needRedraw = true;
    }

    // Debug: alle 2 s Roh-Pegel der vier Buttons loggen.
    // Erwartung im Ruhezustand: alle = 1 (Pullup hoch, Taste nicht gedrueckt).
    // Wenn K1/K2/K3 zufaellig auf 0 flackern → GPIO35/36/37-Konflikt
    // mit dem Octal PSRAM auf dem N16R8-Board.
    static uint32_t lastBtnLogMs = 0;
    if (millis() - lastBtnLogMs > 10000) {
        lastBtnLogMs = millis();
        Serial.printf("[BTN-RAW] K1=%d K2=%d K3=%d K4=%d TRIG=%d TSOP=%d\n",
            digitalRead(BTN_K1), digitalRead(BTN_K2),
            digitalRead(BTN_K3), digitalRead(BTN_K4),
            digitalRead(TRIG_PIN), digitalRead(TSOP_PIN));
    }

    // K1: Volume cyclen (+VOLUME_STEP, am Maximum zurueck zu VOLUME_MIN).
    // Wir cyclen mit etwas Toleranz, sonst rechnet Float-Addition vorbei.
    if (btnPressedEdge(gBtns[0])) {
        gVolume += VOLUME_STEP;
        if (gVolume > VOLUME_MAX + 0.001f) gVolume = VOLUME_MIN;
        Serial.printf("[K1] Volume -> %.2f (%d %% des Speaker-Limits)\n",
            gVolume, VOLUME_MAX > 0.0f
                ? (int)((gVolume / VOLUME_MAX) * 100.0f + 0.5f) : 0);
        needRedraw = true;
    }

    // K2: Sound cyclen (nur Auswahl, KEIN Abspielen).
    // Testablauf: K2 bis zum gewuenschten Sound drücken, dann Trigger feuern.
    // Wenn man nur Sound + Volume ohne Stab testen will: K4.
    if (btnPressedEdge(gBtns[1])) {
        gSoundIdx = (gSoundIdx + 1) % NUM_SOUNDS;
        Serial.printf("[K2] Sound -> [%u/%u] %s (%s)\n",
            (unsigned)(gSoundIdx + 1), (unsigned)NUM_SOUNDS,
            SOUNDS[gSoundIdx].shortName, SOUNDS[gSoundIdx].path);
        needRedraw = true;
    }

    // K3: Laser-Toggle (Doc 14 Punkt 2.5) + IR-LED synchron dauer-an/aus.
    // Active-HIGH am BC546C-Basis → Transistor leitet → Laser an.
    // Zum Testen der IR-LED: der IR-Treiber laeuft dauerhaft auf 38 kHz PWM
    // (nicht nur ein kurzer Burst), damit man dauerhaft sieht ob die IR-LED
    // sendet – am [BTN-RAW]-Log (TSOP=0 = empfaengt) bzw. per Handy-Kamera.
    // Mit K3 wieder aus. (Die Display-Box I:..+ zaehlt nur Trigger-Bursts.)
    if (btnPressedEdge(gBtns[2])) {
        gLaserOn = !gLaserOn;
        digitalWrite(LASER_PIN, gLaserOn ? HIGH : LOW);
        ledcWrite(IR_LEDC_CHANNEL, gLaserOn ? IR_DUTY_8BIT : 0);
        Serial.printf("[K3] Laser + IR-LED %s\n", gLaserOn ? "ON" : "OFF");
        needRedraw = true;
    }

    /* Frühere Belegungen von K3 zur Referenz:
       - Diagnose-Mode-Toggle (XSMT mute ↔ idle) für Rauschen-Test 2.1
         → Punkt 2.1 ist abgehakt, kann bei Bedarf reaktiviert werden.
       - Door-Bang-Sound (DOOR_BANG_WAV) bevor 2.5 dazu kam.
         → Nach Abschluss der Laser/IR-Diagnose ggf. wieder einsetzen
         oder dem Trigger zuweisen.
    */

    // K4: aktuellen Sound mit aktueller Lautstaerke abspielen – OHNE
    // Index-Wechsel, OHNE IR-Burst. Reiner Audio-Vorhoer-Knopf, damit man
    // auch ohne Stab den Klang gegen-pruefen kann.
    if (btnPressedEdge(gBtns[3])) {
        Serial.printf("[K4] Vorhoeren: %s @ Vol %.2f\n",
            SOUNDS[gSoundIdx].shortName, gVolume);
        PlayState prev = gPlayState;
        gPlayState = PLAYING;
        drawDisplay();          // Status sofort sichtbar
        updateStatusLed();      // busy-Farbe VOR dem blockierenden playWav

        playWav(SOUNDS[gSoundIdx].path);

        gPlayCount++;
        gLastPlayMs = millis();
        gPlayState  = prev;
        if (prev == IDLE_STREAM) {
            digitalWrite(XSMT_PIN, HIGH);
        }
        updateStatusLed();
        needRedraw  = true;
    }

    // Trigger (GPIO 4): Stab-Trigger → wie K4 (IR + Sound), aber
    //   - er WECHSELT den Sound NICHT (gSoundIdx bleibt),
    //   - er nutzt einen separaten Counter (gTrigCount),
    //   - damit der `[TRIG]`-Tag im Log den Weg ueber den Trigger-Pfad
    //     belegt (und nicht ueber K4-Druecken).
    // So kann man mit K4 den Sound waehlen und dann mit dem Stab beliebig
    // oft "feuern". Frueher fix DOOR_BANG_WAV, jetzt der ausgewaehlte Sound.
    if (btnPressedEdge(gBtns[4])) {
      if (gNow.consumeTriggerTest()) {
        // Selbsttest: Trigger-Test bestanden (Ergebnis geht per Funk an
        // die Config-Box) – kein Schuss ausloesen.
        needRedraw = true;
      } else {
        Serial.printf("[TRIG] Trigger ausgeloest (Trig #%lu) – IR + Sound %s\n",
            (unsigned long)(gTrigCount + 1), SOUNDS[gSoundIdx].shortName);
        PlayState prev = gPlayState;
        gPlayState = PLAYING;
        drawDisplay();
        updateStatusLed();      // busy-Farbe VOR dem blockierenden playWav

        sendIrBurst(1);
        playWav(SOUNDS[gSoundIdx].path);

        gPlayCount++;
        gTrigCount++;
        gLastPlayMs = millis();
        gPlayState  = prev;
        if (prev == IDLE_STREAM) {
            digitalWrite(XSMT_PIN, HIGH);
        }
        updateStatusLed();
        needRedraw = true;
      }
    }

    if (needRedraw) drawDisplay();

    // Stab-Statusfarbe (Prioritaet siehe updateStatusLed):
    // Identify-Blink > Setup-Lila > busy (Audio) > bereit.
    updateStatusLed();

    delay(5);   // kurze Pause → schont CPU, Debouncer arbeitet stabil
}
