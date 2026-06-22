# Changelog — PicoWiFiModemUSB (fork : proxy TLS/SSL)

Fork de travail pour ajouter une terminaison TLS/SSL (et R&D SSH) au modem.
Voir le document de conception : `../docs/design-proxy-tls-ssh.md`.

Format inspiré de [Keep a Changelog](https://keepachangelog.com/).

## [0.2.1] — 2026-06-22 — Vérification CA « lazy » (E-lazy)

Vérification de certificat contre un **bundle multi-CA** stocké en LittleFS, **sans**
charger tout le magasin en RAM (impossible sur RP2040 : ~200–400 Ko pour le bundle
Mozilla parsé). Conception détaillée : `../docs/design-proxy-tls-ssh.md` §10.

**Ajouté**
- `src/mbedtls_config.h` : `MBEDTLS_X509_TRUSTED_CERTIFICATE_CALLBACK` (active
  `mbedtls_ssl_conf_ca_cb`).
- `src/lfs.c` + `src/lfs.h` : curseur de lecture en flux sur `ca.pem`
  (`caBundleOpen/Read/Close`) — permet de scanner un bundle PEM multi-certificats
  un bloc à la fois, sans le charger entièrement.
- `src/tcp_support.h` : `caLazyNextBlock()` (segmente les blocs `BEGIN…END
  CERTIFICATE`) et `lazyCaCb()` (callback de confiance mbedTLS : parse **un** CA à
  la fois et ne retient que celui dont le `subject` DER == `issuer` recherché). Pic
  RAM ≈ 1 certificat, indépendant de la taille du bundle.
- `src/wifi_modem.h` : `FW_VERSION "0.2.1"`, affichée par `ATI`
  (« Pico WiFi modem v0.2.1 »).

**Modifié**
- `src/tcp_support.h` `tcpConnect()` : la config TLS client ne porte plus de chaîne
  CA en RAM (`altcp_tls_create_config_client(NULL, 0)`). En mode vérifié (`AT$CV1` +
  bundle présent) on pose `mbedtls_ssl_conf_ca_cb(conf, lazyCaCb, NULL)` +
  `authmode REQUIRED` par session ; sinon `authmode NONE`. Supprime l'ancien
  chargement `readCACert()`→`caBuf[4096]` en RAM et la reconstruction de config.

**Corrigé**
- `src/at_proprietary.h` `AT$CA=` : **fin de la troncature silencieuse** (backlog
  design §9). En cas de PEM dépassant le tampon (4096 o), la commande ne stocke plus
  un certificat tronqué avec un faux `OK` : elle draine le flux jusqu'au terminateur
  `.` (pour ne pas réinjecter le reste dans le parseur AT), affiche
  `CA too large (max 4095 bytes); not stored` et renvoie `ERROR`. Garde-fou bornant
  l'attente du terminateur (`4 × sizeof(pem)`).

**Compatibilité**
- `ca.pem` accepte désormais **plusieurs** certificats PEM concaténés. Un CA unique
  reste valide (rétro-compatible). L'upload `AT$CA=` reste plafonné à 4096 o ; un
  bundle volumineux se provisionne par image LittleFS.

**Validation**
- ✅ Compilation `arm-none-eabi-gcc` 14.2.1 / `cmake --build` OK :
  `wifi_modem.uf2` (~998 Ko), `text` ≈ 487 Ko, `bss` ≈ 130 Ko. Symboles
  `lazyCaCb`/`caBundle*` présents.
- ✅ **Validé sur matériel (2026-06-22)** — Pico W réel, bundle multi-CA (Amazon leurre
  + ISRG Root X1) via `AT$CA=` : `badssl.com` → `CONNECT` (ISRG trouvé après le leurre,
  scan lazy confirmé), `untrusted-root.badssl.com` (CA absent) → `NO CARRIER`,
  `expired.badssl.com` → `NO CARRIER`. Correctif `AT$CA=` : PEM > 4096 o → `ERROR` sans
  troncature, CA préservé. Banc de test : `../validation/` (rapport + `validate.py`).

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

### Ajout — chargement d'un CA par série (`AT$CA=`)

La vérification de certificat (`AT$CV1`) exige un CA en LittleFS, mais aucune
commande ne permettait d'en provisionner un (`writeCACert()` existait sans être
câblée). Ajout de la commande d'upload.

