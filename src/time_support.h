// time_support.h — horloge système pour la vérification de date des certificats TLS.
//
// Le RP2040 n'a pas d'horloge fiable au boot. On synchronise l'heure par SNTP à la
// connexion WiFi (lwIP apps/sntp) et on fournit cette horloge à mbedTLS
// (MBEDTLS_PLATFORM_TIME_ALT) pour qu'il évalue notBefore/notAfter
// (MBEDTLS_HAVE_TIME_DATE). Avant la première synchro, on retombe sur la date de build
// (BUILD_EPOCH) comme plancher : suffisant pour rejeter les certificats déjà expirés à
// la compilation, sans bloquer les certificats valides récents.
#ifndef _TIME_SUPPORT_H
#define _TIME_SUPPORT_H

#include <time.h>
#include "pico/stdlib.h"
#include "lwip/apps/sntp.h"

#ifndef BUILD_EPOCH
#define BUILD_EPOCH 1735689600UL   // 2025-01-01 — repli si CMake ne le définit pas
#endif

static volatile time_t   timeEpochBase = (time_t)BUILD_EPOCH; // epoch à la dernière synchro
static volatile uint32_t timeBaseMs    = 0;                   // ms-depuis-boot à la synchro
static volatile bool     timeSynced    = false;              // SNTP/AT$TIME a fixé l'heure ?
static int               tzOffsetMin   = 0;                   // fuseau : décalage vs UTC (min)

// Horloge courante (epoch UTC) : base + temps écoulé depuis la base.
static time_t modemNow(void) {
   uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - timeBaseMs;
   return timeEpochBase + (time_t)(elapsed / 1000u);
}

// Fixe l'heure (appelée par lwIP SNTP via la macro SNTP_SET_SYSTEM_TIME, ou par AT$TIME=).
extern "C" void sntpSetSystemTime(unsigned long sec) {
   timeEpochBase = (time_t)sec;
   timeBaseMs    = to_ms_since_boot(get_absolute_time());
   timeSynced    = true;
}

// Conversion epoch UTC → {année, mois, jour, heure, min, sec} SANS gmtime_r.
// gmtime_r (newlib) prend un verrou interne ; appelé depuis le contexte protégé du
// handshake TLS (async_context threadsafe-background), il fige la puce. Cet algorithme
// (jours civils, d'après H. Hinnant) est purement arithmétique, donc sûr partout.
static void epochToYMDHMS(time_t t, long out[6]) {
   long days = (long)(t / 86400);
   long secs = (long)(t % 86400);
   if( secs < 0 ) { secs += 86400; --days; }
   out[3] = secs / 3600; out[4] = (secs % 3600) / 60; out[5] = secs % 60;
   long z = days + 719468;
   long era = (z >= 0 ? z : z - 146096) / 146097;
   long doe = z - era * 146097;
   long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
   long y = yoe + era * 400;
   long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
   long mp = (5 * doy + 2) / 153;
   long d = doy - (153 * mp + 2) / 5 + 1;
   long m = mp < 10 ? mp + 3 : mp - 9;
   out[0] = y + (m <= 2); out[1] = m; out[2] = d;
}

// Démarre la synchro SNTP (idempotent). À appeler une fois le WiFi + DNS prêts.
static void startSntp(void) {
   static bool started = false;
   if( started ) return;
   started = true;
   sntp_setoperatingmode(SNTP_OPMODE_POLL);
   sntp_setservername(0, "pool.ntp.org");
   sntp_init();
}

// SNTP « one-shot » : dès que l'heure est synchronisée, on ARRÊTE le service. Laisser
// SNTP actif en arrière-plan (pcb UDP + timer lwIP) entrait en conflit avec le scan
// flash de lazyCaCb pendant un handshake vérifié et figeait la puce. On n'a besoin de
// l'heure qu'une fois (l'horloge avance ensuite avec l'uptime). À appeler dans loop().
static void maybeStopSntp(void) {
   static bool stopped = false;
   if( !stopped && timeSynced ) {
      sntp_stop();
      stopped = true;
   }
}

#endif
