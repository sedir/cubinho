#include "calendar_feed.h"
#include "logger.h"
#include "runtime_config.h"
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>

static CalendarEvent _todayEvents[MAX_CALENDAR_EVENTS];
static int _todayCount = 0;
static int _todayTotalCount = 0;
static int _cacheYear = 0;
static int _cacheMonth = 0;
static int _cacheDay = 0;
static CalendarStatus _status = CAL_STATUS_OFF;

static bool getTodayBounds(time_t& todayStart, time_t& tomorrowStart, struct tm& todayInfo)
{
    time_t nowTs = time(nullptr);
    if (nowTs <= 0 || !localtime_r(&nowTs, &todayInfo)) return false;

    todayInfo.tm_hour = 0;
    todayInfo.tm_min = 0;
    todayInfo.tm_sec = 0;
    todayInfo.tm_isdst = -1;

    todayStart = mktime(&todayInfo);
    if (todayStart <= 0) return false;

    struct tm tomorrowInfo = todayInfo;
    tomorrowInfo.tm_mday += 1;
    tomorrowStart = mktime(&tomorrowInfo);
    return tomorrowStart > todayStart;
}

static void clearCacheForToday()
{
    _todayCount = 0;
    _todayTotalCount = 0;
}

static void setCacheDate(const struct tm& todayInfo)
{
    _cacheYear = todayInfo.tm_year + 1900;
    _cacheMonth = todayInfo.tm_mon + 1;
    _cacheDay = todayInfo.tm_mday;
}

static bool isCacheForToday()
{
    struct tm todayInfo = {};
    time_t todayStart = 0;
    time_t tomorrowStart = 0;
    if (!getTodayBounds(todayStart, tomorrowStart, todayInfo)) return false;
    return _cacheYear == (todayInfo.tm_year + 1900) &&
           _cacheMonth == (todayInfo.tm_mon + 1) &&
           _cacheDay == todayInfo.tm_mday;
}

static void normalizeCalendarUrl(const char* input, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    out[0] = '\0';
    if (!input || !input[0]) return;

    if (strncmp(input, "webcal://", 9) == 0) {
        snprintf(out, outSize, "https://%s", input + 9);
        return;
    }

    if (strncmp(input, "webcals://", 10) == 0) {
        snprintf(out, outSize, "https://%s", input + 10);
        return;
    }

    strlcpy(out, input, outSize);
}

static String stripLineEnding(const String& in)
{
    int end = in.length();
    while (end > 0 && (in[end - 1] == '\r' || in[end - 1] == '\n')) end--;
    return in.substring(0, end);
}

static void unescapeIcsText(const String& in, char* out, size_t outSize)
{
    if (!out || outSize == 0) return;
    size_t j = 0;
    for (size_t i = 0; i < (size_t)in.length() && j + 1 < outSize; i++) {
        char c = in[(int)i];
        if (c == '\\' && i + 1 < (size_t)in.length()) {
            char n = in[(int)(i + 1)];
            if (n == 'n' || n == 'N') {
                out[j++] = ' ';
                i++;
                continue;
            }
            if (n == ',' || n == ';' || n == '\\') {
                out[j++] = n;
                i++;
                continue;
            }
        }
        out[j++] = c;
    }
    out[j] = '\0';
}

static bool parseProperty(const String& line, const char* key, String& params, String& value)
{
    String prefix(key);
    if (!line.startsWith(prefix)) return false;

    int colon = line.indexOf(':');
    if (colon < 0) return false;

    params = line.substring(prefix.length(), colon);
    value = line.substring(colon + 1);
    return true;
}

static bool parseDateValue(const String& value, struct tm& outTm, bool& allDay, bool& isUtc)
{
    if (value.length() < 8) return false;

    memset(&outTm, 0, sizeof(outTm));
    outTm.tm_year = value.substring(0, 4).toInt() - 1900;
    outTm.tm_mon = value.substring(4, 6).toInt() - 1;
    outTm.tm_mday = value.substring(6, 8).toInt();
    outTm.tm_isdst = -1;

    allDay = true;
    isUtc = false;

    if (value.length() >= 15 && value[8] == 'T') {
        outTm.tm_hour = value.substring(9, 11).toInt();
        outTm.tm_min = value.substring(11, 13).toInt();
        outTm.tm_sec = value.substring(13, 15).toInt();
        allDay = false;
        isUtc = value.endsWith("Z");
    }

    return true;
}

static time_t mktimeUtc(struct tm tmValue)
{
    const char* oldTz = getenv("TZ");
    char oldTzCopy[32];
    bool hadOldTz = oldTz && oldTz[0];

    if (hadOldTz) {
        strlcpy(oldTzCopy, oldTz, sizeof(oldTzCopy));
    }

    setenv("TZ", "UTC0", 1);
    tzset();
    time_t ts = mktime(&tmValue);

    if (hadOldTz) setenv("TZ", oldTzCopy, 1);
    else          unsetenv("TZ");
    tzset();

    return ts;
}