**Ajouté**
- `src/at_proprietary.h` : `doCACert()` gère `AT$CA=` — lit un certificat PEM
  depuis le port jusqu'à une ligne ne contenant que `.`, puis le stocke via
  `writeCACert()`. La pile USB (`tud_task()`/`cdc_task()`) est pompée pendant la
  lecture bloquante (même raison que le correctif `startupWait`).

**Validation**
- ✅ Sur matériel : `AT$CA=` (ISRG Root X1) → `CA stored: 1939 bytes`,
  `AT$CA?` → `1939 bytes`.

### Correctif — vérification de certificat inopérante (`MBEDTLS_PEM_PARSE_C` manquant)

Premier test matériel de `AT$CV1` : **toute** connexion en mode vérification
échouait (`NO CARRIER`), y compris vers des sites parfaitement valides.

**Cause**
- `src/mbedtls_config.h` n'activait pas `MBEDTLS_PEM_PARSE_C` :
  `mbedtls_x509_crt_parse()` n'accepte alors que du DER. Les certificats reçus
  du serveur pendant le handshake sont en DER (donc `AT$CV0` fonctionnait), mais
  le CA local chargé via `AT$CA=` est en **PEM** → son parsing échouait →
  `altcp_tls_create_config_client()` renvoyait `NULL` → `tcpConnect()` renvoyait
  `NULL` → `NO CARRIER` pour toute connexion sécurisée. La vérification ne
  rejetait donc rien : la config TLS ne se créait simplement jamais.

**Modifié**
- `src/mbedtls_config.h` : `#define MBEDTLS_PEM_PARSE_C` + `MBEDTLS_BASE64_C`
  (dépendance). uf2 990 Ko → 994 Ko.

**Validation** (matériel, CA = ISRG Root X1, `AT$CV1`)
- ✅ `www.badssl.com`, `sha256.badssl.com`, `badssl.com` (apex, couvert par le
  SAN) → **CONNECT**.
- ✅ `wrong.host.badssl.com` (hors wildcard) → **refusé** (hostname).
- ✅ `expired` / `self-signed` / `untrusted-root.badssl.com` → **refusés**.
- Première validation matérielle complète de la vérification de certificat.

### Correctif — CDC figé pendant la connexion WiFi (`ATC1`)

