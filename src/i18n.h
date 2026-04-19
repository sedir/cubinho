#pragma once

// ── i18n consolidado ────────────────────────────────────────────────────────
// Todas as strings exibidas ao usuario estao aqui, facilitando auditoria e
// futura troca de idioma. A infraestrutura e centralizada: o firmware le
// `g_lang` (ver abaixo) e indexa a tabela correspondente via macro `T(key)`.
//
// Por enquanto so existe uma lingua (pt-BR) e o macro `T(x)` simplesmente
// devolve o literal. Manter tudo via `T()` evita "vazar" textos em PT no
// codigo — quando chegar a hora de adicionar en-US ou ja-JP basta trocar o
// macro por uma lookup table sem mexer nos screens.
//
// Regras:
// 1. Novas strings user-facing devem entrar aqui, nunca como literal inline.
// 2. IDs em maiusculas e snake_case (ex: `I18N_BATTERY_LOW`).
// 3. Dias/meses/unidades tambem vivem aqui.
// 4. Strings de LOG_I/W/E nao sao user-facing — ficam inline em portugues.

namespace i18n {

// ── Telas — cabecalhos ──────────────────────────────────────────────────────
static constexpr const char* SCREEN_HOME_SYNC      = "atualizando...";
static constexpr const char* SCREEN_HOME_NO_EVENT  = "";
static constexpr const char* SCREEN_HOME_READY     = "PRONTO!";
static constexpr const char* SCREEN_HOME_CLOSE     = "toque para fechar";

// ── Timer ───────────────────────────────────────────────────────────────────
static constexpr const char* TIMER_HINT_START      = "Segurar: iniciar";
static constexpr const char* TIMER_HINT_RESET      = "Segurar: zerar";
static constexpr const char* TIMER_STOPWATCH       = "SW · Cronometro";

// ── Modo cozinha ────────────────────────────────────────────────────────────
static constexpr const char* COOKING_BADGE_PREFIX  = "COZINHA";

// ── WiFi / Conexao ──────────────────────────────────────────────────────────
static constexpr const char* NET_NO_CONNECTION     = "Sem conexao";
static constexpr const char* NET_CONNECTING        = "conectando...";
static constexpr const char* NET_PORTAL_TITLE      = "Configuracao WiFi";
static constexpr const char* NET_PORTAL_HINT       = "Conecte ao AP e abra 192.168.4.1";

// ── Bateria ─────────────────────────────────────────────────────────────────
static constexpr const char* BAT_LOW_WARN          = "BATERIA BAIXA!";
static constexpr const char* BAT_CHARGING          = "carregando";

// ── Clima ───────────────────────────────────────────────────────────────────
static constexpr const char* WEATHER_UPDATING      = "atualizando clima...";
static constexpr const char* WEATHER_UPDATED_AT    = "Atualizado";
static constexpr const char* WEATHER_MAX           = "Max";
static constexpr const char* WEATHER_MIN           = "Min";
static constexpr const char* WEATHER_HUMIDITY      = "Umidade";
static constexpr const char* WEATHER_CURRENT       = "Atual";

// ── Sistema ─────────────────────────────────────────────────────────────────
static constexpr const char* SYS_BOOT_COUNT        = "Boots";
static constexpr const char* SYS_UPTIME            = "Uptime";
static constexpr const char* SYS_IP                = "IP";
static constexpr const char* SYS_RSSI              = "RSSI";
static constexpr const char* SYS_HEAP              = "Heap";
static constexpr const char* SYS_PSRAM             = "PSRAM";

// ── Notificacoes ────────────────────────────────────────────────────────────
static constexpr const char* NOTIF_EMPTY           = "Nenhuma notificacao";
static constexpr const char* NOTIF_MORE_FMT        = "+ %d mais...";
static constexpr const char* NOTIF_CLEAR           = "Limpar";
static constexpr const char* NOTIF_MQTT_DEFAULT    = "MQTT";

// ── Dias / meses (curtos, 3 letras) ─────────────────────────────────────────
static constexpr const char* DIAS[]  = { "dom", "seg", "ter", "qua", "qui", "sex", "sab" };
static constexpr const char* MESES[] = { "jan", "fev", "mar", "abr", "mai", "jun",
                                          "jul", "ago", "set", "out", "nov", "dez" };

}  // namespace i18n

// Macro de conveniencia — hoje passthrough, amanha pode virar lookup por idioma.
#define T(x) (i18n::x)
