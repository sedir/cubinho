#include "recipes.h"
#include "logger.h"
#include <Preferences.h>
#include <ArduinoJson.h>

static Recipe _recipes[RECIPES_COUNT];
static Preferences _prefs;

static const Recipe DEFAULTS[RECIPES_COUNT] = {
    { "Forno",  30 },
    { "Massa",   9 },
    { "Cafe",    4 },
    { "Cha",     5 },
    { "Sopa",   15 },
    { "Arroz",  18 },
    { "Bolo",   35 },
    { "Ovo",     6 },
    { "Frango", 25 },
    { "Livre",   5 },
};

static void applyDefaults() {
    for (int i = 0; i < RECIPES_COUNT; i++) _recipes[i] = DEFAULTS[i];
}

static void saveNvs();

void recipesInit() {
    applyDefaults();
    _prefs.begin("rcp", true);
    String raw = _prefs.getString("all", "");
    _prefs.end();
    if (raw.length()) recipesApplyJson(raw.c_str());
    LOG_I("rcp", "Receitas carregadas (%d)", RECIPES_COUNT);
}

int recipesCount() { return RECIPES_COUNT; }

const Recipe* recipesGetAt(int index) {
    if (index < 0 || index >= RECIPES_COUNT) return nullptr;
    return &_recipes[index];
}

bool recipesSetAt(int index, const char* name, int minutes) {
    if (index < 0 || index >= RECIPES_COUNT) return false;
    if (!name || !name[0]) return false;
    if (minutes < 1) minutes = 1;
    if (minutes > 99) minutes = 99;
    strlcpy(_recipes[index].name, name, RECIPE_NAME_LEN);
    _recipes[index].minutes = minutes;
    saveNvs();
    return true;
}

bool recipesApplyJson(const char* json) {
    if (!json || !json[0]) return false;
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;
    JsonArray arr = doc.as<JsonArray>();
    if (arr.isNull()) return false;
    int i = 0;
    for (JsonObject it : arr) {
        if (i >= RECIPES_COUNT) break;
        const char* nm = it["name"] | "";
        int mn         = it["minutes"] | _recipes[i].minutes;
        if (nm[0]) {
            strlcpy(_recipes[i].name, nm, RECIPE_NAME_LEN);
        }
        if (mn < 1)  mn = 1;
        if (mn > 99) mn = 99;
        _recipes[i].minutes = mn;
        i++;
    }
    saveNvs();
    LOG_I("rcp", "Receitas atualizadas — %d entradas", i);
    return true;
}

size_t recipesToJson(char* out, size_t outSize) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < RECIPES_COUNT; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["name"]    = _recipes[i].name;
        o["minutes"] = _recipes[i].minutes;
    }
    return serializeJson(doc, out, outSize);
}

void recipesResetDefaults() {
    applyDefaults();
    saveNvs();
    LOG_I("rcp", "Receitas resetadas");
}

static void saveNvs() {
    char buf[768];
    size_t n = recipesToJson(buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return;
    _prefs.begin("rcp", false);
    _prefs.putString("all", buf);
    _prefs.end();
}
