#pragma once
#include <Arduino.h>

// Lista de compras compartilhada. CRUD via HTTP (porta 8080) e MQTT
// (subtopico <base>/shopping). Persistida em NVS no namespace "shop" como JSON
// compacto para sobreviver a reboot/deep sleep.
//
// Uso tipico: alguem lembra de comprar leite na cozinha, abre "Cubinho Push"
// no celular (ou manda por automacao), e o item aparece contado na home do
// Cubinho. O usuario marca como feito direto no celular ou dispara "clear"
// quando chega em casa do mercado.
//
// Limite: SHOPPING_MAX itens (20); nome ate 32 chars.

#ifndef SHOPPING_MAX
#define SHOPPING_MAX 20
#endif

#ifndef SHOPPING_NAME_LEN
#define SHOPPING_NAME_LEN 32
#endif

struct ShoppingItem {
    char name[SHOPPING_NAME_LEN];
    bool done;
    uint32_t addedAt;   // time_t (epoch) quando foi criado; 0 se sem clock
};

// Inicializa o modulo — le estado do NVS. Chame no setup() antes do primeiro
// loop().
void shoppingInit();

// CRUD basico. Retornam true em caso de sucesso.
bool shoppingAdd(const char* name);          // ignora duplicados (case-insensitive)
bool shoppingToggle(int index);              // alterna done
bool shoppingRemove(int index);
void shoppingClear();                        // esvazia toda a lista
void shoppingClearDone();                    // remove apenas os marcados

// Leitura
int                  shoppingCount();        // total de itens (done + pending)
int                  shoppingPendingCount(); // itens nao marcados
const ShoppingItem*  shoppingGetAt(int index);

// Serializa para JSON (retorna tamanho escrito).
// Formato: {"items":[{"name":"leite","done":false,"added":1713456789}, ...],"pending":3}
size_t shoppingToJson(char* out, size_t outSize);

// Aplica um comando vindo de MQTT/HTTP em JSON.
// Aceita:
//   {"action":"add","name":"..."}
//   {"action":"toggle","index":0}
//   {"action":"remove","index":0}
//   {"action":"clear"}
//   {"action":"clear_done"}
//   ou array puro [{"name":"..."}, ...] (substitui lista)
// Retorna true se processou.
bool shoppingApplyJson(const char* json);
