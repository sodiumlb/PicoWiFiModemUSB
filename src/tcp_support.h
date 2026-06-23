//
// lwIP interface functions
//
#if LWIP_ALTCP_TLS_MBEDTLS
#include "mbedtls/ssl.h"       // mbedtls_ssl_set_hostname() for TLS SNI
#include "mbedtls/x509_crt.h"  // E-lazy on-demand trusted-CA callback
#include "mbedtls/platform.h"  // mbedtls_calloc() / mbedtls_free()
#include <string.h>

// ── E-lazy: on-demand trusted-CA lookup ────────────────────────────────────
// Instead of parsing the whole CA store into RAM (impossible on the RP2040 for
// a full bundle), we stream the LittleFS bundle (ca.pem, one or many PEM certs
// concatenated) and parse only the single CA whose subject matches the issuer
// mbedTLS is currently looking for. Peak RAM ≈ one certificate.
// See docs/design-proxy-tls-ssh.md §10.

#define CA_LAZY_PEM_MAX 3072   // one PEM cert (covers RSA-4096 roots, e.g. Mozilla bundle max ~2772 o)

// Small buffered reader over the bundle cursor (avoids a flash call per byte).
static char caRdBuf[256];
static int  caRdLen = 0, caRdPos = 0;

static int caRdGetc(void) {
   if( caRdPos >= caRdLen ) {
      caRdLen = caBundleRead(caRdBuf, sizeof(caRdBuf));
      caRdPos = 0;
      if( caRdLen <= 0 ) return -1;
   }
   return (unsigned char)caRdBuf[caRdPos++];
}

// Copy the next PEM certificate block (BEGIN..END inclusive, NUL-terminated)
// into out. Return codes:
//   >0  length of the captured cert
//    0  clean end of bundle
//   -1  cert larger than the buffer: the block is DRAINED to its END line so the
//       caller stays in sync and can skip to the next cert (do NOT stop the scan)
//   -2  EOF in the middle of a block (truncated bundle): real error, stop
static int caLazyNextBlock(char *out, size_t outsz) {
   char line[96];
   size_t len = 0;
   bool capturing = false, overflow = false;
   for(;;) {
      size_t ll = 0;
      int c;
      while( (c = caRdGetc()) >= 0 && c != '\n' ) {
         if( c == '\r' ) continue;
         if( ll < sizeof(line) - 1 ) line[ll++] = (char)c;
      }
      line[ll] = '\0';
      if( c < 0 && ll == 0 )
         return capturing ? -2 : 0;        // EOF: truncated block, or clean end
      if( !capturing ) {
         if( strstr(line, "BEGIN CERTIFICATE") ) capturing = true;
         else continue;                    // skip whatever sits between certs
      }
      // Once oversized, keep consuming the block (to stay byte-aligned) without
      // storing, so the next call resumes cleanly at the following certificate.
      if( !overflow && len + ll + 1 >= outsz ) overflow = true;
      if( !overflow ) {
         memcpy(out + len, line, ll); len += ll;
         out[len++] = '\n';
      }
      if( strstr(line, "END CERTIFICATE") ) {
         if( overflow ) return -1;         // oversized cert: drained, skip it
         out[len] = '\0';
         return (int)len;
      }
      if( c < 0 ) return -2;               // EOF in the middle of a block
   }
}

