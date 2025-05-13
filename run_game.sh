#!/usr/bin/env bash
set -euo pipefail

BROKER_HOST="34.83.53.31"
BROKER_PORT=1883

echo "Starting autoplay…"
while true; do
  ./player_x.sh
  ./player_o.sh

  status=$(mosquitto_sub -h "$BROKER_HOST" -p "$BROKER_PORT" \
             -t game/status -C 1 -W 2 2>/dev/null || echo "")
  if [[ $status == *'"done":true'* ]]; then
    echo "Autoplay: game finished."
    break
  fi
done
