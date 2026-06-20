# Changelog — PicoWiFiModemUSB (fork : proxy TLS/SSL)

Fork de travail pour ajouter une terminaison TLS/SSL (et R&D SSH) au modem.
Voir le document de conception : `../docs/design-proxy-tls-ssh.md`.

Format inspiré de [Keep a Changelog](https://keepachangelog.com/).

## [non publié]

### Sprint 1 — Migration de la pile TCP vers l'API `altcp` (sans TLS)

Refactor préparatoire, **sans changement de comportement** : la pile réseau passe
de l'API lwIP brute `tcp_*` à l'API « application-layered TCP » `altcp_*`, qui
permet d'ouvrir indifféremment une connexion TCP en clair (sprint 1) ou une
session TLS terminée (sprint TLS) derrière une interface unique.

**Ajouté**
- `src/lwipopts.h` : `LWIP_ALTCP=1` (active la couche altcp ; allocateur plain).

**Modifié**
- `src/types.h` : `TCP_CLIENT_T.pcb`, `TCP_SERVER_T.pcb`, `TCP_SERVER_T.clientPcb`
  passent de `struct tcp_pcb *` à `struct altcp_pcb *`. Inclusions
  `lwip/altcp.h` + `lwip/altcp_tcp.h`.
- `src/tcp_support.h` : tous les appels `tcp_*` → `altcp_*` (new_ip_type, arg,
  recv, sent, poll, err, nagle_disable, connect, write, output, close, abort,
  sndbuf, sndqueuelen, recved, bind, listen_with_backlog, accept). Signatures de
  callbacks (`struct tcp_pcb *` → `struct altcp_pcb *`). Accès `pcb->callback_arg`
  → `pcb->arg`. `tcpConnect()` et `tcpServerStart()` créent le PCB via un
  `altcp_allocator_t` plain (`altcp_tcp_alloc`) — point d'insertion balisé pour
  l'allocateur TLS du sprint suivant.
- `src/support.h` et `src/at_basic.h` : `&tcpClient->pcb->remote_ip` (champ absent
  de `struct altcp_pcb`) → `altcp_get_ip(tcpClient->pcb, 0)`.

**Build**
- Aucun changement de `CMakeLists.txt` requis : `altcp.c`/`altcp_alloc.c`/
  `altcp_tcp.c` sont déjà compilés par la lib `pico_lwip` du SDK (gardés par
  `#if LWIP_ALTCP`).

**Validation**
- ✅ Compilation OK (`arm-none-eabi-gcc` 14.2.1, `cmake -S src -B src/build`).
  Artefacts : `wifi_modem.elf` (~1,90 Mo) et `wifi_modem.uf2` (~796 Ko) — taille
  flash de référence pour mesurer le surcoût du sprint TLS.

### Sprint 2 — Terminaison TLS (mbedTLS via altcp_tls)

Le modem peut désormais **terminer une session TLS** vers le serveur distant et
présenter du clair côté série : l'Oric atteint des BBS/services chiffrés sans
faire de cryptographie. Mode **insecure** pour ce premier jet (handshake réussi
mais certificat **non vérifié**) ; la vérification CA est prévue au sprint 3.

**Ajouté**
- `src/mbedtls_config.h` : configuration mbedTLS 2.28 client (TLS 1.2, ECDHE/RSA,
  AES-GCM, SNI), entropie matérielle (`MBEDTLS_ENTROPY_HARDWARE_ALT` →
  `mbedtls_hardware_poll` du SDK), record de sortie plafonné à 2 Ko. Dérivée du
  modèle SDK `kitchen_sink`.
- `src/globals.h` : `bool sessionSecure` — marque la session courante comme TLS.
- Dial : préfixe **`#`** combinable (ex. `ATDT#bbs.example.com:992`, `ATDT#=host`)
  pour ouvrir la connexion en TLS.

**Modifié**
- `src/lwipopts.h` : `LWIP_ALTCP_TLS=1`, `LWIP_ALTCP_TLS_MBEDTLS=1`.
- `src/CMakeLists.txt` : lien `pico_lwip_mbedtls` + `pico_mbedtls` ;
  `MBEDTLS_CONFIG_FILE="mbedtls_config.h"`.
- `src/types.h` : include `lwip/altcp_tls.h`.
- `src/tcp_support.h` : `tcpConnect()` prend un paramètre `bool secure` ; en mode
  sécurisé, l'allocateur devient `altcp_tls_alloc` avec une config client
  partagée (`altcp_tls_create_config_client(NULL, 0)` → `VERIFY_OPTIONAL`, pas de
  CA). Sinon allocateur plain inchangé.
- `src/at_basic.h` : parsing du préfixe `#`, propagation de `sessionSecure` à
  l'appel de dial ; `ATGET` (HTTP) et la date NIST passent explicitement `false`.

**Empreinte (arm-none-eabi-size / uf2)**
- Flash : 777 Ko → **964 Ko** (+186 Ko mbedTLS) sur 2 Mo — confortable.
- RAM statique (bss) : **98 Ko** / 264 Ko — laisse ~166 Ko pour pile + heap +
  buffers TLS dynamiques du handshake. Une session TLS simultanée tient.

**Validation**
- ✅ Compilation OK (mbedTLS 2.28.1 compilée et liée : `ssl_cli`, `ssl_tls`,
  `x509_crt`…). Artefact `wifi_modem.uf2`.
- ⚠️ **Runtime non testé** : pas de matériel Pico W dans cet environnement. Le
  handshake TLS réel (et le comportement en `threadsafe_background`) reste à
  vérifier sur cible.

### Sprint 2.1 — Validation runtime sous Wokwi + corrections TLS

Test du firmware dans l'émulateur **Wokwi** (Pico W simulé, WiFi + accès Internet
via la gateway). Le harnais a révélé et permis de corriger trois bugs TLS qui
auraient aussi bloqué sur matériel réel.

