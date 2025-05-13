#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <WiFi.h>
#include <PubSubClient.h>

// I2C pin definitions for LCD
#define SDA 14    // Define SDA pin
#define SCL 13    // Define SCL pin

/*
 * Note: If LCD1602 uses PCF8574T, IIC address is 0x27
 *       If LCD1602 uses PCF8574AT, IIC address is 0x3F
*/
LiquidCrystal_I2C lcd(0x27, 16, 2);

// WiFi and MQTT configuration
const char* ssid        = "CIC Operations";
const char* password    = "juxqb4f7n6cet";
const char* mqtt_server = "34.83.53.31";

WiFiClient     espClient;
PubSubClient   client(espClient);

// Game state variables
char board[3][3];      // 3x3 Tic-Tac-Toe board
int  xWins = 0, oWins = 0, draws = 0;

char  waitingPlayer = ' ';  // Track who is waiting for MQTT echo
bool  moveReceived   = false;  // Whether a valid move has been received
int   recRow = -1, recCol = -1;  // Coordinates of the received move
bool  haveMode = false;
int   gameMode = 0;

// —— forward declarations —— 
void    setup_LCD();
void    setup_WiFi();
void    setup_MQTT();
void    reconnectMQTT();
void    LCDWelcomeMessage();
void    runGameLoop();
void    playGame();
void    displayScore();
bool    checkWin(char player);
void    resetBoard();
bool    waitForMoveFromMQTT(char player);
void    mqtt_callback(char* topic, byte* payload, unsigned int len);
bool    i2CAddrTest(uint8_t addr);
void    publishBoardState();

void setup() {
  Serial.begin(115200);
  setup_LCD();
  setup_WiFi();
  setup_MQTT();
  LCDWelcomeMessage();
}

void loop() {
  client.loop();  // Keep MQTT client alive
}

void setup_LCD() {
  Wire.begin(SDA, SCL);
  if (!i2CAddrTest(0x27)) {
    lcd = LiquidCrystal_I2C(0x3F, 16, 2);
  }
  lcd.init();
  lcd.backlight();
}

void setup_WiFi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  int retries = 0;
  while (WiFi.status() != WL_CONNECTED && retries < 20) {
    delay(500);
    Serial.print(".");
    retries++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.print("ESP32 IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("ESP32 MAC address: ");
    Serial.println(WiFi.macAddress());
  } else {
    Serial.println("\nFailed to connect to WiFi!");
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Failed!");
    while (true) delay(1000);
  }
}

void setup_MQTT() {
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqtt_callback);
  reconnectMQTT();
  client.subscribe("game/mode");
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Connecting to MQTT...");
    String clientID = "ESP32-" + WiFi.macAddress();
    clientID.replace(":", "");
    if (client.connect(clientID.c_str())) {
      Serial.println("connected.");
      client.subscribe("game/mode");
      client.subscribe("game/move_X");
      client.subscribe("game/move_O");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 sec");
      delay(5000);
    }
  }
}

void LCDWelcomeMessage() {
  lcd.setCursor(0, 0);
  lcd.print("Tic-Tac-Toe!");
  delay(2000);
  lcd.clear();
}

void runGameLoop() {
  for (int i = 0; i < 100; i++) {
    playGame();
    displayScore();

    delay(1000);

    char statusMsg[32];
    snprintf(statusMsg, sizeof(statusMsg), "{\"games_played\":%d}", i + 1);
    client.publish("game/status", statusMsg);

    delay(500);
  }

  client.publish("game/status", "{\"done\":true}");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DONE! Draws:");
  lcd.print(draws);
  lcd.setCursor(0, 1);
  lcd.print("X:");
  lcd.print(xWins);
  lcd.print(" O:");
  lcd.print(oWins);
}

