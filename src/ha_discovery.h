#pragma once

// Home Assistant MQTT Discovery — publica configuracao de entidades e estados
// no topico `homeassistant/` seguindo a convencao oficial do HA. Requer MQTT
// habilitado e conectado (notifMqttIsConnected()). As publicacoes sao retained
// para que o HA recupere o estado mesmo apos um restart do broker.
//
// Entidades expostas:
// - sensor.cubinho_battery       — % de bateria
// - sensor.cubinho_rssi          — dBm do WiFi
// - sensor.cubinho_uptime        — segundos desde boot
// - sensor.cubinho_weather_temp  — temperatura atual do ultimo fetch
// - binary_sensor.cubinho_alarm  — true enquanto o alarme do timer toca
// - binary_sensor.cubinho_timer  — true enquanto algum timer roda

// Retorna true se a descoberta esta habilitada (padrao: ativa quando MQTT OK).
bool haDiscoveryEnabled();

// Ativa/desativa. Quando desativado, publica payloads vazios para limpar
// entidades existentes no HA (retained delete).
void haDiscoverySetEnabled(bool enabled);

// Publica as configuracoes de todas as entidades. Chamar quando o MQTT
// conecta (uma vez por sessao). Usa topico "homeassistant/<component>/<node>/<obj>/config".
void haDiscoveryPublishConfigs();

// Publica estados (chamar periodicamente; internamente limita a 1x/30s).
void haDiscoveryPublishStates();