// mbedTLS trust callback: return the CA(s) that may have issued `child`.
static int lazyCaCb(void *ctx, const mbedtls_x509_crt *child,
                    mbedtls_x509_crt **candidate_cas) {
   (void)ctx;
   *candidate_cas = NULL;
   if( !caBundleOpen() ) return 0;         // no bundle → no candidate (verify fails)
   caRdLen = caRdPos = 0;
   static char pem[CA_LAZY_PEM_MAX];
   for(;;) {
      int n = caLazyNextBlock(pem, sizeof(pem));
      if( n == -1 ) continue;              // cert too big for the buffer: skip it
      if( n <= 0 ) break;                  // 0 = end of bundle, -2 = truncated block
      mbedtls_x509_crt probe;
      mbedtls_x509_crt_init(&probe);
      if( mbedtls_x509_crt_parse(&probe, (const unsigned char *)pem, (size_t)n + 1) == 0
          && probe.subject_raw.len == child->issuer_raw.len
          && memcmp(probe.subject_raw.p, child->issuer_raw.p, probe.subject_raw.len) == 0 ) {
         // Match: hand a fresh heap copy to mbedTLS, which takes ownership.
         mbedtls_x509_crt *cand = (mbedtls_x509_crt *)mbedtls_calloc(1, sizeof(*cand));
         if( cand ) {
            mbedtls_x509_crt_init(cand);
            if( mbedtls_x509_crt_parse(cand, (const unsigned char *)pem, (size_t)n + 1) == 0 ) {
               *candidate_cas = cand;
            } else {
               mbedtls_x509_crt_free(cand);
               mbedtls_free(cand);
            }
         }
         mbedtls_x509_crt_free(&probe);
         break;
      }
      mbedtls_x509_crt_free(&probe);
   }
   caBundleClose();
   return 0;
}

// Date check (mbedTLS lacks HAVE_TIME_DATE — it stalled the handshake with the
// trust callback). Returns 0 if `crt` is within its validity period,
// MBEDTLS_X509_BADCERT_FUTURE (not yet valid) or MBEDTLS_X509_BADCERT_EXPIRED per
// the SNTP clock. Lexicographic comparison (year, month, day, h, min, s).
static uint32_t certDateFlags(const mbedtls_x509_crt *crt) {
   long cur[6];
   epochToYMDHMS(modemNow(), cur);          // NOT gmtime_r (lock → handshake deadlock)
   const mbedtls_x509_time *vf = &crt->valid_from, *vt = &crt->valid_to;
   long from[6] = { vf->year, vf->mon, vf->day, vf->hour, vf->min, vf->sec };
   long to[6]   = { vt->year, vt->mon, vt->day, vt->hour, vt->min, vt->sec };
   int cf = 0, ct = 0;                       // sign of (cur - valid_from) and (cur - valid_to)
   for( int i = 0; i < 6 && cf == 0; ++i ) cf = (cur[i] > from[i]) - (cur[i] < from[i]);
   for( int i = 0; i < 6 && ct == 0; ++i ) ct = (cur[i] > to[i])   - (cur[i] < to[i]);
   if( cf < 0 ) return MBEDTLS_X509_BADCERT_FUTURE;   // cur < valid_from → not yet valid
   if( ct > 0 ) return MBEDTLS_X509_BADCERT_EXPIRED;  // cur > valid_to   → expired
   return 0;
}

// mbedTLS verify callback: invoked for each cert in the chain DURING the handshake
// (the cert is still alive). We OR the date fault into the flags → handshake rejected
// if expired/not yet valid. Only if the time is synced (otherwise we don't reject,
// lacking a reliable clock). See time_support.h.
static int dateVerifyCb(void *ctx, mbedtls_x509_crt *crt, int depth, uint32_t *flags) {
   (void)ctx; (void)depth;
   if( timeSynced ) {
      *flags |= certDateFlags(crt);
   }
   return 0;
}
#endif

static volatile bool dnsLookupFinished = false;

static void dnsLookupDone(const char *name, const ip_addr_t *ipaddr, void *arg) {
   ip_addr_t *resolved = (ip_addr_t *)arg;
   if( ipaddr && ipaddr->addr) {
      resolved->addr = ipaddr->addr;
   }
   dnsLookupFinished = true;
}

bool dnsLookup(const char *name, ip_addr_t *resolved) {
   
   dnsLookupFinished = false;
   ip4_addr_set_any(resolved);
   
   switch( dns_gethostbyname(name, resolved, dnsLookupDone, resolved) ) {
      case ERR_OK:
         return true;
         break;
      case ERR_INPROGRESS:
         break;
      default:
         return false;
   }
   while( !dnsLookupFinished ) {
      tight_loop_contents();
   }
   return !ip4_addr_isany(resolved);
}

uint32_t millis(void) {
   return to_ms_since_boot(get_absolute_time());
}

