#!/usr/bin/env bash
set -e

BROKER="34.83.53.31"
PORT=1883
BOARD_TOPIC="game/board"
PLAYER=${1:-X}      # pass X or O, default X
MOVE_TOPIC="game/move_$PLAYER"

while true; do
  # get the latest board once, timeout quickly if none
  payload=$(mosquitto_sub -h "$BROKER" -p $PORT -t "$BOARD_TOPIC" -C 1 -W 1) || continue

  # extract the 9-char board string
  board=$(jq -r .board <<<"$payload")
  [[ ${#board} -eq 9 ]] || continue

  # find all empty positions
  empties=()
  for i in {0..8}; do
    [[ ${board:i:1} == " " ]] && empties+=($i)
  done
  (( ${#empties[@]} )) || break  # no empties → done

  # pick one at random
  idx=$(( RANDOM % ${#empties[@]} ))
  mv=${empties[$idx]}

  # publish move JSON
  row=$(( mv/3 ))
  col=$(( mv%3 ))
  msg="{\"player\":\"$PLAYER\",\"row\":$row,\"col\":$col}"
  mosquitto_pub -h "$BROKER" -p $PORT -t "$MOVE_TOPIC" -m "$msg"

  # wait a bit before next
  sleep 0.5
done


