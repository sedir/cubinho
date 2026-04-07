#pragma once
#include <Arduino.h>

#define MAX_EVENTS 10

struct Event {
    char    name[24];
    uint8_t month;     // 1–12
    uint8_t day;       // 1–31
    uint8_t hour;      // 0–23
    uint8_t minute;    // 0–59
    bool    active;
};

// Inicializa e carrega eventos do SD card (/events.json).
void eventsInit();

// Retorna o número de eventos ativos.
int eventsCount();

// Retorna evento por índice (0-based). Retorna nullptr se inválido.
const Event* eventsGet(int index);

// Adiciona um evento. Retorna true se sucesso.
bool eventsAdd(const char* name, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute);

// Remove evento por índice. Retorna true se sucesso.
bool eventsRemove(int index);

// Retorna o próximo evento a ocorrer (ou nullptr se nenhum).
// Preenche 'out' com o evento e retorna true.
bool eventsGetNext(Event& out);

// Salva eventos no SD card.
void eventsSave();
