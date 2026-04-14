#pragma once
#include <Arduino.h>
#include <time.h>

#define MAX_CALENDAR_EVENTS 8

struct CalendarEvent {
    char   title[40];
    time_t startTs;
    time_t endTs;
    bool   allDay;
};

enum CalendarStatus {
    CAL_STATUS_OFF,
    CAL_STATUS_SAVED,
    CAL_STATUS_OK,
    CAL_STATUS_EMPTY,
    CAL_STATUS_ERROR,
    CAL_STATUS_TIME_INVALID,
};

bool calendarHasFeedConfigured();
bool calendarFetchToday();
bool calendarBuildTodaySummary(char* out, size_t outSize);
int  calendarTodayCount();
CalendarStatus calendarGetStatus();
const char* calendarGetStatusLabel();
const char* calendarGetStatusText();
