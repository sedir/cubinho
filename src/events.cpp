#include "events.h"
#include "logger.h"
#include <SD.h>
#include <ArduinoJson.h>
#include <time.h>

static Event _events[MAX_EVENTS];
static int   _eventCount = 0;

static bool eventToTimestamp(const Event& e, int year, time_t& outTs) {
    struct tm candidate = {};
    candidate.tm_year = year - 1900;
    candidate.tm_mon  = e.month - 1;
    candidate.tm_mday = e.day;
    candidate.tm_hour = e.hour;
    candidate.tm_min  = e.minute;
    candidate.tm_sec  = 0;
    candidate.tm_isdst = -1;

    time_t ts = mktime(&candidate);
    if (ts < 0) return false;

    struct tm check = {};
    if (!localtime_r(&ts, &check)) return false;
    if (check.tm_mon  != candidate.tm_mon  ||
        check.tm_mday != candidate.tm_mday ||
        check.tm_hour != candidate.tm_hour ||
        check.tm_min  != candidate.tm_min) {
        return false;
    }

    outTs = ts;
    return true;
}

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

bool eventsGetNextOccurrence(Event& out, time_t& outTs) {
    struct tm now;
    if (!getLocalTime(&now, 0)) return false;
    time_t nowTs = time(nullptr);
    if (nowTs <= 0) return false;

    int bestIdx = -1;
    time_t bestTs = 0;
    int currentYear = now.tm_year + 1900;

    for (int i = 0; i < _eventCount; i++) {
        if (!_events[i].active) continue;
        time_t candidateTs = 0;
        if (!eventToTimestamp(_events[i], currentYear, candidateTs)) continue;
        if (candidateTs < nowTs && !eventToTimestamp(_events[i], currentYear + 1, candidateTs)) {
            continue;
        }
        if (bestIdx < 0 || candidateTs < bestTs) {
            bestIdx = i;
            bestTs = candidateTs;
        }
    }

    if (bestIdx < 0) return false;
    out = _events[bestIdx];
    outTs = bestTs;
    return true;
}

bool eventsGetNext(Event& out) {
    time_t nextTs = 0;
    return eventsGetNextOccurrence(out, nextTs);
}
