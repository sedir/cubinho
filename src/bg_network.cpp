#include "bg_network.h"
#include "calendar_feed.h"
#include "logger.h"
#include "notifications.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_heap_caps.h>

// Stack de 12KB — suficiente para WiFiClientSecure (TLS é heap-allocated),
// HTTPClient, JSON parse, e iCal parse. Payload de rede vai para o heap.
#define BG_TASK_STACK  12288
#define BG_TASK_PRIO   1

static TaskHandle_t  _bgTask       = nullptr;
static volatile bool _bgDone       = false;
static bool          _bgWeatherOk  = false;
static WeatherData   _bgWeatherIn;
static WeatherData   _bgWeatherOut;

static void bgTaskFunc(void* param) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        uint32_t startMs = millis();
        LOG_I("bg_net", "Fetch iniciado (core %d, heap livre: %u)",
              xPortGetCoreID(), heap_caps_get_free_size(MALLOC_CAP_DEFAULT));

        // ── Weather ──
        WeatherData result = _bgWeatherIn;
        _bgWeatherOk = weatherFetch(result);
        if (_bgWeatherOk) {
            _bgWeatherOut = result;
            notifMarkWeatherFetch();  // telemetria /health
        } else {
            LOG_W("bg_net", "Weather fetch falhou — mantendo dados anteriores");
            _bgWeatherOut = _bgWeatherIn;
        }

        // ── Calendar ──
        if (calendarHasFeedConfigured()) {
            if (!calendarFetchToday()) {
                LOG_W("bg_net", "Calendario indisponivel — mantendo ultimo dado valido");
            }
        }

        LOG_I("bg_net", "Fetch concluido em %lu ms (heap livre: %u)",
              millis() - startMs, heap_caps_get_free_size(MALLOC_CAP_DEFAULT));
        // Barrier: garante que todas as escritas em _bgWeatherOut sejam visíveis
        // pelo core 1 antes que _bgDone seja lido como true.
        __sync_synchronize();
        _bgDone = true;
    }
}

void bgNetworkInit() {
    xTaskCreatePinnedToCore(bgTaskFunc, "bg_net", BG_TASK_STACK, nullptr,
                            BG_TASK_PRIO, &_bgTask, 0);
    LOG_I("bg_net", "Task de rede em background iniciada no core 0");
}

void bgNetworkStartFetch(WeatherData* current) {
    if (!_bgTask) return;
    _bgWeatherIn = current ? *current : WeatherData{};
    _bgDone      = false;
    _bgWeatherOk = false;
    xTaskNotifyGive(_bgTask);
}

bool bgNetworkIsDone() {
    return _bgDone;
}

void bgNetworkConsume(WeatherData& out) {
    if (_bgWeatherOk) {
        out = _bgWeatherOut;
    }
    _bgDone = false;
}
