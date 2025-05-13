#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <MQTTClient.h>
#include <unistd.h>   // for sleep()
#include <time.h>

#define ADDRESS         "tcp://34.83.53.31"
#define CLIENTID        "C_TUI_Client"
#define TOPIC_BOARD     "game/board"
#define TOPIC_MODE      "game/mode"
#define QOS             1
#define TIMEOUT         10000L
#define TOPIC_MOVE_X    "game/move_X"

// a rolling copy of the last 9‐char board payload:
static char lastBoard[10] = "         ";  // 9 spaces + '\0'

// forward‐declare your new mode function:
void bot_vs_bot(MQTTClient client);

// draw a 3×3 board from a flat 9-char string
void drawBoard(const char *b) {
  system("clear");
  printf("\nTic-Tac-Toe Board:\n\n");
  for (int i = 0; i < 9; i++) {
    char c = b[i];
    if (c == ' ')      c = '.';    // empty → dot
    printf(" %c ", c);
    if (i % 3 < 2)     printf("|");
    if (i % 3 == 2 && i != 8) printf("\n---+---+---\n");
  }
  printf("\n\n");
}

// check for a 3-in-a-row
int checkWin(const char *b, char p) {
  for (int i = 0; i < 3; i++) {
    if (b[3*i+0]==p && b[3*i+1]==p && b[3*i+2]==p) return 1;
    if (b[0*3+i]==p && b[1*3+i]==p && b[2*3+i]==p) return 1;
  }
  if (b[0]==p && b[4]==p && b[8]==p) return 1;
  if (b[2]==p && b[4]==p && b[6]==p) return 1;
  return 0;
}

// ============================================================================
// Part 1 & 3 callback for MQTT board updates (modes 1 & 3)
// ============================================================================
int messageArrived(void *context, char *topicName, int topicLen, MQTTClient_message *message) {
  char *payload = (char *)message->payload;
  printf("[DEBUG] messageArrived on %s -> %.*s\n",
         topicName,
         (int)message->payloadlen,
         payload);

  char board[10] = {0};

  char *start = strstr(payload, "\"board\":\"");
  if (start) {
    start += 9;                     // skip past `"board":"`
    strncpy(board, start, 9);      // copy exactly 9 chars
    board[9] = '\0';
    drawBoard(board);

    // keep a local copy for our bot loop:
    memcpy(lastBoard, board, 9);
    lastBoard[9] = '\0';
  }

  // Paho frees message/topic for us when we return 1
  MQTTClient_freeMessage(&message);
  MQTTClient_free(topicName);
  return 1;
}
//─────────────────────────────────────────────────────────────────────────────
// Mode 1: Human (X) vs Computer (O over MQTT)
//─────────────────────────────────────────────────────────────────────────────
void human_vs_bot(MQTTClient client) {
  char board[10] = {0};
  int xcount, ocount;
  char *topicName = NULL;
  MQTTClient_message *msg = NULL;
  while (1) {
    // 1) Block until we get the next board update from ESP32
    if (MQTTClient_receive(client,
                           &topicName,
                           NULL,
                           &msg,
                           TIMEOUT) != MQTTCLIENT_SUCCESS
        || !msg) {
      continue;
    }

    // 2) Extract the 9-char board string
    char *payload = (char*)msg->payload;
    char *start = strstr(payload, "\"board\":\"");
    if (!start) {
      MQTTClient_freeMessage(&msg);
      MQTTClient_free(topicName);
      continue;
    }
    start += 9;
    strncpy(board, start, 9);
    board[9] = '\0';

    // 3) Draw it
    drawBoard(board);

    // 4) Count X’s and O’s so we know whose turn
    xcount = 0; ocount = 0;
    for (int i = 0; i < 9; i++) {
      if (board[i] == 'X') xcount++;
      if (board[i] == 'O') ocount++;
    }

    // 5) Terminal condition
    if (checkWin(board, 'X')) { printf("You win!\n"); break; }
    if (checkWin(board, 'O')) { printf("Bot wins!\n"); break; }
    if (xcount + ocount == 9) { printf("It's a draw!\n"); break; }

    // 6a) If it's your turn, prompt for X and publish
    if (xcount == ocount) {
      int mv;
      char line[32];
      do {
        printf("Player X, enter your move (0–8): ");
        fflush(stdout);
      } while (!fgets(line, sizeof(line), stdin)
               || sscanf(line, "%d", &mv) != 1
               || mv < 0 || mv > 8
               || board[mv] != ' ');

      // translate to row/col and send
      int row = mv / 3, col = mv % 3;
      char moveMsg[64];
      snprintf(moveMsg, sizeof(moveMsg),
               "{\"player\":\"X\",\"row\":%d,\"col\":%d}",
               row, col);

      MQTTClient_message pub = MQTTClient_message_initializer;
      MQTTClient_deliveryToken token;
      pub.payload    = moveMsg;
      pub.payloadlen = (int)strlen(moveMsg);
      pub.qos        = QOS;
      pub.retained   = 0;
      MQTTClient_publishMessage(client,
                                "game/move_X",
                                &pub,
                                &token);
      MQTTClient_waitForCompletion(client, token, 1000L);
    } else {
      printf("Computer (O) thinking...\n");
      fflush(stdout);
      int rc = system("./bot_player.sh O");
      MQTTClient_yield();
      if (rc != 0) {
        fprintf(stderr, "Error running bot_player.sh (returned %d)\n", rc);
      }
    }

    // 7) clean up and loop
    MQTTClient_freeMessage(&msg);
    MQTTClient_free(topicName);
  }
}