bool tcpIsConnected(TCP_CLIENT_T *client) {
   if( client && client->pcb && client->pcb->arg ) {
      return client->connected;
   }
   return false;
}

err_t tcpClientClose(TCP_CLIENT_T *client) {
   err_t err = ERR_OK;
   
   cyw43_arch_lwip_begin();
   if( client ) {
      client->connected = false;
      if( client->pcb ) {
         altcp_err( client->pcb, NULL);
         altcp_sent(client->pcb, NULL);
         altcp_poll(client->pcb, NULL, 0);
         altcp_recv(client->pcb, NULL);
         altcp_arg( client->pcb, NULL);
         err = altcp_close(client->pcb);
         if( err != ERR_OK ) {
            altcp_abort(client->pcb);
            err = ERR_ABRT;
         }
         client->pcb = NULL;
      }
   }
   cyw43_arch_lwip_end();
   return err;
}

// NB: the PCB may have already been freed when this function is called
static void tcpClientErr(void *arg, err_t err) {
   TCP_CLIENT_T *client = (TCP_CLIENT_T *)arg;

   if( client ) {
      client->connectFinished = true;
      client->connected = false;
      client->pcb = NULL;
   }
}

static err_t tcpSend(TCP_CLIENT_T *client) {
   err_t err = ERR_OK;
   uint32_t ints;
   
   if( client->txBuffLen ) {
      uint16_t maxLen = altcp_sndbuf(client->pcb);
      if( maxLen > 0 && altcp_sndqueuelen(client->pcb) < TCP_SND_QUEUELEN ) {
         if( client->txBuffLen < maxLen ) {
            maxLen = client->txBuffLen;
         }
         uint8_t tmp[maxLen];
         // make copies of the head and length and work
         // with those in case altcp_write fails and we
         // have to re-send the same data later
         uint16_t tmpTxBuffHead = client->txBuffHead;
         uint16_t tmpTxBuffLen = client->txBuffLen;
         for( int i = 0; i < maxLen; ++i ) {
            tmp[i] = client->txBuff[tmpTxBuffHead++];
            if( tmpTxBuffHead == TCP_CLIENT_TX_BUF_SIZE ) {
               tmpTxBuffHead = 0;
            }
            --tmpTxBuffLen;
         }
         err = altcp_write(client->pcb, tmp, maxLen, TCP_WRITE_FLAG_COPY);
         client->waitingForAck = err == ERR_OK;
         altcp_output(client->pcb);
         if( err == ERR_OK ) {
            client->txBuffHead = tmpTxBuffHead;
            ints = save_and_disable_interrupts();
            client->txBuffLen = tmpTxBuffLen;
            restore_interrupts(ints);
#ifndef NDEBUG
         } else {
            lastTcpWriteErr = err;
#endif
         }
      }
   }
   return err;
}

static err_t tcpSent(void *arg, struct altcp_pcb *tpcb, u16_t len) {
   TCP_CLIENT_T *client = (TCP_CLIENT_T *)arg;
   err_t err = ERR_OK;
   
   if( client->txBuffLen ) {
      err = tcpSend(client);
   } else {
      client->waitingForAck = false;
   }
   return err;
}

// in the event that the altcp_write call in tcpSend failed earlier,
// and there weren't any other packets waiting to be ACKed, try
// sending any data in the txBuff again.
static err_t tcpPoll(void *arg, struct altcp_pcb *tpcb) {
   TCP_CLIENT_T *client = (TCP_CLIENT_T *)arg;
   err_t err = ERR_OK;
#ifndef NDEBUG
   static bool pollState = false;   
   
   gpio_put(POLL_STATE_LED, pollState);
   pollState = !pollState;
#endif
   if( !client->waitingForAck && client->txBuffLen ) {
      err = tcpSend(client);
   }
   return err;
}

