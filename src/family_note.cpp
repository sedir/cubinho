#include "family_note.h"
#include "logger.h"
#include <Preferences.h>
#include <time.h>

static char     _note[NOTE_TEXT_LEN] = "";
static uint32_t _noteTs = 0;

static Preferences _prefs;

void familyNoteInit() {
    _prefs.begin("note", true);
    String saved = _prefs.getString("text", "");
    _noteTs      = _prefs.getUInt("ts", 0);
    _prefs.end();
    strlcpy(_note, saved.c_str(), sizeof(_note));
    LOG_I("note", "Recado carregado (%d chars)", (int)strlen(_note));
}

void familyNoteSet(const char* text) {
    if (!text) text = "";
    strlcpy(_note, text, sizeof(_note));
    // Trim simples de espacos/nl no final
    for (int i = (int)strlen(_note) - 1; i >= 0; i--) {
        if (_note[i] == ' ' || _note[i] == '\t' ||
            _note[i] == '\r' || _note[i] == '\n') _note[i] = '\0';
        else break;
    }
    time_t now = time(nullptr);
    _noteTs = (now > 1577836800L) ? (uint32_t)now : 0;

    _prefs.begin("note", false);
    _prefs.putString("text", _note);
    _prefs.putUInt("ts",     _noteTs);
    _prefs.end();
    LOG_I("note", "Recado: \"%s\"", _note);
}

bool familyNoteHas() { return _note[0] != '\0'; }

void familyNoteGet(char* out, size_t outSize) {
    if (!out || outSize == 0) return;
    strlcpy(out, _note, outSize);
}

uint32_t familyNoteTimestamp() { return _noteTs; }

void familyNoteClear() { familyNoteSet(""); }
