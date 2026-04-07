#include "ota_manager.h"
#include "config.h"
#include "logger.h"
#include <ArduinoOTA.h>
#include <WiFi.h>

static bool _otaStarted = false;

void otaInit() {
#if OTA_ENABLED
    if (_otaStarted) return;
    if (WiFi.status() != WL_CONNECTED) return;

    ArduinoOTA.setHostname(OTA_HOSTNAME);

    ArduinoOTA.onStart([]() {
        String type = (ArduinoOTA.getCommand() == U_FLASH) ? "firmware" : "filesystem";
        LOG_I("ota", "Iniciando update: %s", type.c_str());
    });

    ArduinoOTA.onEnd([]() {
        LOG_I("ota", "Update concluido — reiniciando");
    });

    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        static int lastPct = -1;
        int pct = progress * 100 / total;
        if (pct != lastPct && pct % 10 == 0) {
            LOG_I("ota", "Progresso: %d%%", pct);
            lastPct = pct;
        }
    });

    ArduinoOTA.onError([](ota_error_t error) {
        const char* msg = "desconhecido";
        if      (error == OTA_AUTH_ERROR)    msg = "auth";
        else if (error == OTA_BEGIN_ERROR)   msg = "begin";
        else if (error == OTA_CONNECT_ERROR) msg = "connect";
        else if (error == OTA_RECEIVE_ERROR) msg = "receive";
        else if (error == OTA_END_ERROR)     msg = "end";
        LOG_E("ota", "Erro: %s", msg);
    });

    ArduinoOTA.begin();
    _otaStarted = true;
    LOG_I("ota", "OTA ativo — hostname: %s", OTA_HOSTNAME);
#endif
}

void otaUpdate() {
#if OTA_ENABLED
    if (_otaStarted) ArduinoOTA.handle();
#endif
}

bool otaIsActive() {
    return _otaStarted;
}