static time_t toTimestamp(struct tm tmValue, bool isUtc)
{
    return isUtc ? mktimeUtc(tmValue) : mktime(&tmValue);
}

static void sortTodayEvents()
{
    for (int i = 1; i < _todayCount; i++) {
        CalendarEvent key = _todayEvents[i];
        int j = i - 1;
        while (j >= 0 && _todayEvents[j].startTs > key.startTs) {
            _todayEvents[j + 1] = _todayEvents[j];
            j--;
        }
        _todayEvents[j + 1] = key;
    }
}

static int parseDateKey(const String& value)
{
    if (value.length() < 8) return 0;
    return value.substring(0, 8).toInt();
}

static void maybeStoreEvent(const char* title, time_t startTs, time_t endTs, bool allDay,
                            time_t todayStart, time_t tomorrowStart,
                            int startDateKey, int endDateKey, int todayDateKey)
{
    if (!title[0]) return;
    if (endTs <= startTs) endTs = allDay ? (startTs + 86400) : (startTs + 60);

    bool overlapsToday = (startTs < tomorrowStart && endTs > todayStart);
    if (!overlapsToday && startDateKey > 0 && todayDateKey > 0) {
        int effectiveEndKey = (endDateKey > 0) ? endDateKey : startDateKey;
        if (allDay) {
            overlapsToday = (startDateKey <= todayDateKey && effectiveEndKey > todayDateKey);
        } else {
            overlapsToday = (startDateKey == todayDateKey ||
                             effectiveEndKey == todayDateKey ||
                             (startDateKey < todayDateKey && effectiveEndKey > todayDateKey));
        }
    }

    if (!overlapsToday) return;

    _todayTotalCount++;
    if (_todayCount >= MAX_CALENDAR_EVENTS) return;

    CalendarEvent& dst = _todayEvents[_todayCount++];
    strlcpy(dst.title, title, sizeof(dst.title));
    dst.startTs = startTs;
    dst.endTs = endTs;
    dst.allDay = allDay;
}

bool calendarHasFeedConfigured()
{
    return runtimeConfigHasCalendarUrl();
}

CalendarStatus calendarGetStatus()
{
    if (!calendarHasFeedConfigured()) {
        return CAL_STATUS_OFF;
    }
    if (_status == CAL_STATUS_OFF) {
        return CAL_STATUS_SAVED;
    }
    return _status;
}

const char* calendarGetStatusLabel()
{
    switch (calendarGetStatus()) {
        case CAL_STATUS_OFF:          return "OFF";
        case CAL_STATUS_SAVED:        return "URL";
        case CAL_STATUS_OK:           return "OK";
        case CAL_STATUS_EMPTY:        return "OK";
        case CAL_STATUS_ERROR:        return "ERRO";
        case CAL_STATUS_TIME_INVALID: return "RELOG";
        default:                      return "";
    }
}

const char* calendarGetStatusText()
{
    switch (calendarGetStatus()) {
        case CAL_STATUS_OFF:          return "Calendario desativado";
        case CAL_STATUS_SAVED:        return "URL salva, aguardando validacao";
        case CAL_STATUS_OK:           return "Calendario validado";
        case CAL_STATUS_EMPTY:        return "Calendario validado, sem eventos hoje";
        case CAL_STATUS_ERROR:        return "Falha ao validar o feed iCal";
        case CAL_STATUS_TIME_INVALID: return "Relogio invalido para validar calendario";
        default:                      return "";
    }
}

