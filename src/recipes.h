#pragma once
#include <Arduino.h>

// Receitas rapidas — cada preset do timer combina NOME + DURACAO (minutos).
// Antes eram strings soltas ("Forno") e o usuario tinha que ajustar os minutos
// toda vez; agora cada preset ja vem com uma duracao razoavel por default, e
// o usuario pode editar pelo portal web (nome e/ou minutos).
//
// Persistidos em NVS (namespace "rcp") como JSON compacto. Se o NVS estiver
// vazio, usa os defaults abaixo.

#ifndef RECIPES_COUNT
#define RECIPES_COUNT 10
#endif

#ifndef RECIPE_NAME_LEN
#define RECIPE_NAME_LEN 16
#endif

struct Recipe {
    char name[RECIPE_NAME_LEN];
    int  minutes;  // 1–99
};

void  recipesInit();                             // carrega NVS (ou defaults)
int   recipesCount();                            // sempre RECIPES_COUNT
const Recipe* recipesGetAt(int index);           // nullptr se fora do range

// Define nome e duracao para o preset. Salva em NVS.
bool  recipesSetAt(int index, const char* name, int minutes);

// Aplica um JSON de array: [{"name":"Forno","minutes":30}, ...].
// Substitui a tabela inteira. Entradas faltantes ficam com defaults.
bool  recipesApplyJson(const char* json);

// Serializa para JSON: [{"name":"...","minutes":N}, ...]
size_t recipesToJson(char* out, size_t outSize);

// Reseta para defaults de fabrica.
void  recipesResetDefaults();