void playGame() {
  resetBoard();
  publishBoardState();
  int turn = 0;
  char currentPlayer = 'X';

  while (turn < 9) {
    if (waitForMoveFromMQTT(currentPlayer)) {
      if (checkWin(currentPlayer)) {
        if (currentPlayer == 'X') xWins++;
        else oWins++;
        return;
      }
      currentPlayer = (currentPlayer == 'X') ? 'O' : 'X';
      turn++;
    }
  }

  draws++;
}

void interactiveGame() {
  playGame();
  displayScore();
}

bool waitForMoveFromMQTT(char player) {
  waitingPlayer = player;
  moveReceived   = false;
  recRow = recCol = -1;

  while (!moveReceived) {
    client.loop();
    delay(10);
  }

  if (recRow >= 0 && recCol >= 0 && board[recRow][recCol] == ' ') {
    board[recRow][recCol] = player;
    publishBoardState();
    waitingPlayer = ' ';
    return true;
  }

  waitingPlayer = ' ';
  return false;
}

void mqtt_callback(char* topic, byte* payload, unsigned int length) {
  String msg = String((char*)payload).substring(0, length);

  Serial.print("Received MQTT [");
  Serial.print(topic);
  Serial.print("]: ");
  Serial.println(msg);

  // Mode message arrives
  if (String(topic) == "game/mode") {
    int colon = msg.indexOf(':');
    int brace = msg.lastIndexOf('}');
    if (colon < 0 || brace < 0) return;
    int mode = msg.substring(colon + 1, brace).toInt();

    haveMode = true;
    gameMode = mode;

    if (mode == 1) {
      Serial.println("-> Mode 1: Human vs Bot (single interactive game)");
      interactiveGame();
    }
    else if (mode == 2) {
      Serial.println("-> Mode 2: Human vs Human (single interactive game)");
      interactiveGame();
    }
    else if (mode == 3) {
      Serial.println("-> Mode 3: Bot vs Bot (autoplay 100 games)");
      runGameLoop();
    }
    
    return;
  }

  if (!haveMode) {
    Serial.println(" (haven't received mode yet - ignoring)");
    return;
  }

  if (waitingPlayer == ' ') return;

  String t = String(topic);
  if ((waitingPlayer == 'X' && t != "game/move_X") ||
      (waitingPlayer == 'O' && t != "game/move_O")) {
    return;
  }

  int rIdx = msg.indexOf("row");
  int cIdx = msg.indexOf("col");
  if (rIdx < 0 || cIdx < 0) return;

  recRow = msg.substring(msg.indexOf(':', rIdx) + 1, msg.indexOf(',', rIdx)).toInt();
  recCol = msg.substring(msg.indexOf(':', cIdx) + 1, msg.indexOf('}', cIdx)).toInt();
  moveReceived = true;
}

void displayScore() {
  Serial.printf("Score now X=%d O=%d D=%d\n", xWins,oWins,draws);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("X:");
  lcd.print(xWins);
  lcd.print(" O:");
  lcd.print(oWins);
  lcd.setCursor(0, 1);
  lcd.print("Draws:");
  lcd.print(draws);
}

bool checkWin(char player) {
  for (int i = 0; i < 3; i++) {
    if (board[i][0]==player && board[i][1]==player && board[i][2]==player) return true;
    if (board[0][i]==player && board[1][i]==player && board[2][i]==player) return true;
  }
  if (board[0][0]==player && board[1][1]==player && board[2][2]==player) return true;
  if (board[0][2]==player && board[1][1]==player && board[2][0]==player) return true;
  return false;
}

void resetBoard() {
  for (int r = 0; r < 3; r++)
    for (int c = 0; c < 3; c++)
      board[r][c] = ' ';
}

void publishBoardState() {
  String boardStr;
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      boardStr += board[i][j];
  String msg = "{\"board\":\"" + boardStr + "\"}";
  // Use the overload that lets us set retained=true
  client.publish("game/board",(const uint8_t*)msg.c_str(), msg.length(), true);
  Serial.println("Published board state (retained).");
}

bool i2CAddrTest(uint8_t addr) {
  Wire.beginTransmission(addr);
  return (Wire.endTransmission() == 0);
}