static err_t tcpRecv(void *arg, struct altcp_pcb *tpcb, struct pbuf *p, err_t err) {
   TCP_CLIENT_T *client = (TCP_CLIENT_T *)arg;
   
   if( !p ) {
      return tcpClientClose(client);
   }
   if( p->tot_len > 0 && client ) {
      for( struct pbuf *q = p; q; q = q->next ) {
         for( int i = 0; i < q->len; ++i ) {
            client->rxBuff[client->rxBuffTail++] = ((uint8_t *)q->payload)[i];
            if( client->rxBuffTail == TCP_CLIENT_RX_BUF_SIZE ) {
               client->rxBuffTail = 0;
            }
            ++client->rxBuffLen;
         }
      }
#ifndef NDEBUG
      if( client->rxBuffLen > TCP_CLIENT_RX_BUF_SIZE ) {
         gpio_put(RXBUFF_OVFL, HIGH);
      }
      if( client->rxBuffLen > maxRxBuffLen ) {
         maxRxBuffLen = client->rxBuffLen;
      }
#endif
      if( client->rxBuffLen <= TCP_MSS ) {
         altcp_recved(client->pcb, p->tot_len);
      } else {
         client->totLen += p->tot_len;
#ifndef NDEBUG
         if( client->totLen > maxTotLen ) {
            maxTotLen = client->totLen;
         }
#endif
      }
   }
   pbuf_free(p);
   return ERR_OK;
}

static err_t tcpHasConnected(void *arg, struct altcp_pcb *tpcb, err_t err) {
   TCP_CLIENT_T *client = (TCP_CLIENT_T*)arg;
   
   client->connectFinished = true;
   client->connected = err == ERR_OK;
   if( err != ERR_OK ) {
      tcpClientClose(client);
   }
   return ERR_OK;
}

TCP_CLIENT_T *tcpConnect(TCP_CLIENT_T *client, const char *host, int portNum, bool secure) {
   if( !dnsLookup(host, &client->remoteAddr) ) {
      return NULL;
   } else {
      // The altcp allocator selects the transport: plain TCP, or a terminated
      // TLS session when the call is secure (dial prefix '#'). The shared client
      // TLS config carries NO CA chain in RAM. Trust is resolved per-handshake:
      // when verification is enabled (AT$CV1 + a bundle present) the lazyCaCb
      // callback streams the LittleFS bundle and parses one CA at a time, so RAM
      // stays flat regardless of bundle size (E-lazy; design §10). Authmode and
      // the CA callback are (re)applied per pcb in the SNI block below.
      altcp_allocator_t allocator;
      if( secure ) {
         static struct altcp_tls_config *tlsConfig = NULL;
         if( !tlsConfig ) {
            tlsConfig = altcp_tls_create_config_client(NULL, 0);
         }
         if( !tlsConfig ) {
            return NULL;
         }
         allocator.alloc = altcp_tls_alloc;
         allocator.arg = tlsConfig;
      } else {
         allocator.alloc = altcp_tcp_alloc;
         allocator.arg = NULL;
      }
      client->pcb = altcp_new_ip_type(&allocator, IP_GET_TYPE(client->remoteAddr));
      if( !client->pcb ) {
         return NULL;
      }
   }
#if LWIP_ALTCP_TLS_MBEDTLS
   if( secure ) {
      // Set the TLS SNI (Server Name Indication). Without it, CDN-fronted hosts
      // reject the handshake with a fatal alert, having no way to pick a cert.
      mbedtls_ssl_context *ssl = (mbedtls_ssl_context *)altcp_tls_context(client->pcb);
      if( ssl ) {
         mbedtls_ssl_set_hostname(ssl, host);
         // Per-call auth mode: REQUIRED aborts the handshake on an untrusted
         // cert (verify on + a bundle present), NONE accepts any peer (insecure).
         // In REQUIRED mode trust is resolved on demand by lazyCaCb (E-lazy):
         // the shared config holds no CA chain, the callback supplies the right
         // CA from the LittleFS bundle one at a time.
         if( ssl->conf ) {
            mbedtls_ssl_config *conf = (mbedtls_ssl_config *)ssl->conf;
            if( settings.tlsVerify && hasCACert() ) {
               mbedtls_ssl_conf_ca_cb(conf, lazyCaCb, NULL);
               mbedtls_ssl_conf_verify(conf, dateVerifyCb, NULL);  // date check (SNTP)
               mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_REQUIRED);
            } else {
               mbedtls_ssl_conf_authmode(conf, MBEDTLS_SSL_VERIFY_NONE);
            }
         }
      }
   }