// ============================================================================
// Mode 2: local, human vs human
// ============================================================================
void human_vs_human() {
  char board[10];
  memset(board, ' ', 9);
  board[9] = '\0';

  drawBoard(board);
  char player = 'X';
  for (int turn = 0; turn < 9; ++turn) {
    int mv;
    printf("Player %c, enter your move (0–8): ", player);
    if (scanf("%d", &mv) != 1) {
      fprintf(stderr, "Invalid input. Try again.\n");
      // clear stdin
      while ( getchar()!='\n' );
      --turn;
      continue;
    }
    if (mv < 0 || mv > 8 || board[mv] != ' ') {
      printf("Cell occupied or out of range. Try again.\n");
      --turn;
      continue;
    }
    board[mv] = player;
    drawBoard(board);
    if (checkWin(board, player)) {
      printf("Player %c wins!\n\n", player);
      return;
    }
    player = (player == 'X' ? 'O' : 'X');
  }
  printf("It's a draw!\n\n");
}

// ============================================================================
// Mode 3: bot vs bot
// ============================================================================
void bot_vs_bot(MQTTClient client) {
    srand(time(NULL));

    // subscribe to board updates
    MQTTClient_subscribe(client, TOPIC_BOARD, QOS);

    // initialize to empty
    memset(lastBoard, ' ', 9);
    lastBoard[9] = '\0';

    while (1) {
        // ----- X’s turn (this C‐bot) -----
        {
            int moved = 0;
            while (!moved) {
                MQTTClient_yield();
                // collect empty cells
                int empties[9], n=0;
                for (int i=0; i<9; i++) {
                    if (lastBoard[i] == ' ' || lastBoard[i] == '.') {
                        empties[n++] = i;
                    }
                }
                if (n > 0) {
                    int choice = empties[rand() % n];
                    int row = choice / 3, col = choice % 3;

                    char payload[64];
                    snprintf(payload, sizeof(payload),
                             "{\"player\":\"X\",\"row\":%d,\"col\":%d}",
                             row, col);

                    MQTTClient_message pubmsg = MQTTClient_message_initializer;
                    pubmsg.payload    = payload;
                    pubmsg.payloadlen = (int)strlen(payload);
                    pubmsg.qos        = QOS;
                    pubmsg.retained   = 0;
                    MQTTClient_publishMessage(client,
                                              "game/move_X",
                                              &pubmsg,
                                              NULL);
                    moved = 1;
                }
                usleep(100000);  // 0.1s
            }
        }

        sleep(1);  // give ESP32 time to echo back the new board
        // ----- O’s turn (your bash bot) -----
        {
            int moved = 0;
            while (!moved) {
                MQTTClient_yield();
                // collect empty cells
                int empties[9], n=0;
                for (int i=0; i<9; i++) {
                    if (lastBoard[i] == ' ' || lastBoard[i] == '.') {
                        empties[n++] = i;
                    }
                }
                if (n > 0) {
                    int choice = empties[rand() % n];
                    int row = choice / 3, col = choice % 3;

                    char cmd[128];
                    // call your existing bash script to publish move_O
                    snprintf(cmd, sizeof(cmd),
                             "./bot_player.sh %d %d",
                             row, col);
                    system(cmd);

                    moved = 1;
                }
                usleep(100000);
            }
        }

        sleep(1);  // let the ESP32 update again
    }
}

