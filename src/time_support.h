// time_support.h — system clock for TLS certificate date validation.
//
// The RP2040 has no reliable clock at boot. We sync the time via SNTP on WiFi
// connection (lwIP apps/sntp) and feed this clock to mbedTLS
// (MBEDTLS_PLATFORM_TIME_ALT) so it can evaluate notBefore/notAfter
// (MBEDTLS_HAVE_TIME_DATE). Before the first sync, we fall back to the build date
// (BUILD_EPOCH) as a floor: enough to reject certificates already expired at
// compile time, without blocking recent valid certificates.
#ifndef _TIME_SUPPORT_H
#define _TIME_SUPPORT_H

#include <time.h>
#include "pico/stdlib.h"
#include "lwip/apps/sntp.h"

#ifndef BUILD_EPOCH
#define BUILD_EPOCH 1735689600UL   // 2025-01-01 — fallback if CMake does not define it
#endif

static volatile time_t   timeEpochBase = (time_t)BUILD_EPOCH; // epoch at last sync
static volatile uint32_t timeBaseMs    = 0;                   // ms-since-boot at sync
static volatile bool     timeSynced    = false;              // SNTP/AT$TIME has set the time?
// Timezone (offset vs UTC, minutes): persisted in settings.tzOffsetMin (AT$TZ / AT&W).

// Current clock (epoch UTC): base + time elapsed since the base.
static time_t modemNow(void) {
   uint32_t elapsed = to_ms_since_boot(get_absolute_time()) - timeBaseMs;
   return timeEpochBase + (time_t)(elapsed / 1000u);
}

// Sets the time (called by lwIP SNTP via the SNTP_SET_SYSTEM_TIME macro, or by AT$TIME=).
extern "C" void sntpSetSystemTime(unsigned long sec) {
   timeEpochBase = (time_t)sec;
   timeBaseMs    = to_ms_since_boot(get_absolute_time());
   timeSynced    = true;
}

// Convert epoch UTC → {year, month, day, hour, min, sec} WITHOUT gmtime_r.
// gmtime_r (newlib) takes an internal lock; called from the protected context of the
// TLS handshake (async_context threadsafe-background), it freezes the chip. This algorithm
// (civil days, from H. Hinnant) is purely arithmetic, so safe everywhere.
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

// Starts the SNTP sync (idempotent). Call once WiFi + DNS are ready.
static void startSntp(void) {
   static bool started = false;
   if( started ) return;
   started = true;
   sntp_setoperatingmode(SNTP_OPMODE_POLL);
   sntp_setservername(0, "pool.ntp.org");
   sntp_init();
}

// SNTP "one-shot": as soon as the time is synced, we STOP the service. Leaving
// SNTP active in the background (UDP pcb + lwIP timer) conflicted with the flash
// scan of lazyCaCb during a verified handshake and froze the chip. We only need
// the time once (the clock then advances with uptime). Call from loop().
static void maybeStopSntp(void) {
   static bool stopped = false;
   if( !stopped && timeSynced ) {
      sntp_stop();
      stopped = true;
   }
}

#endif
