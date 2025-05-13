#!/usr/bin/env bash
set -euo pipefail

BROKER_HOST="34.83.53.31"
BROKER_PORT=1883
PLAYER="O"
BOARD_TOPIC="game/board"
MOVE_TOPIC="game/move_O"

payload=$(mosquitto_sub -h "$BROKER_HOST" -p "$BROKER_PORT" \
             -t "$BOARD_TOPIC" -C 1 -W 2) \
          || exit 1

board=$(jq -r '.board' <<<"$payload")

for i in {0..8}; do
  if [[ "${board:$i:1}" == " " ]]; then
    row=$(( i/3 ))
    col=$(( i%3 ))
    mosquitto_pub -h "$BROKER_HOST" -p "$BROKER_PORT" \
      -t "$MOVE_TOPIC" \
      -m "{\"player\":\"$PLAYER\",\"row\":$row,\"col\":$col}"
    echo "player_o: played O at ($row,$col)"
    exit 0
  fi
done

exit 1
