#!/usr/bin/env bash
set -euo pipefail

# ─── CONFIG ──────────────────────────────────────────────────────────────────
BROKER_HOST="34.83.53.31"
BROKER_PORT=1883
PLAYER="${PLAYER:-X}"    # must be “X” or “O”
BOARD_TOPIC="game/board"
MOVE_TOPIC="game/move_${PLAYER}"
# ─────────────────────────────────────────────────────────────────────────────

while true; do
  # 1) fetch the latest board
  payload=$(mosquitto_sub -h "$BROKER_HOST" -p "$BROKER_PORT" \
               -t "$BOARD_TOPIC" -C 1 -W 2 2>/dev/null) \
           || { echo "Timed out; retrying…" >&2; continue; }

  # 2) extract the 9-char board string
  board=$(jq -r '.board' <<<"$payload")
  [[ ${#board} -eq 9 ]] || { echo "Bad board payload, retrying…" >&2; continue; }

  # 3) display it
  echo
  echo "Current board (you are $PLAYER):"
  for i in {0..8}; do
    c="${board:$i:1}"
    [[ $c == " " ]] && c="·"
    printf " %s " "$c"
    (( (i+1) % 3 == 0 )) && echo
  done

  # 4) prompt for move
  read -p $'\nYour move (0–8)? ' mv
  if ! [[ $mv =~ ^[0-8]$ ]]; then
    echo " → Invalid. Must be 0–8." >&2
    continue
  fi

  # 5) check that cell is empty
  if [[ "${board:$mv:1}" != " " ]]; then
    echo " → Occupied. Try again." >&2
    continue
  fi

  # 6) publish your move
  row=$(( mv / 3 ))
  col=$(( mv % 3 ))
  mosquitto_pub -h "$BROKER_HOST" -p "$BROKER_PORT" \
    -t "$MOVE_TOPIC" \
    -m "{\"player\":\"$PLAYER\",\"row\":$row,\"col\":$col}"

  break
done