**Corrigé (essentiel au TLS)**
- `src/tcp_support.h` : **SNI** ajouté (`mbedtls_ssl_set_hostname` sur le contexte
  obtenu via `altcp_tls_context`). Sans SNI, les hôtes derrière un CDN rejettent
  le handshake par une alerte fatale (`-0x7780`).
- `src/lwipopts.h` : `TCP_WND` porté à `12*MSS` (≥ 16 Ko). Un `TCP_WND` plus
  petit que le buffer de déchiffrement TLS fait staller la réception d'un gros
  record (chaîne de certificats). `PBUF_POOL_SIZE` 24→32, `MEM_SIZE` 4000→16384.
- `src/lwipopts.h` : `ALTCP_MBEDTLS_AUTHMODE = 0` (`MBEDTLS_SSL_VERIFY_NONE`).
  En mode insecure sans CA, `VERIFY_OPTIONAL` faisait quand même échouer le
  handshake (callback de vérif d'altcp + hostname). `NONE` complète le handshake.
  La vérification réelle (avec CA) reste prévue au sprint 3.

**Ajouté — variant de test Wokwi** (option CMake `-DWOKWI=1`)
- `src/CMakeLists.txt` : option `WOKWI` ; backend série `ser_uart.c` (console
  UART0) au lieu de `ser_cdc.c` ; macro `WOKWI_BUILD`.
- `src/wifi_modem.cpp` (sous `#ifdef WOKWI_BUILD`) : saut de l'attente d'hôte USB
  (`while(!tud_ready())`, qui bloque sans hôte USB dans la sim) ; SSID préconfiguré
  `Wokwi-GUEST` en auth `OPEN` ; console 115200.
- Harnais hors firmware : `../wokwi/` (diagram `board-pi-pico-w`, `wokwi.toml`),
  piloté par `wokwi-cli --interactive`.

**Résultat de validation**
- ✅ Boot, WiFi (CYW43 simulé), DHCP, **HTTP réel** (`ATGET` → réponse Cloudflare).
- ✅ Le firmware exécute un **handshake TLS client complet et correct** :
  ClientHello → réception/traitement des certificats serveur → ClientKeyExchange
  → ChangeCipherSpec → **Finished envoyé** (mesuré à ~8 s sur RP2040 *émulé*).
- ⚠️ Complétion non atteinte **sous Wokwi uniquement** : la lenteur du crypto
  émulé (~8 s, vs ~1-2 s sur silicium) dépasse le timeout de handshake des
  serveurs, et la Public Gateway proxifie le TLS (MITM). Ces deux causes sont des
  artefacts de l'émulateur, pas du firmware. La complétion `CONNECT` reste à
  confirmer sur **matériel réel**.

### Sprint 3 — HTTPS, vérification de certificat (CA via LittleFS), tuning RAM

**Ajouté — `ATGET https`**
- `src/at_basic.h` : `httpGet()` détecte le schéma `https://` (port 443 par
  défaut, dial TLS) en plus de `http://` ; parsing host/port/chemin robuste.
- `src/wifi_modem.h` : `HTTPS_PORT 443`.

**Ajouté — vérification de certificat (CA via LittleFS + commandes AT)**
- `src/types.h` : `SETTINGS_T.tlsVerify` ; `MAGIC_NUMBER` 0x5678→0x5679 (la
  taille de la struct change → re-`factoryDefaults` au boot). Défaut : `false`.
- `src/lfs.c` / `lfs.h` : CA stocké en fichier LittleFS `ca.pem`
  (`hasCACert`, `readCACert`, `writeCACert`, `deleteCACert`, `caCertSize`). Le CA
  est provisionné hors-bande dans l'image LittleFS (pas de saisie AT multi-ligne).
- `src/tcp_support.h` : `tcpConnect` vérifie le certificat quand
  `tlsVerify && hasCACert()` — config client créée avec le CA et
  `MBEDTLS_SSL_VERIFY_REQUIRED` ; sinon `NULL`/`VERIFY_NONE` (insecure). La config
  partagée est reconstruite quand le mode change ; l'authmode est aussi forcé par
  appel sur le contexte SSL.
- `src/at_proprietary.h` + `wifi_modem.cpp` : commandes `AT$CV?/0/1` (activer la
  vérif) et `AT$CA?/-` (taille du CA / suppression).

**Ajouté — tuning RAM d'une session TLS**
- `src/mbedtls_config.h` : `MBEDTLS_SSL_IN_CONTENT_LEN 8192` (record d'entrée
  plafonné à 8 Ko au lieu de 16 Ko ; un pair émettant un record > 8 Ko échouerait).
- `src/lwipopts.h` : `TCP_WND` 12*MSS → 6*MSS (≥ buffer de déchiffrement), RAM
  récupérée côté lwIP.

**Validation**
- ✅ Build prod + variant Wokwi OK. uf2 prod ~990 Ko ; bss ~126 Ko / 264 Ko
  (laisse ~135 Ko pour heap + pile + session TLS).
- ⚠️ **Runtime non testé** (Wokwi trop lent pour compléter un handshake, pas de
  matériel). La vérification de certificat reste à valider sur cible réelle.

### Correctif — deadlock USB CDC au démarrage (`startupWait`)

Premier test sur **cible matérielle réelle** (Pico W, USB CDC). Le firmware
restait muet sur `/dev/ttyACM0` : aucune réponse AT, et l'écriture hôte
(`tcdrain`) bloquait — symptôme d'un `tud_task()` qui ne tourne jamais.

**Cause**
- `src/wifi_modem.cpp`, boucle d'attente du CR quand `settings.startupWait` est
  vrai (`while(true)`) : elle interrogeait le CDC via `ser_is_readable()`/
  `ser_getc()` **sans jamais appeler `tud_task()`**. La pile TinyUSB n'étant plus
  pompée, l'endpoint OUT n'est jamais traité → le CR ne peut jamais être reçu →
  deadlock total (USB figé, ni RX ni TX applicatifs). Déclenché par un
  `startupWait=true` hérité de la config persistante LittleFS (non effacée par un
  reflash `.uf2`).

**Modifié**
- `src/wifi_modem.cpp` : la boucle `startupWait` appelle désormais `tud_task()` et
  `cdc_task()` à chaque itération (gardé par `#ifndef WOKWI_BUILD`), comme la
  boucle principale `loop()`.

**Validation**
- ✅ Build prod OK ; flashé sur Pico W (`cafe:4001`, `/dev/ttyACM0`).
- ✅ Runtime confirmé sur matériel : `AT`→`OK`, `ATI` (infos réseau complètes),
  `AT$CV?`→`0`, `AT$CA?`→`CA: 0 bytes`. Dialogue stable (866 o sur 40 s).
  **Première validation runtime réelle de la pile (TLS incluse) sur cible.**

### À venir
- Test sur cible matérielle (Pico W + LOCI) : confirmer le `CONNECT` TLS, la
  vérification CA stricte, et l'`ATGET https`.
- R&D SSH : étude wolfSSH vs shim socket (bloqué par `LWIP_SOCKET=0`).