#endif
   altcp_arg( client->pcb, client);
   altcp_recv(client->pcb, tcpRecv);
   altcp_sent(client->pcb, tcpSent);
   altcp_poll(client->pcb, tcpPoll, 2);
   altcp_err( client->pcb, tcpClientErr);
   altcp_nagle_disable(client->pcb);  // disable Nalge algorithm by default

   client->rxBuffLen = 0;
   client->rxBuffHead = 0;
   client->rxBuffTail = 0;
   client->totLen = 0;

   client->txBuffLen = 0;
   client->txBuffHead = 0;
   client->txBuffTail = 0;

   client->connected = false;
   client->connectFinished = false;
   client->waitingForAck = false;

   cyw43_arch_lwip_begin();
   err_t err = altcp_connect(client->pcb, &client->remoteAddr, portNum, tcpHasConnected);
   cyw43_arch_lwip_end();
   if( err != ERR_OK ) {
      client->pcb = NULL;
      return NULL;
   }

   while( client->pcb && client->pcb->arg && !client->connectFinished && !ser_is_readable(ser0)) {
      tight_loop_contents();
   }
   if( !client->connected ) {
      client->pcb = NULL;
      return NULL;
   }
   return client;
}

// NB: the PCB may have already been freed when this function is called
static void tcpServerErr(void *arg, err_t err) {
   TCP_SERVER_T *server = (TCP_SERVER_T *)arg;

   if( server ) {
      server->pcb = NULL;
      server->clientPcb = NULL;
   }
}

static err_t tcpServerAccept(void *arg, struct altcp_pcb *clientPcb, err_t err) {
   TCP_SERVER_T *server = (TCP_SERVER_T*)arg;

   if( err != ERR_OK || !clientPcb ) {
//###      printf("Failure in accept: %d\n",err); //###
      server->clientPcb = NULL;
      altcp_close(server->pcb);
      return ERR_VAL;
   }
//###   if( server->clientPcb ) {
//###      printf("Overwriting server->clientPcb\n");   //###
//###   }
   server->clientPcb = clientPcb;
   return ERR_OK;
}

bool tcpServerStart(TCP_SERVER_T *server, int portNum) {
   altcp_allocator_t allocator = { altcp_tcp_alloc, NULL };
   server->pcb = altcp_new_ip_type(&allocator, IPADDR_TYPE_ANY);
   if( !server->pcb ) {
      return false;
   }

   if( altcp_bind(server->pcb, NULL, portNum) != ERR_OK ) {
      return false;
   }

   server->clientPcb = NULL;

   struct altcp_pcb *pcb = altcp_listen_with_backlog(server->pcb, 1);
   if( !pcb ) {
      if( server->pcb ) {
         altcp_close(server->pcb);
         server->pcb = NULL;
      }
      return false;
   }
   server->pcb = pcb;

   altcp_arg(   server->pcb, server);
   altcp_accept(server->pcb, tcpServerAccept);
   altcp_err(   server->pcb, tcpServerErr);

   return true;
}

uint16_t tcpWriteBuf(TCP_CLIENT_T *client, const uint8_t *buf, uint16_t len) {
   uint32_t ints;
   
   if( client && client->pcb && client->pcb->arg ) {

      if( client->txBuffLen + len > TCP_CLIENT_TX_BUF_SIZE && client->connected ) {
#ifndef NDEBUG
         gpio_put(TXBUFF_OVFL, HIGH);
#endif
         while( client->txBuffLen + len > TCP_CLIENT_TX_BUF_SIZE && client->connected ) {
            tight_loop_contents();
         }
#ifndef NDEBUG
         gpio_put(TXBUFF_OVFL, LOW);
#endif
      }
      // lock out the lwIP thread now so that it can't end up calling
      // tcpSend until we're done with it... really don't want two
      // threads messing with txBuff at the same time.
      cyw43_arch_lwip_begin();
      for( uint16_t i = 0; i < len; ++i ) {
         client->txBuff[client->txBuffTail++] = buf[i];
         if( client->txBuffTail == TCP_CLIENT_TX_BUF_SIZE ) {
            client->txBuffTail = 0;
         }
         ints = save_and_disable_interrupts();
         ++client->txBuffLen;
         restore_interrupts(ints);
      }
#ifndef NDEBUG
      if( client->txBuffLen > maxTxBuffLen ) {
         maxTxBuffLen = client->txBuffLen;
      }
#endif
      if( client->txBuffLen && client->pcb && client->pcb->arg && !client->waitingForAck ) {
         tcpSend(client);
      }
      cyw43_arch_lwip_end();
      return len;
   }
   return 0;
}

