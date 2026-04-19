#include "ha_discovery.h"
#include "notifications.h"
#include "power_manager.h"
#include "screen_home.h"
#include "logger.h"
#include <WiFi.h>

// Node-id baseado no MAC (ultimos 6 hex). Permite multiplos Cubinhos coexistirem
// no mesmo broker/HA sem colisao de unique_id.
static char _nodeId[24]   = "";
static char _devicePath[16] = "";  // ex: "cubinho_a1b2c3"
static bool _enabled      = true;
static uint32_t _lastStateMs = 0;
static bool _configsPublished = false;

static void ensureNodeId() {
    if (_nodeId[0] != '\0') return;
    uint64_t mac = ESP.getEfuseMac();
    snprintf(_devicePath, sizeof(_devicePath), "cubinho_%06llx", mac & 0xFFFFFFULL);
    snprintf(_nodeId, sizeof(_nodeId), "%s", _devicePath);
}

bool haDiscoveryEnabled() { return _enabled; }

void haDiscoverySetEnabled(bool enabled) {
    if (_enabled == enabled) return;
    _enabled = enabled;
    if (!enabled) {
        // Publica configs vazios (retained delete) para limpar entidades no HA.
        ensureNodeId();
        char topic[96];
        const char* const kEnts[] = {
            "sensor/battery", "sensor/rssi", "sensor/uptime",
            "binary_sensor/alarm", "binary_sensor/timer"
        };
        for (size_t i = 0; i < sizeof(kEnts)/sizeof(kEnts[0]); i++) {
            snprintf(topic, sizeof(topic), "homeassistant/%s/%s/config",
                     kEnts[i], _nodeId);
            notifMqttPublish(topic, "", true);
        }
        _configsPublished = false;
        LOG_I("ha", "Discovery desativado — entidades limpas");
    } else {
        _configsPublished = false;  // forca re-publicar na proxima chamada
    }
}

// Monta bloco "device" comum a todas as entidades (JSON interno ao payload).
static void buildDeviceBlock(char* out, size_t outSize) {
    snprintf(out, outSize,
        "\"dev\":{"
        "\"ids\":[\"%s\"],"
        "\"name\":\"Cubinho\","
        "\"mf\":\"M5Stack\","
        "\"mdl\":\"CoreS3\","
        "\"sw\":\"cubinho-fw\""
        "}",
        _devicePath);
}

// Publica a config de uma entidade.
// `component` ex: "sensor" ou "binary_sensor"
// `obj` ex: "battery"
// `extra` campos adicionais (sem trailing vírgula); ex: "\"dev_cla\":\"battery\",\"unit_of_meas\":\"%\""
static void publishConfig(const char* component, const char* obj,
                          const char* friendlyName, const char* extra) {
    char topic[128];
    snprintf(topic, sizeof(topic), "homeassistant/%s/%s/%s/config",
             component, _nodeId, obj);

    char stateTopic[96];
    snprintf(stateTopic, sizeof(stateTopic), "%s/%s", _devicePath, obj);

    char dev[192];
    buildDeviceBlock(dev, sizeof(dev));

    char payload[512];
    snprintf(payload, sizeof(payload),
        "{"
        "\"name\":\"%s\","
        "\"stat_t\":\"%s\","
        "\"uniq_id\":\"%s_%s\","
        "%s%s"
        "%s"
        "}",
        friendlyName,
        stateTopic,
        _devicePath, obj,
        extra ? extra : "",
        (extra && extra[0]) ? "," : "",
        dev);

    notifMqttPublish(topic, payload, true);
}

void haDiscoveryPublishConfigs() {
    if (!_enabled) return;
    if (!notifMqttIsConnected()) return;
    ensureNodeId();

    publishConfig("sensor", "battery", "Bateria",
        "\"dev_cla\":\"battery\",\"unit_of_meas\":\"%\",\"stat_cla\":\"measurement\"");
    publishConfig("sensor", "rssi", "RSSI",
        "\"dev_cla\":\"signal_strength\",\"unit_of_meas\":\"dBm\",\"stat_cla\":\"measurement\",\"ent_cat\":\"diagnostic\"");
    publishConfig("sensor", "uptime", "Uptime",
        "\"unit_of_meas\":\"s\",\"stat_cla\":\"total_increasing\",\"ent_cat\":\"diagnostic\"");
    publishConfig("binary_sensor", "alarm", "Alarme",
        "\"dev_cla\":\"sound\",\"pl_on\":\"ON\",\"pl_off\":\"OFF\"");
    publishConfig("binary_sensor", "timer", "Timer rodando",
        "\"pl_on\":\"ON\",\"pl_off\":\"OFF\"");

    _configsPublished = true;
    _lastStateMs = 0;  // forca primeira publicacao de estado imediata
    LOG_I("ha", "Discovery configs publicados como %s", _nodeId);
}

static void publishState(const char* obj, const char* value) {
    char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s", _devicePath, obj);
    notifMqttPublish(topic, value, true);
}

void haDiscoveryPublishStates() {
    if (!_enabled) return;
    if (!notifMqttIsConnected()) return;
    if (!_configsPublished) haDiscoveryPublishConfigs();

    uint32_t now = millis();
    if (_lastStateMs != 0 && (now - _lastStateMs) < 30000UL) return;
    _lastStateMs = now;

    char buf[24];

    int bat = batteryPercent();
    if (bat >= 0) {
        snprintf(buf, sizeof(buf), "%d", bat);
        publishState("battery", buf);
    }

    int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : 0;
    snprintf(buf, sizeof(buf), "%d", rssi);
    publishState("rssi", buf);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)(now / 1000UL));
    publishState("uptime", buf);

    publishState("alarm", screenHomeIsAlarmActive() ? "ON" : "OFF");
    publishState("timer", screenHomeIsTimerRunning() ? "ON" : "OFF");
}
