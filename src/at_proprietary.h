//
// AT$AE? query auto execute command string
// AT$AE=auto execute command string
//
char *doAutoExecute(char *atCmd) {
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("%s\r\n", settings.autoExecute);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '=':
         ++atCmd;
         strncpy(settings.autoExecute, atCmd, MAX_AUTOEXEC_LEN);
         settings.busyMsg[MAX_AUTOEXEC_LEN] = NUL;
         atCmd[0] = NUL;
         sendResult(R_OK);
         break;
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$AYT send "Are you there?" if in a Telnet session
//
char *doAreYouThere(char *atCmd) {
   
   static const uint8_t areYouThere[] = {IAC, AYT};
   
   if( tcpIsConnected(tcpClient) && settings.telnet != NO_TELNET ) {
      state = ONLINE;
      dtrWentInactive = false;
      bytesOut += tcpWriteBuf(tcpClient, areYouThere, sizeof areYouThere);
   } else {
      sendResult(R_ERROR);
   }
   return atCmd;
}

//
// AT$BM?  query busy message
// AT$BM=busy message set busy message
//
char *doBusyMessage(char *atCmd) {
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("%s\r\n", settings.busyMsg);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '=':
         ++atCmd;
         strncpy(settings.busyMsg, atCmd, MAX_BUSYMSG_LEN);
         settings.busyMsg[MAX_BUSYMSG_LEN] = NUL;
         atCmd[0] = NUL;
         sendResult(R_OK);
         break;
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$MDNS? query mDNS network name
// AT$MDNS=mdnsname set mDNS network name
//
char *doMdnsName(char *atCmd) {
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("%s\r\n", settings.mdnsName);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;

      case '=':
         ++atCmd;
         strncpy(settings.mdnsName , atCmd, MAX_MDNSNAME_LEN);
         settings.mdnsName[MAX_MDNSNAME_LEN] = NUL;
         atCmd[0] = NUL;
         sendResult(R_OK);
         break;

      default:
         sendResult(R_ERROR);
         lastCmd[0] = NUL;
         break;
   }
   return atCmd;
}

//
// AT$PASS? query WiFi password
// AT$PASS=password set WiFi password
//
char *doWiFiPassword(char *atCmd) {
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("%s\r\n", settings.wifiPassword);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '=':
         ++atCmd;
         strncpy(settings.wifiPassword, atCmd, MAX_WIFI_PWD_LEN);
         settings.wifiPassword[MAX_WIFI_PWD_LEN] = NUL;
         atCmd[0] = NUL;
         sendResult(R_OK);
         break;
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$TIME?          query current UTC time (and sync source)
// AT$TIME=<epoch>   force the clock (UNIX epoch, seconds) — offline fallback
//
char *doTime(char *atCmd) {
   switch( atCmd[0] ) {
      case '?': {
         ++atCmd;
         time_t utc = modemNow();
         time_t loc = utc + (time_t)settings.tzOffsetMin * 60;   // local time (display)
         struct tm tmv;
         gmtime_r(&loc, &tmv);
         char buf[32];
         strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmv);
         int off = settings.tzOffsetMin;
         printf("%s UTC%+03d:%02d (epoch %lu, %s)\r\n",
                buf, off / 60, (off < 0 ? -off : off) % 60,
                (unsigned long)utc, timeSynced ? "synced" : "build-fallback");
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      }
      case '=': {
         ++atCmd;
         unsigned long epoch = strtoul(atCmd, NULL, 10);
         atCmd[0] = NUL;
         if( epoch > 0 ) {
            sntpSetSystemTime(epoch);
            sendResult(R_OK);
         } else {
            sendResult(R_ERROR);
         }
         break;
      }
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$TZ?            query timezone offset (display only; cert checks stay in UTC)
// AT$TZ=±H[:MM]     set timezone offset, e.g. +2, -8, +05:30 (range ±14:00)
//
char *doTimeZone(char *atCmd) {
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("UTC%+03d:%02d\r\n", settings.tzOffsetMin / 60,
                (settings.tzOffsetMin < 0 ? -settings.tzOffsetMin : settings.tzOffsetMin) % 60);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '=': {
         ++atCmd;
         const char *p = atCmd;
         int sign = 1;
         if( *p == '+' ) { ++p; }
         else if( *p == '-' ) { sign = -1; ++p; }
         int hh = atoi(p);
         int mm = 0;
         const char *colon = strchr(p, ':');
         if( colon ) mm = atoi(colon + 1);
         int off = sign * (hh * 60 + mm);
         atCmd[0] = NUL;
         if( off >= -14 * 60 && off <= 14 * 60 ) {   // bounds UTC-14..+14
            settings.tzOffsetMin = off;
            sendResult(R_OK);
         } else {
            sendResult(R_ERROR);
         }
         break;
      }
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$SB?  query serial speed
// AT$SB=n set serial speed
//
char *doSpeedChange(char *atCmd) {
   long newSerialSpeed;

   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("%u\r\n", settings.serialSpeed);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '=':
         ++atCmd;
         newSerialSpeed = atol(atCmd);
         while( isdigit(*atCmd) ) {
            ++atCmd;
         }
         if( newSerialSpeed != settings.serialSpeed ) {
            switch( newSerialSpeed ) {
               case 110L:                       // 110 thru 76.8K are the
               case 300L:                       // standard 'BYE' rates, if
               case 450L:                       // you're wondering why
               case 600L:                       // unusual rates like 110, 450
               case 710L:                       // and 710 are in this list
               case 1200L:
               case 2400L:
               case 4800L:
               case 9600L:
               case 19200L:
               case 38400L:
               case 57600L:
               case 76800L:
               case 115200L:
                  sendResult(R_OK);
                  ser_tx_wait_blocking(ser0);  // wait for transmit to finish
                  ser_set_baudrate(ser0, newSerialSpeed);
                  settings.serialSpeed = newSerialSpeed;
                  break;

               default:
                  sendResult(R_ERROR);
                  break;
            }
         } else {
            sendResult(R_OK);
         }
         break;
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$SP?  query inbound TCP port #
// AT$SP=n set inbound TCP port #
//         NOTE: n=0 will disable the inbound TCP port
//               and a RING message will never be displayed
//
char *doServerPort(char *atCmd) {
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("%u\r\n", settings.listenPort);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '=':
         ++atCmd;
         settings.listenPort = atoi(atCmd);
         while( isdigit(atCmd[0]) ) {
            ++atCmd;
         }
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$SSID? query WiFi SSID
// AT$SSID=ssid set WiFi SSID
//
char *doSSID(char *atCmd) {
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("%s\r\n", settings.ssid);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '=':
         ++atCmd;
         strncpy(settings.ssid, atCmd, MAX_SSID_LEN);
         settings.ssid[MAX_SSID_LEN] = NUL;
         atCmd[0] = NUL;
         sendResult(R_OK);
         break;
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$SCAN scan for nearby WiFi networks (2.4 GHz)
//
// Lists one access point per line as "<index> <ssid><TAB><sec>", de-duplicated
// by SSID (keeping the strongest signal) and terminated by OK. <sec> is a
// single char: 'O' = open (no password), 'S' = secured. The TAB separator keeps
// it backward-compatible: a host that ignores everything after the SSID still
// works. The index is 1-based so a host program can present a numbered menu and
// feed the chosen SSID back via AT$SSID=. Hidden (empty-SSID) networks skipped.
//
#define SCAN_MAX_APS 24

static char    scanList[SCAN_MAX_APS][MAX_SSID_LEN + 1];
static int16_t scanRssi[SCAN_MAX_APS];
static bool    scanOpen[SCAN_MAX_APS];   // true = open network (auth_mode 0)
static int     scanNum;

static int scanResult(void *env, const cyw43_ev_scan_result_t *result) {
   (void)env;
   if( result && result->ssid_len > 0 && scanNum < SCAN_MAX_APS ) {
      char ssid[MAX_SSID_LEN + 1];
      int n = result->ssid_len > MAX_SSID_LEN ? MAX_SSID_LEN : result->ssid_len;
      memcpy(ssid, result->ssid, n);
      ssid[n] = NUL;
      for( int i = 0; i < scanNum; ++i ) {      // de-duplicate by SSID
         if( !strcmp(scanList[i], ssid) ) {
            if( result->rssi > scanRssi[i] ) scanRssi[i] = result->rssi;
            return 0;
         }
      }
      strcpy(scanList[scanNum], ssid);
      scanRssi[scanNum] = result->rssi;
      scanOpen[scanNum] = (result->auth_mode == 0);   // 0 = open
      ++scanNum;
   }
   return 0;
}

char *doScan(char *atCmd) {
   cyw43_wifi_scan_options_t opts;
   memset(&opts, 0, sizeof opts);
   scanNum = 0;
   if( cyw43_wifi_scan(&cyw43_state, &opts, NULL, scanResult) != 0 ) {
      sendResult(R_ERROR);
      return atCmd;
   }
   absolute_time_t deadline = make_timeout_time_ms(15000);
   while( cyw43_wifi_scan_active(&cyw43_state) && !time_reached(deadline) ) {
#ifndef WOKWI_BUILD
      // Keep the USB CDC alive during the scan wait, otherwise output is
      // frozen for the whole scan (same deadlock class as the ATC1 fix).
      tud_task();
      cdc_task();
#else
      sleep_ms(20);
#endif
   }
   for( int i = 0; i < scanNum; ++i ) {
      printf("%d %s\t%c\r\n", i + 1, scanList[i], scanOpen[i] ? 'O' : 'S');
#ifndef WOKWI_BUILD
      tud_task();   // drain TX between lines so a long list isn't truncated
      cdc_task();
#endif
   }
   sendResult(R_OK);
   return atCmd;
}

//
// AT$SU? query data configuration
// AT$SU=nps set data configuration
//       n=7/8   data bits
//       p=N/E/O parity
//       s=1/2   stop bits
//
char *doDataConfig(char *atCmd) {
   bool ok = true;
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("%u", settings.dataBits);
         switch( settings.parity ) {
            case UART_PARITY_NONE:
               ser_putc(ser0, 'N');
               break;
            case UART_PARITY_ODD:
               ser_putc(ser0, 'O');
               break;
            case UART_PARITY_EVEN:
               ser_putc(ser0, 'E');
               break;
            default:
               ser_putc(ser0, '?');
               break;
         }
         printf("%u\r\n", settings.stopBits);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '=':
         switch( atCmd[1] ) {
            case '5':
            case '6':
            case '7':
            case '8':
               break;
            default:
               ok = false;
               break;
         }
         switch( toupper(atCmd[2]) ) {
            case 'N':
            case 'O':
            case 'E':
               break;
            default:
               ok = false;
               break;
         }
         switch( atCmd[3] ) {
            case '1':
            case '2':
               break;
            default:
               ok = false;
               break;
         }
         if( ok ) {
            settings.dataBits = atCmd[1] - '0';
            switch( toupper(atCmd[2]) ) {
               case 'N':
                  settings.parity = UART_PARITY_NONE;
                  break;
               case 'O':
                  settings.parity = UART_PARITY_ODD;
                  break;
               case 'E':
                  settings.parity = UART_PARITY_EVEN;
                  break;
            }
            settings.stopBits = atCmd[3] - '0';
            atCmd += 4;    // skip over =dps
            if( !atCmd[0] ) {
               sendResult(R_OK);
            }
         } else {
            sendResult(R_ERROR);
         }
         break;
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$TTL? query Telnet location
// AT$TTL=location set Telnet location
//
char *doLocation(char *atCmd) {
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("%s\r\n", settings.location);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '=':
         ++atCmd;
         strncpy(settings.location, atCmd, MAX_LOCATION_LEN);
         settings.location[MAX_LOCATION_LEN] = NUL;
         atCmd[0] = NUL;
         sendResult(R_OK);
         break;
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$TTS? query Telnet window size
// AT$TTS=WxH set Telnet window size (width x height)
//
char *doWindowSize(char *atCmd) {
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("%ux%u\r\n", settings.width, settings.height);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '=':
         {
            char *width = atCmd + 1;
            char *height = strpbrk(width, "xX");
            if( !width || !height ) {
               sendResult(R_ERROR);
            } else {
               ++height; // point to 1st char past X
               settings.width = atoi(width);
               settings.height = atoi(height);
               atCmd = height;
               while( isdigit(atCmd[0]) ) {
                  ++atCmd;
               }
               if( !atCmd[0] ) {
                  sendResult(R_OK);
               }
            }
         }
         break;
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$TTY? query Telnet terminal type
// AT$TTY=terminal set Telnet terminal type
//
char *doTerminalType(char *atCmd) {
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("%s\r\n", settings.terminal);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '=':
         ++atCmd;
         strncpy(settings.terminal, atCmd, MAX_TERMINAL_LEN);
         settings.location[MAX_TERMINAL_LEN] = NUL;
         atCmd[0] = NUL;
         sendResult(R_OK);
         break;
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$W? query startup wait status
// AT$W=0 disable startup wait
// AT$W=1 enable startup wait (wait for a CR on startup)
//
char *doStartupWait(char *atCmd) {
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("%u\r\n", settings.startupWait);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '=':
         ++atCmd;
         switch( atCmd[0] ) {
            case '0':
            case '1':
               settings.startupWait = atCmd[0] == '1';
               atCmd[0] = NUL;
               sendResult(R_OK);
               break;
            default:
               sendResult(R_ERROR);
               break;
         }
         break;
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$CV? query TLS certificate verification (0 = off/insecure, 1 = on)
// AT$CV0 disable certificate verification (insecure: accept any server cert)
// AT$CV1 enable certificate verification (ERROR if no CA stored in LittleFS)
//
char *doCertVerify(char *atCmd) {
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("%u\r\n", settings.tlsVerify);
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '0':
         settings.tlsVerify = false;
         ++atCmd;
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '1':
         ++atCmd;
         // Refuse to enable verification without a stored CA: otherwise it would
         // be silently insecure (no CA => VERIFY_NONE accepts any cert), giving a
         // false sense of security. Load a CA first with AT$CA=.
         if( !hasCACert() ) {
            sendResult(R_ERROR);
         } else {
            settings.tlsVerify = true;
            if( !atCmd[0] ) {
               sendResult(R_OK);
            }
         }
         break;
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}

//
// AT$CA? report the stored TLS CA size in bytes (0 = none)
// AT$CA= upload a CA certificate (PEM); end with a line containing only '.'
// AT$CA- delete the stored CA
//
char *doCACert(char *atCmd) {
   switch( atCmd[0] ) {
      case '?':
         ++atCmd;
         printf("CA: %d bytes\r\n", caCertSize());
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      case '=': {
         // Upload a CA in PEM form: read from the serial port until a line
         // containing only '.' is received, then store it via writeCACert().
         // The USB stack is pumped during the (blocking) read so the CDC stays
         // alive (same rationale as the startupWait loop fix).
         ++atCmd;
         static char pem[4096];
         size_t len = 0;            // bytes accumulated in pem (excl. NUL)
         size_t lineLen = 0;        // chars on the current line (excl. CR/LF)
         char   firstOnLine = 0;    // first char of the current line
         bool   done = false, overflow = false;
         size_t overflowDrain = 0;  // bounds the wait once we stop storing
         ser_puts(ser0, "\r\nSend CA in PEM; end with a line containing only '.'\r\n");
         // Read until the lone '.' terminator. On overflow we KEEP draining the
         // stream (so the leftover PEM is not parsed as AT commands) but stop
         // storing, and report ERROR instead of a truncated CA with a false OK.
         while( !done ) {
#ifndef WOKWI_BUILD
            tud_task();
            cdc_task();
#endif
            if( !ser_is_readable(ser0) ) {
               continue;
            }
            char c = ser_getc(ser0);
            if( c == '\r' ) {
               continue;               // segment on LF only
            }
            if( c == '\n' ) {
               if( lineLen == 1 && firstOnLine == '.' ) {
                  if( !overflow && len > 0 ) len--;   // drop the lone '.'
                  done = true;
               } else if( !overflow ) {
                  if( len < sizeof(pem) - 1 ) pem[len++] = '\n';   // keep the newline
                  else overflow = true;
               }
               lineLen = 0; firstOnLine = 0;
            } else {
               if( lineLen == 0 ) firstOnLine = c;
               lineLen++;
               if( !overflow ) {
                  if( len < sizeof(pem) - 1 ) pem[len++] = c;
                  else overflow = true;
               }
            }
            // Safety: don't wait forever for the terminator from a runaway sender.
            if( overflow && ++overflowDrain > 4 * sizeof(pem) ) {
               done = true;
            }
         }
         pem[len] = NUL;
         if( !overflow && len > 0 && writeCACert(pem, len) ) {
            printf("CA stored: %u bytes\r\n", (unsigned)len);
            sendResult(R_OK);
         } else {
            if( overflow ) {
               printf("CA too large (max %u bytes); not stored\r\n",
                      (unsigned)(sizeof(pem) - 1));
            }
            sendResult(R_ERROR);
         }
         break;
      }
      case '-':
         ++atCmd;
         deleteCACert();
         if( !atCmd[0] ) {
            sendResult(R_OK);
         }
         break;
      default:
         sendResult(R_ERROR);
         break;
   }
   return atCmd;
}
