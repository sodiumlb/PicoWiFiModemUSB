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

**Build**
- Aucun changement de `CMakeLists.txt` requis : `altcp.c`/`altcp_alloc.c`/
  `altcp_tcp.c` sont déjà compilés par la lib `pico_lwip` du SDK (gardés par
  `#if LWIP_ALTCP`).

**Validation**
- ⚠️ Compilation non vérifiée dans cet environnement : la toolchain
  `arm-none-eabi-gcc` n'est pas installée (installation soumise à autorisation
  sudo). Vérification statique effectuée : inventaire des appels `altcp_*` croisé
  avec les signatures du SDK (`lwip/altcp.h`, `lwip/altcp_tcp.h`), aucune
  occurrence `tcp_*` minuscule résiduelle, macros `TCP_*` préservées.

### À venir
- Sprint TLS : `mbedtls_config.h`, `LWIP_ALTCP_TLS*`, lien `pico_lwip_mbedtls`,
  branche TLS dans `tcpConnect`, commande AT de dial sécurisé, budget RAM mesuré.
- R&D SSH : étude wolfSSH vs shim socket (bloqué par `LWIP_SOCKET=0`).
