#!/bin/bash
# flash-pico.sh — copie le firmware sur un Pico W en mode BOOTSEL.
#
# Le dongle (cafe:4001) n'expose PAS d'interface reset picotool : le passage
# en BOOTSEL est donc manuel. Marche à suivre :
#   1. Débrancher le Pico W.
#   2. Maintenir le bouton BOOTSEL enfoncé, rebrancher l'USB, relâcher.
#      -> un volume "RPI-RP2" se monte automatiquement.
#   3. Lancer ce script : il attend le volume puis y copie l'uf2.
#
# Usage : ./flash-pico.sh [chemin/vers/wifi_modem.uf2]

set -euo pipefail
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
UF2="${1:-$SCRIPT_DIR/src/build/wifi_modem.uf2}"

[ -f "$UF2" ] || { echo "uf2 introuvable : $UF2" >&2; exit 1; }
echo "Firmware  : $UF2 ($(du -h "$UF2" | cut -f1))"
echo "En attente du volume RPI-RP2 (passe le Pico en BOOTSEL)..."

DST=""
for _ in $(seq 1 60); do            # ~60 s d'attente
    for d in /media/"$USER"/RPI-RP2 /run/media/"$USER"/RPI-RP2 /mnt/RPI-RP2; do
        [ -d "$d" ] && DST="$d" && break
    done
    [ -n "$DST" ] && break
    # repli : repérer un montage dont le label commence par RPI-RP2
    m=$(mount | grep -iom1 '/[^ ]*RPI-RP2[^ ]*' || true)
    [ -n "$m" ] && DST="$m" && break
    sleep 1
done

[ -n "$DST" ] || { echo "RPI-RP2 non détecté. Le Pico est-il bien en BOOTSEL ?" >&2; exit 1; }
echo "Volume détecté : $DST"
cp "$UF2" "$DST/" && sync
echo "Copie terminée. Le Pico redémarre sur le nouveau firmware (~3 s)."
echo "Vérifie ensuite : /dev/ttyACM0 doit réapparaître (AT -> OK, AT\$SCAN)."