Même classe de bug que `startupWait` : la boucle d'attente de `ATC1` bloquait sur
`sleep_ms(500)` (jusqu'à 25 s) sans pomper la pile USB → le CDC gelait pendant
toute la tentative de connexion (écriture hôte en timeout).

**Modifié**
- `src/at_basic.h` : `wifiConnection()` — le `sleep_ms(500)` de la boucle d'attente
  est remplacé (hors `WOKWI_BUILD`) par une attente active de 500 ms qui appelle
  `tud_task()`/`cdc_task()`, gardant le CDC vivant. `sleep_ms()` conservé pour la
  variante Wokwi (UART, pas de CDC).

**Validation** (matériel)
- ✅ Pendant `ATC1` : 26/27 écritures hôte passent (auparavant : timeout
  systématique), progression `......` reçue en temps réel, connexion OK. Le
  timeout résiduel unique vient de `cyw43_arch_wifi_connect_async()` (hors boucle).

### Durcissement — `AT$CV1` refusé sans CA + restauration de `AT$CA-`

**Modifié**
- `src/at_proprietary.h`, `doCertVerify()` : `AT$CV1` renvoie désormais `ERROR`
  (et n'active pas `tlsVerify`) tant qu'aucun CA n'est stocké. Sans CA, la
  vérification retombait silencieusement en `VERIFY_NONE` (tout cert accepté) :
  un faux sentiment de sécurité. `AT$CV0` reste accepté en toute circonstance.

**Correctif (régression)**
- `src/at_proprietary.h`, `doCACert()` : restauration du `case '-'` (suppression
  du CA), supprimé par mégarde lors de l'ajout de `AT$CA=`. `AT$CA-` retombait
  dans `default` → `ERROR` et ne supprimait rien.

**Validation** (matériel)
- ✅ `AT$CA-` → `OK`, `AT$CA?` → `0 bytes`.
- ✅ `AT$CV1` sans CA → `ERROR`, `AT$CV?` → `0`.
- ✅ Après rechargement du CA : `AT$CV1` → `OK`, `AT$CV?` → `1`.

### Ajout — `AT$SCAN` : scan des réseaux WiFi

Pour permettre la **configuration du WiFi depuis l'Oric** (menu de réseaux plutôt
que saisie aveugle du SSID), ajout d'une commande de scan absente du firmware
amont.

**Ajouté**
- `src/at_proprietary.h` : `doScan()` — déclenche `cyw43_wifi_scan()`, collecte les
  résultats via callback (dédup par SSID en gardant le meilleur RSSI, SSID cachés
  ignorés, max 24 AP), puis imprime **un AP par ligne** au format `<index> <ssid>`
  (1-based), terminé par `OK` (`ERROR` si le scan échoue). Attente active de fin de
  scan via `cyw43_wifi_scan_active()` (timeout 15 s). Format volontairement trivial
  à parser côté Oric (entier de tête = choix, reste = SSID à renvoyer en `AT$SSID=`).
- `src/wifi_modem.cpp` : branche de dispatch `$SCAN` (placée avant `$SSID` ;
  `strncasecmp` sur 5 → pas de collision avec `$SSID`/`$SB`/`$SU`/`$SP`).

**But applicatif**
- Programme Oric de référence (via LOCI, ACIA `$0380`) : `../oric/wificonf.bas`
  (scan → menu numéroté → choix → mot de passe → `ATC1` → `AT&W`) et core assembleur
  `../oric/serialcore.asm`.

**Validation**
- ✅ Build prod OK (`cmake --build src/build`), `wifi_modem.uf2` ~996 Ko, sans warning.
- ✅ **Runtime validé sur Pico W** (après le correctif de famine CDC ci-dessous) :
  `AT$SCAN` renvoie en ~1,5 s la liste numérotée des réseaux réels, dédupliquée,
  terminée par `OK` (ex. `1 AlterOP New`, `2 Livebox-E380`, …). Format consommé tel
  quel par `../oric/wificonf.bas`.

### Correctif — `AT$SCAN` muet (famine CDC) + reflash logiciel (1200-baud touch)

Premier test matériel d'`AT$SCAN` : la commande était **échouée silencieusement**
(echo de la commande, puis ni liste ni `OK`), alors que le modem restait répondant
après coup.

**Cause**
- `src/at_proprietary.h`, `doScan()` : la boucle d'attente de fin de scan faisait
  `sleep_ms(50)` **sans pomper `tud_task()`/`cdc_task()`** → le CDC USB gelait
  pendant tout le scan, et la liste/`OK` ne partaient jamais. Même classe de bug que
  les correctifs `startupWait` et `ATC1`.

**Modifié**
- `src/at_proprietary.h` : la boucle d'attente (`cyw43_wifi_scan_active`) pompe
  désormais `tud_task()`/`cdc_task()` (gardé par `#ifndef WOKWI_BUILD`,
  `sleep_ms` conservé pour Wokwi). Pompage aussi **entre chaque ligne imprimée**
  pour qu'une longue liste ne soit pas tronquée par un buffer TX plein.

**Ajouté — reflash sans bouton (1200-baud touch)**
- `src/usb_cdc.c` : `tud_cdc_line_coding_cb()` implémenté — ouvrir le port à
  **1200 bauds** appelle `reset_usb_boot(0,0)` → le Pico redémarre en BOOTSEL
  (volume `RPI-RP2`) sans appui physique. Permet de reflasher par logiciel
  (`include "pico/bootrom.h"`).

**Validation** (matériel)
- ✅ Build prod OK (`wifi_modem.uf2` ~997 Ko).
- ✅ `AT$SCAN` renvoie désormais la liste complète + `OK` (~1,5 s) — le CDC reste
  vivant pendant le scan.
- ✅ **1200-baud touch** confirmé : ouvrir `/dev/ttyACM0` à 1200 bauds fait apparaître
  le volume `RPI-RP2` sans appui bouton. Boucle de reflash 100 % logicielle validée
  de bout en bout (touch → BOOTSEL → copie uf2 → reboot → `AT$SCAN` re-OK).

### Ajout — flag de sécurité dans `AT$SCAN` (ouvert / sécurisé)

Pour que l'utilisateur Oric voie d'un coup d'œil quels réseaux sont **ouverts**.

**Modifié**
- `src/at_proprietary.h` (`doScan`) : chaque ligne devient `<index> <ssid><TAB><sec>`
  avec `<sec>` = `O` (ouvert, `result->auth_mode == 0`) ou `S` (sécurisé). Le
  séparateur **TAB** est rétro-compatible (un hôte ignorant la fin de ligne marche
  encore). Tableau `scanOpen[]` rempli dans le callback de scan.

**Validation** (matériel)
- ✅ Build OK, reflashé via 1200-touch. `AT$SCAN` →
  `1 Freebox-C31EBC\tS`, `2 AlterOP New\tS`, … `OK` (réseaux environnants tous
  sécurisés ⇒ `S` ; un AP ouvert donnerait `O`).
- Programme Oric `../oric/wificonf.bas` mis à jour : affiche « (OUVERT) » et **saute la
  saisie du mot de passe** pour un réseau ouvert.

### Ajout — réseaux WiFi ouverts (mot de passe vide)

`ATC1` exigeait un mot de passe non vide et forçait `CYW43_AUTH_WPA2_AES_PSK` ;
la connexion au boot forçait WPA2 aussi en prod. Conséquence : impossible de se
connecter à un **réseau ouvert** (sans mot de passe). Le programme Oric de config
(`../oric/wificonf.bas`) tombait sur `ERROR` dès qu'on laissait le mot de passe vide.

**Modifié**
- `src/at_basic.h` (`wifiConnection`) : `ATC1` n'exige plus que le SSID ;
  `authMode = settings.wifiPassword[0] ? CYW43_AUTH_WPA2_AES_PSK : CYW43_AUTH_OPEN`.
  Message d'erreur ajusté ("Configure SSID.").
- `src/wifi_modem.cpp` (connexion au boot) : même logique `authMode` — unifie prod et
  variant Wokwi (le SSID `Wokwi-GUEST` a un mot de passe vide ⇒ OPEN, ce qui est
  correct ; le `#ifdef WOKWI_BUILD` qui forçait OPEN est supprimé).

**Validation**
- ✅ Build OK (`wifi_modem.uf2` ~997 Ko), reflashé via 1200-touch.
- ✅ **Non-régression WPA2** : reconnexion automatique à "AlterOP New" (mot de passe
  présent ⇒ WPA2) confirmée par `ATI`.
- ⏳ Chemin OPEN vérifié par revue de code ; test runtime complet nécessite un AP
  ouvert (non disponible ici — ne pas effacer le mot de passe du réseau courant).

### Correctif CRITIQUE — `AT&W` figeait le firmware (écriture flash sans IRQ désactivées)

Symptôme remonté en test réel : `AT&W` (sauvegarde NVRAM) **ne renvoyait pas `OK`** et
**gelait** le firmware (CDC mort, plus aucune réponse ; le 1200-touch lui-même ne
fonctionnait plus → **reset physique obligatoire**). Apparaissait surtout **juste après
un `ATC1`** réussi (WiFi actif).

**Cause**
- `src/lfs.c` : `lfs_prog()`/`lfs_erase()` appelaient `flash_range_program()` /
  `flash_range_erase()` **sans désactiver les interruptions**. Pendant une opération
  flash, le **XIP est indisponible** ; si une **IRQ cyw43** (servie en contexte
  interruption par `pico_cyw43_arch_lwip_threadsafe_background`, donc WiFi actif) est
  exécutée **depuis la flash** à ce moment-là → **hard fault** → gel total.

**Modifié**
- `src/lfs.c` : `#include "hardware/sync.h"` ; les 3 opérations flash (`lfs_prog`,
  `lfs_erase`, et l'effacement) sont encadrées par
  `save_and_disable_interrupts()` / `restore_interrupts()`.

**Validation** (matériel)
- ✅ Build OK (`wifi_modem.uf2` ~997 Ko).
- ✅ **Confirmé sur Pico W** : avec WiFi connecté (cyw43 actif), `AT&W` → `OK`
  immédiat, **sans gel** ; le modem reste vivant après (`AT` → `OK`). Avant le
  correctif : `AT&W` figeait le firmware (CDC mort, reset physique requis).
- Récupération du Pico figé : power cycle complet (débrancher ~5 s) → firmware
  ré-éveillé → 1200-touch → flash du correctif.

### À venir
- Test sur cible matérielle (Pico W + LOCI) : confirmer le `CONNECT` TLS, la
  vérification CA stricte, l'`ATGET https`, et `AT$SCAN`.
- R&D SSH : étude wolfSSH vs shim socket (bloqué par `LWIP_SOCKET=0`).
