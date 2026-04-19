#include "shopping_list.h"
#include "logger.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <time.h>

static ShoppingItem _items[SHOPPING_MAX];
static int          _count = 0;

static void loadFromNvs();
static void saveToNvs();

static bool strEqCI(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
        a++; b++;
    }
    return *a == *b;
}

void shoppingInit() {
    memset(_items, 0, sizeof(_items));
    _count = 0;
    loadFromNvs();
    LOG_I("shop", "Lista carregada — %d itens", _count);
}

bool shoppingAdd(const char* name) {
    if (!name || !name[0]) return false;
    if (_count >= SHOPPING_MAX) {
        LOG_W("shop", "Lista cheia (%d)", SHOPPING_MAX);
        return false;
    }
    for (int i = 0; i < _count; i++) {
        if (strEqCI(_items[i].name, name)) {
            // Duplicado: se estava marcado como feito, desmarca (traz de volta)
            if (_items[i].done) {
                _items[i].done = false;
                saveToNvs();
            }
            return false;
        }
    }
    strlcpy(_items[_count].name, name, SHOPPING_NAME_LEN);
    _items[_count].done    = false;
    time_t now = time(nullptr);
    _items[_count].addedAt = (now > 1577836800L) ? (uint32_t)now : 0;
    _count++;
    saveToNvs();
    LOG_I("shop", "Adicionado: %s (total=%d)", name, _count);
    return true;
}

bool shoppingToggle(int index) {
    if (index < 0 || index >= _count) return false;
    _items[index].done = !_items[index].done;
    saveToNvs();
    return true;
}

bool shoppingRemove(int index) {
    if (index < 0 || index >= _count) return false;
    for (int i = index; i < _count - 1; i++) _items[i] = _items[i + 1];
    _count--;
    memset(&_items[_count], 0, sizeof(ShoppingItem));
    saveToNvs();
    return true;
}

void shoppingClear() {
    memset(_items, 0, sizeof(_items));
    _count = 0;
    saveToNvs();
    LOG_I("shop", "Lista limpa");
}

void shoppingClearDone() {
    int w = 0;
    for (int r = 0; r < _count; r++) {
        if (!_items[r].done) {
            if (w != r) _items[w] = _items[r];
            w++;
        }
    }
    for (int i = w; i < _count; i++) memset(&_items[i], 0, sizeof(ShoppingItem));
    _count = w;
    saveToNvs();
    LOG_I("shop", "Marcados removidos — restam %d", _count);
}

int shoppingCount()        { return _count; }
int shoppingPendingCount() {
    int n = 0;
    for (int i = 0; i < _count; i++) if (!_items[i].done) n++;
    return n;
}

const ShoppingItem* shoppingGetAt(int index) {
    if (index < 0 || index >= _count) return nullptr;
    return &_items[index];
}

size_t shoppingToJson(char* out, size_t outSize) {
    JsonDocument doc;
    JsonArray arr = doc["items"].to<JsonArray>();
    for (int i = 0; i < _count; i++) {
        JsonObject o = arr.add<JsonObject>();
        o["name"]  = _items[i].name;
        o["done"]  = _items[i].done;
        o["added"] = _items[i].addedAt;
    }
    doc["count"]   = _count;
    doc["pending"] = shoppingPendingCount();
    return serializeJson(doc, out, outSize);
}

bool shoppingApplyJson(const char* json) {
    if (!json || !json[0]) return false;
    JsonDocument doc;
    if (deserializeJson(doc, json)) return false;

    // Array puro substitui a lista inteira
    if (doc.is<JsonArray>()) {
        JsonArray arr = doc.as<JsonArray>();
        memset(_items, 0, sizeof(_items));
        _count = 0;
        for (JsonObject it : arr) {
            if (_count >= SHOPPING_MAX) break;
            const char* name = it["name"] | "";
            if (!name[0]) continue;
            strlcpy(_items[_count].name, name, SHOPPING_NAME_LEN);
            _items[_count].done    = it["done"] | false;
            _items[_count].addedAt = it["added"] | 0;
            _count++;
        }
        saveToNvs();
        LOG_I("shop", "Lista substituida — %d itens", _count);
        return true;
    }

    const char* action = doc["action"] | "";
    if (strcmp(action, "add") == 0) {
        const char* name = doc["name"] | "";
        return shoppingAdd(name);
    }
    if (strcmp(action, "toggle") == 0) {
        int idx = doc["index"] | -1;
        if (idx < 0) {
            const char* name = doc["name"] | "";
            for (int i = 0; i < _count; i++) {
                if (strEqCI(_items[i].name, name)) { idx = i; break; }
            }
        }
        return shoppingToggle(idx);
    }
    if (strcmp(action, "remove") == 0) {
        int idx = doc["index"] | -1;
        return shoppingRemove(idx);
    }
    if (strcmp(action, "clear") == 0)      { shoppingClear();     return true; }
    if (strcmp(action, "clear_done") == 0) { shoppingClearDone(); return true; }
    return false;
}

// ── NVS ──────────────────────────────────────────────────────────────────────
// Persistimos a lista como JSON compacto em uma chave unica. Simples e sobrevive
// aos reboots (deep sleep nao preserva variaveis nao-RTC). 20 itens * ~50 bytes
// = ~1KB, bem dentro do limite de NVS (~4KB por chave).
static Preferences _prefs;

static void saveToNvs() {
    _prefs.begin("shop", false);
    char buf[1536];
    size_t n = shoppingToJson(buf, sizeof(buf));
    if (n > 0 && n < sizeof(buf)) {
        _prefs.putString("list", buf);
    }
    _prefs.end();
}

static void loadFromNvs() {
    _prefs.begin("shop", true);
    String raw = _prefs.getString("list", "");
    _prefs.end();
    if (raw.length() == 0) return;

    JsonDocument doc;
    if (deserializeJson(doc, raw)) return;
    JsonArray arr = doc["items"].as<JsonArray>();
    for (JsonObject it : arr) {
        if (_count >= SHOPPING_MAX) break;
        const char* name = it["name"] | "";
        if (!name[0]) continue;
        strlcpy(_items[_count].name, name, SHOPPING_NAME_LEN);
        _items[_count].done    = it["done"] | false;
        _items[_count].addedAt = it["added"] | 0;
        _count++;
    }
}