// ============================================================================
// main: menu + dispatch
// ============================================================================
int main(int argc, char* argv[]) {
    int choice;

    // ---- ask the user ----
    printf("Select mode:\n");
    printf(" 1) Human (X) vs Computer (O)\n");
    printf(" 2) Human (X) vs Human   (O)\n");
    printf(" 3) Computer (X) vs Computer (O)\n");
    printf("Enter choice (1–3): ");
    if (scanf("%d", &choice) != 1 || choice < 1 || choice > 3) {
        fprintf(stderr, "Invalid choice. Exiting.\n");
        return 1;
    }

    // ---- Mode 2 is purely local ----
    if (choice == 2) {
        human_vs_human();
        return 0;
    }

    // ---- otherwise (1 or 3) we do an MQTT run ----
    MQTTClient       client;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;

    // create + set callbacks
    MQTTClient_create(&client, ADDRESS, CLIENTID,
                      MQTTCLIENT_PERSISTENCE_NONE, NULL);
    MQTTClient_setCallbacks(client,
                            NULL,          // context for lost conn
                            NULL,          // connectionLost
                            messageArrived,
                            NULL);         // deliveryComplete

    conn_opts.keepAliveInterval = 20;
    conn_opts.cleansession       = 1;
    if (MQTTClient_connect(client, &conn_opts) != MQTTCLIENT_SUCCESS) {
        fprintf(stderr, "Failed to connect to MQTT broker.\n");
        return 1;
    }

    // subscribe to board updates
    MQTTClient_subscribe(client, TOPIC_BOARD, QOS);
    printf("[DEBUG] Subscribed to BOARD topic: %s\n", TOPIC_BOARD);

    // publish mode to ESP32 so it can wire itself as X or O
    {
        char modeMsg[32];
        MQTTClient_message pubmsg = MQTTClient_message_initializer;
        MQTTClient_deliveryToken token;

        snprintf(modeMsg, sizeof(modeMsg), "{\"mode\":%d}", choice);
        pubmsg.payload    = modeMsg;
        pubmsg.payloadlen = (int)strlen(modeMsg);
        pubmsg.qos        = QOS;
        pubmsg.retained   = 0;

        printf("[DEBUG] Publishing mode %d to topic %s: %s\n", choice, TOPIC_MODE, modeMsg);
        MQTTClient_publishMessage(client, TOPIC_MODE, &pubmsg, &token);
        MQTTClient_waitForCompletion(client, token, 1000L);
    }

    // dispatch to the right “bot” or “human_vs_bot” function
    if (choice == 1) {
        printf("[DEBUG] running human_vs_bot()\n");
        human_vs_bot(client);
    } else {
        printf("[DEBUG] running bot_vs_bot()\n");
        bot_vs_bot(client);
    }

    // clean up
    MQTTClient_disconnect(client, 10000);
    MQTTClient_destroy(&client);
    return 0;
}
