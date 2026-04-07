#include "events.h"
#include "logger.h"
#include <SD.h>
#include <ArduinoJson.h>
#include <time.h>

static Event _events[MAX_EVENTS];
static int   _eventCount = 0;

static void eventsLoad() {
    _eventCount = 0;
    if (!SD.exists("/events.json")) return;

    File f = SD.open("/events.json", FILE_READ);
    if (!f) return;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        LOG_W("events", "JSON erro: %s", err.c_str());
        return;
    }

    JsonArray arr = doc.as<JsonArray>();
    for (JsonObject obj : arr) {
        if (_eventCount >= MAX_EVENTS) break;
        Event& e = _events[_eventCount];
        strlcpy(e.name, obj["n"] | "?", sizeof(e.name));
        e.month  = obj["m"] | 1;
        e.day    = obj["d"] | 1;
        e.hour   = obj["h"] | 0;
        e.minute = obj["i"] | 0;
        e.active = true;
        _eventCount++;
    }
    LOG_I("events", "Carregados %d eventos", _eventCount);
}

void eventsInit() {
    eventsLoad();
}

int eventsCount() {
    return _eventCount;
}

const Event* eventsGet(int index) {
    if (index < 0 || index >= _eventCount) return nullptr;
    return &_events[index];
}

bool eventsAdd(const char* name, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute) {
    if (_eventCount >= MAX_EVENTS) return false;
    Event& e = _events[_eventCount];
    strlcpy(e.name, name, sizeof(e.name));
    e.month  = month;
    e.day    = day;
    e.hour   = hour;
    e.minute = minute;
    e.active = true;
    _eventCount++;
    eventsSave();
    LOG_I("events", "Adicionado: %s %02d/%02d %02d:%02d", name, day, month, hour, minute);
    return true;
}

bool eventsRemove(int index) {
    if (index < 0 || index >= _eventCount) return false;
    LOG_I("events", "Removido: %s", _events[index].name);
    for (int i = index; i < _eventCount - 1; i++)
        _events[i] = _events[i + 1];
    _eventCount--;
    eventsSave();
    return true;
}

void eventsSave() {
    File f = SD.open("/events.json", FILE_WRITE);
    if (!f) {
        LOG_E("events", "Falha ao salvar eventos");
        return;
    }
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (int i = 0; i < _eventCount; i++) {
        JsonObject obj = arr.add<JsonObject>();
        obj["n"] = _events[i].name;
        obj["m"] = _events[i].month;
        obj["d"] = _events[i].day;
        obj["h"] = _events[i].hour;
        obj["i"] = _events[i].minute;
    }
    serializeJson(doc, f);
    f.close();
}

bool eventsGetNext(Event& out) {
    struct tm now;
    if (!getLocalTime(&now, 0)) return false;

    int nowMonth = now.tm_mon + 1;
    int nowDay   = now.tm_mday;
    int nowHour  = now.tm_hour;
    int nowMin   = now.tm_min;
    int nowVal   = nowMonth * 100000 + nowDay * 1000 + nowHour * 60 + nowMin;

    int bestIdx = -1;
    int bestVal = 999999999;

    for (int i = 0; i < _eventCount; i++) {
        if (!_events[i].active) continue;
        int val = _events[i].month * 100000 + _events[i].day * 1000 +
                  _events[i].hour * 60 + _events[i].minute;
        // Próximo evento futuro (ou hoje, ainda não passou)
        if (val >= nowVal && val < bestVal) {
            bestVal = val;
            bestIdx = i;
        }
    }
    // Se nenhum futuro, pega o primeiro do próximo ciclo (wrap-around)
    if (bestIdx < 0 && _eventCount > 0) {
        bestVal = 999999999;
        for (int i = 0; i < _eventCount; i++) {
            if (!_events[i].active) continue;
            int val = _events[i].month * 100000 + _events[i].day * 1000 +
                      _events[i].hour * 60 + _events[i].minute;
            if (val < bestVal) { bestVal = val; bestIdx = i; }
        }
    }
    if (bestIdx < 0) return false;
    out = _events[bestIdx];
    return true;
}
