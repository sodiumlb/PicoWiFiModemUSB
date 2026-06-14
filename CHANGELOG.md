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

### À venir
- Sprint 3 : vérification de certificat (bundle CA en LittleFS), SNI explicite,
  `ATGET https`, réduction RAM (`MBEDTLS_SSL_IN_CONTENT_LEN`), tests sur cible.
- R&D SSH : étude wolfSSH vs shim socket (bloqué par `LWIP_SOCKET=0`).
