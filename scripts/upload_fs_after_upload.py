"""
Nach jedem normalen Upload (Firmware) wird automatisch auch
das LittleFS-Dateisystem (WAV-Dateien) mit auf den ESP geflasht.
Wird ausgelöst wenn man in PlatformIO auf 'Upload' drückt.
"""
Import("env")

env.AddPostAction(
    "upload",
    env.VerboseAction(
        "$PYTHONEXE -m platformio run -t uploadfs",
        ">>> Lade LittleFS-Dateisystem (WAV-Dateien)..."
    )
)
