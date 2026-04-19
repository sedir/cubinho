#pragma once
#include <Arduino.h>

// Recado curto para a familia — uma unica string persistida em NVS, exibida na
// home abaixo do relogio. Pensada para mensagens efemeras tipo "buscar Lia 17h",
// "comida no forno", "ligar pro encanador". Substitui o proximo evento quando
// preenchido.
//
// Atualizado via HTTP POST /note e MQTT <base>/note (texto puro ou JSON com
// {"text":"..."}). Um texto vazio apaga o recado.

#ifndef NOTE_TEXT_LEN
#define NOTE_TEXT_LEN 96
#endif

void        familyNoteInit();
void        familyNoteSet(const char* text);
bool        familyNoteHas();                       // true se nao vazio
void        familyNoteGet(char* out, size_t outSize);
uint32_t    familyNoteTimestamp();                 // epoch quando foi definido
void        familyNoteClear();
