#pragma once
#include <Arduino.h>
#include <M5Unified.h>
#include <time.h>

// Porta HTTP para o servidor de push. Usa 8080 para nao colidir com a porta 80
// usada pelo portal cativo e pela tela de config do iCal.
#ifndef NOTIF_HTTP_PORT
#define NOTIF_HTTP_PORT 8080
#endif
#ifndef NOTIF_MAX
#define NOTIF_MAX 12
#endif

enum NotifIcon { NOTIF_ICON_INFO = 0, NOTIF_ICON_WARN, NOTIF_ICON_ERROR, NOTIF_ICON_OK };

struct NotifItem {
    char      title[40];
    char      body[128];
    time_t    timestamp;
    uint32_t  recvMs;
    NotifIcon icon;
    bool      read;
};

// ── Storage ──────────────────────────────────────────────────────────────────
void notifInit();
void notifPush(const char* title, const char* body, NotifIcon icon);
int  notifGetCount();
int  notifGetUnreadCount();
const NotifItem* notifGetAt(int i);   // i=0 é a mais recente
void notifClearAll();
void notifMarkAllRead();

// ── HTTP server — ciclo de vida atrelado ao WiFi keep-alive ─────────────────
void notifServerPoll();                // chamar no loop — inicia/para automaticamente
bool notifServerIsRunning();

// ── Toast (banner breve no topo) ─────────────────────────────────────────────
bool notifToastActive();
void notifToastDismiss();
void notifToastDraw(lgfx::LovyanGFX& d);

// ── Gaveta (drawer) ──────────────────────────────────────────────────────────
bool notifDrawerIsOpen();
bool notifDrawerIsAnimating();
bool notifDrawerIsVisible();           // open || animating
void notifDrawerOpen();
void notifDrawerClose();
void notifDrawerUpdate();              // avança animação — chamar no loop
void notifDrawerDraw(lgfx::LovyanGFX& d);

// Retorna true se consumiu o evento (main.cpp deve pular o processamento normal)
bool notifDrawerHandleRelease(int x, int y, int deltaX, int deltaY, bool longPress);

// Detecta swipe de abertura a partir da borda superior
bool notifShouldOpenFromSwipe(int startY, int deltaX, int deltaY);