uint16_t tcpWriteStr(TCP_CLIENT_T *client, const char *str) {
   return tcpWriteBuf(client, (uint8_t *)str, strlen(str));
}

uint16_t tcpWriteByte(TCP_CLIENT_T *client, uint8_t c) {
   return tcpWriteBuf(client, (uint8_t *)&c, 1);
}

uint16_t tcpBytesAvailable(TCP_CLIENT_T *client) {
   if( client ) {
      return client->rxBuffLen;
   }
   return 0;
}

int tcpReadByte(TCP_CLIENT_T *client, int rqstTimeout = -1) {
   int c;
   uint32_t timeout = 0;
   uint32_t ints;
   
   if( client ) {
      if( rqstTimeout > 0 ) {
         timeout = millis() + rqstTimeout;
      }
      do {
         if( client->rxBuffLen ) {
            c = client->rxBuff[client->rxBuffHead++];
            if( client->rxBuffHead == TCP_CLIENT_RX_BUF_SIZE ) {
               client->rxBuffHead = 0;
            }
            ints = save_and_disable_interrupts();
            --client->rxBuffLen;
            restore_interrupts(ints);
            if( !client->rxBuffLen && client->totLen && client->pcb) {
               cyw43_arch_lwip_begin();
               altcp_recved(client->pcb, client->totLen);
               client->totLen = 0;
               cyw43_arch_lwip_end();
            }
            return c;
         } else {
            tight_loop_contents();
         }
      } while( timeout > millis() );
   }
   return -1;
}

uint16_t tcpReadBytesUntil(TCP_CLIENT_T *client, uint8_t terminator, char *buf, uint16_t max_len) {
   char *p = buf;
   uint16_t c;
   
   uint32_t timeout = millis() + 1000;
   if( max_len > 0 ) {
      do {
         if( client->rxBuffLen ) {
            c = tcpReadByte(client);
            if( c != terminator ) {
               *p++ = (char)c;
               --max_len;
               timeout = millis() + 1000;
            } else {
               break;
            }
         } else {
            tight_loop_contents();
         }
      } while( max_len > 0 && timeout > millis() );
      return p - buf;
   }
   return 0;
}

void tcpTxFlush(TCP_CLIENT_T *client) {
   if( client ) {
      while( client->pcb && client->connected && client->txBuffLen ) {
         tight_loop_contents();
      }
   }
}

bool serverHasClient(TCP_SERVER_T *server) {
   return server->clientPcb != NULL;
}

TCP_CLIENT_T *serverGetClient(TCP_SERVER_T *server, TCP_CLIENT_T *client) {
   client->pcb = server->clientPcb;
   server->clientPcb = NULL;
   
   client->rxBuffLen = 0;
   client->rxBuffHead = 0;
   client->rxBuffTail = 0;
   client->totLen = 0;

   client->txBuffLen = 0;
   client->txBuffHead = 0;
   client->txBuffTail = 0;

   client->waitingForAck = false;

   altcp_arg( client->pcb, client);
   altcp_err( client->pcb, tcpClientErr);
   altcp_sent(client->pcb, tcpSent);
   altcp_poll(client->pcb, tcpPoll, 2);
   altcp_recv(client->pcb, tcpRecv);
   altcp_nagle_disable(client->pcb);  // disable Nalge algorithm by default

   client->connected = true;
   client->connectFinished = true;

   return client;
}