bool calendarFetchToday()
{
    if (!calendarHasFeedConfigured()) {
        clearCacheForToday();
        _status = CAL_STATUS_OFF;
        return false;
    }

    time_t todayStart = 0;
    time_t tomorrowStart = 0;
    struct tm todayInfo = {};
    if (!getTodayBounds(todayStart, tomorrowStart, todayInfo)) {
        LOG_W("calendar", "Relogio invalido — calendario ignorado");
        _status = CAL_STATUS_TIME_INVALID;
        return false;
    }

    char rawCalendarUrl[192];
    char calendarUrl[192];
    runtimeConfigGetCalendarUrl(rawCalendarUrl, sizeof(rawCalendarUrl));
    normalizeCalendarUrl(rawCalendarUrl, calendarUrl, sizeof(calendarUrl));
    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    HTTPClient http;
    int httpCode = -1;
    String payload;

    if (strncmp(calendarUrl, "https://", 8) == 0) {
        secureClient.setInsecure();
        if (!http.begin(secureClient, calendarUrl)) {
            LOG_E("calendar", "Falha ao iniciar HTTPClient HTTPS");
            _status = CAL_STATUS_ERROR;
            return false;
        }
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setTimeout(10000);
        httpCode = http.GET();
        if (httpCode == 200) payload = http.getString();
        http.end();
    } else {
        if (!http.begin(plainClient, calendarUrl)) {
            LOG_E("calendar", "Falha ao iniciar HTTPClient");
            _status = CAL_STATUS_ERROR;
            return false;
        }
        http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
        http.setTimeout(10000);
        httpCode = http.GET();
        if (httpCode == 200) payload = http.getString();
        http.end();
    }

    if (httpCode != 200) {
        LOG_W("calendar", "Erro HTTP: %d", httpCode);
        _status = CAL_STATUS_ERROR;
        return false;
    }

    clearCacheForToday();
    setCacheDate(todayInfo);
    int todayDateKey = (todayInfo.tm_year + 1900) * 10000 +
                       (todayInfo.tm_mon + 1) * 100 +
                       todayInfo.tm_mday;

    bool inEvent = false;
    char currentTitle[40] = "";
    time_t currentStart = 0;
    time_t currentEnd = 0;
    bool currentAllDay = false;
    bool hasStart = false;
    bool hasEnd = false;
    int currentStartDateKey = 0;
    int currentEndDateKey = 0;

    auto flushEvent = [&]() {
        if (!inEvent || !hasStart) return;
        maybeStoreEvent(currentTitle, currentStart, hasEnd ? currentEnd : 0, currentAllDay,
                        todayStart, tomorrowStart, currentStartDateKey, currentEndDateKey,
                        todayDateKey);
    };

    int pos = 0;
    String pendingLine;
    while (pos < payload.length()) {
        int lineEnd = payload.indexOf('\n', pos);
        if (lineEnd < 0) lineEnd = payload.length();
        String line = payload.substring(pos, lineEnd);
        pos = lineEnd + 1;
        line = stripLineEnding(line);

        if (line.length() > 0 && (line[0] == ' ' || line[0] == '\t') && pendingLine.length() > 0) {
            pendingLine += line.substring(1);
            continue;
        }

        if (pendingLine.length() > 0) {
            String params;
            String value;

            if (pendingLine == "BEGIN:VEVENT") {
                inEvent = true;
                currentTitle[0] = '\0';
                currentStart = 0;
                currentEnd = 0;
                currentAllDay = false;
                hasStart = false;
                hasEnd = false;
                currentStartDateKey = 0;
                currentEndDateKey = 0;
            } else if (pendingLine == "END:VEVENT") {
                flushEvent();
                inEvent = false;
            } else if (inEvent && parseProperty(pendingLine, "SUMMARY", params, value)) {
                unescapeIcsText(value, currentTitle, sizeof(currentTitle));
            } else if (inEvent && parseProperty(pendingLine, "DTSTART", params, value)) {
                struct tm tmValue = {};
                bool allDay = false;
                bool isUtc = false;
                currentStartDateKey = parseDateKey(value);
                if (parseDateValue(value, tmValue, allDay, isUtc)) {
                    currentStart = toTimestamp(tmValue, isUtc);
                    currentAllDay = allDay || params.indexOf("VALUE=DATE") >= 0;
                    hasStart = currentStart > 0;
                }
            } else if (inEvent && parseProperty(pendingLine, "DTEND", params, value)) {
                struct tm tmValue = {};
                bool allDay = false;
                bool isUtc = false;
                currentEndDateKey = parseDateKey(value);
                if (parseDateValue(value, tmValue, allDay, isUtc)) {
                    currentEnd = toTimestamp(tmValue, isUtc);
                    hasEnd = currentEnd > 0;
                }
            }
        }

        pendingLine = line;
    }

    if (pendingLine.length() > 0) {
        if (pendingLine == "END:VEVENT") {
            flushEvent();
        }
    }

    sortTodayEvents();
    LOG_I("calendar", "Eventos de hoje: %d/%d", _todayCount, _todayTotalCount);
    _status = (_todayTotalCount > 0) ? CAL_STATUS_OK : CAL_STATUS_EMPTY;
    return true;
}

int calendarTodayCount()
{
    return isCacheForToday() ? _todayTotalCount : 0;
}

bool calendarBuildTodaySummary(char* out, size_t outSize)
{
    if (!out || outSize == 0) return false;
    out[0] = '\0';
    if (!isCacheForToday() || _todayTotalCount <= 0 || _todayCount <= 0) return false;

    time_t nowTs = time(nullptr);
    int primaryIdx = 0;
    for (int i = 0; i < _todayCount; i++) {
        if (_todayEvents[i].allDay) continue;
        if (_todayEvents[i].endTs > nowTs) {
            primaryIdx = i;
            break;
        }
    }

    const CalendarEvent& primary = _todayEvents[primaryIdx];
    int extra = _todayTotalCount - 1;

    if (primary.allDay) {
        if (extra > 0) snprintf(out, outSize, "Hoje: %s +%d", primary.title, extra);
        else           snprintf(out, outSize, "Hoje: %s", primary.title);
        return true;
    }

    struct tm startInfo = {};
    localtime_r(&primary.startTs, &startInfo);
    if (extra > 0) {
        snprintf(out, outSize, "Hoje %02d:%02d • %s +%d",
                 startInfo.tm_hour, startInfo.tm_min, primary.title, extra);
    } else {
        snprintf(out, outSize, "Hoje %02d:%02d • %s",
                 startInfo.tm_hour, startInfo.tm_min, primary.title);
    }
    return true;
}
