/*
  Module 3: Button Mash Game for the ESP32 LILYGO T-Display

  Supports 2 players (must modify TEAM_ID and PLAYER_ID). 
  - TEAM_ID:   should match for all members on a team
  - PLAYER_ID: unique for each member on a team (0 or 1) 
*/

#include <WiFi.h>
#include <esp_now.h>
#include <TFT_eSPI.h>

#define TEAM_ID       8      // TODO: Modify based on your TEAM ID
#define PLAYER_ID     1      // TODO: Change to 0, 1, or 2. Each group member should have a unique ID.
#define MAX_PLAYERS   2
#define TARGET        70     // button presses to win
#define BUTTON_A      0      // left button (either button counts)
#define BUTTON_B      35    // right button (either button counts)
#define HEARTBEAT_MS  250

uint8_t broadcastAddress[] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

TFT_eSPI tft = TFT_eSPI();

// counts[i] = presses by player i. Your own slot is updated in loop() while the others are filled in by the receive callback.
volatile uint16_t counts[MAX_PLAYERS] = {0, 0};
volatile bool seen[MAX_PLAYERS]   = {false, false};
volatile bool needsRedraw = true;

bool     gameOver = false;
uint32_t gameOverAt = 0;
uint32_t lastHeartbeat = 0;
int      lastSeenCount = 0;

bool lastA = HIGH, lastB = HIGH;
uint32_t lastPressTime = 0;

const uint16_t playerColors[MAX_PLAYERS] = {TFT_CYAN, TFT_ORANGE};

// Collaborative game variables
bool gameStarted = false;
bool teamWon = false;

uint16_t target = TARGET;
uint32_t timeLimitMs = 27000;

uint8_t frozenPlayer = 255;
uint8_t roundId = 0;
uint8_t turnId = 0;

uint32_t roundStartedAt = 0;
uint32_t freezeEndsAt = 0;
uint32_t lastStateReceived = 0;
uint32_t receivedTimeLeft = 0;
uint32_t receivedFreezeLeft = 0;

uint16_t turnPresses = 0;
uint16_t lastAcceptedPresses = 0;

bool peerConnected = false;
uint32_t peerConnectedAt = 0;
uint32_t stateSequence = 0;
uint32_t lastReceivedSequence = 0;

// message structure between devices
typedef struct {
  uint8_t  teamId;     // which group this message belongs to
  uint8_t  playerId;   // which player in the group sent it
  uint8_t  isReset;    // 1 = "start a new round"
  uint16_t presses;    // sender's total button presses so far

  uint8_t roundId;
  uint8_t turnId;
  uint8_t started;
  uint8_t finished;
  uint8_t won;
  uint8_t frozenId;

  uint16_t goal;
  uint16_t otherPresses;
  uint16_t turnPresses;
  uint32_t timeLimit;
  uint32_t timeLeft;
  uint32_t freezeLeft;
  uint32_t sequence;
} Message;

// Store received messages to process in loop()
portMUX_TYPE msgLock = portMUX_INITIALIZER_UNLOCKED;
Message pendingMessage;
volatile bool messageReady = false;

// receive callback
void onReceive(const esp_now_recv_info_t *info, const uint8_t *data, int len) {
  if (len != sizeof(Message)) return;

  Message msg;
  memcpy(&msg, data, sizeof(msg));
  
  if (msg.teamId != TEAM_ID) return;                 // msgs from a different team
  if (msg.playerId >= MAX_PLAYERS) return;           // bad player number
  if (msg.playerId == PLAYER_ID) return;             // two boards with the same PLAYER_ID

  portENTER_CRITICAL(&msgLock);
  pendingMessage = msg;
  messageReady = true;
  portEXIT_CRITICAL(&msgLock);
}

void setupEspNow() {
  WiFi.mode(WIFI_STA);

  if (esp_now_init() != ESP_OK) {
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_RED);
    tft.drawString("ESP-NOW init failed", 10, 60, 2);

    while (true) delay(1000);
  }
  esp_now_register_recv_cb(onReceive);

  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, broadcastAddress, 6);
  peer.channel = 0;
  peer.encrypt = false;

  esp_now_add_peer(&peer);
}

// Remaining time helper
uint32_t remainingTime(uint32_t deadline){
  int32_t difference = (int32_t)(deadline - millis());
  return difference > 0 ? difference : 0;
}

void sendState(bool reset = false) {
  Message msg = {};

  msg.teamId   = TEAM_ID;
  msg.playerId = PLAYER_ID;
  msg.isReset  = reset ? 1 : 0;
  msg.presses  = counts[PLAYER_ID];

  msg.roundId = roundId;
  msg.turnId = turnId;
  msg.started = gameStarted;
  msg.finished = gameOver;
  msg.won = teamWon;
  msg.frozenId = frozenPlayer;

  msg.goal = target;
  msg.otherPresses = counts[1 - PLAYER_ID];
  msg.turnPresses = turnPresses;
  msg.timeLimit = timeLimitMs;

  // Player 0 numbrs each outgoing game-state update
  if (PLAYER_ID == 0) {
    stateSequence++;
    msg.sequence = stateSequence;
  }

  // Player 0 controls the official timers
  if (PLAYER_ID == 0) {
    msg.timeLeft = gameStarted ? remainingTime(roundStartedAt + timeLimitMs) : timeLimitMs;

    msg.freezeLeft = gameStarted && !gameOver ? remainingTime(freezeEndsAt) : 0;
  }

  esp_now_send(broadcastAddress, (uint8_t *)&msg, sizeof(msg)); // where state is broadcasted
}

// Process received game messages
void processMessages(){
  Message msg;
  bool available = false;

  portENTER_CRITICAL(&msgLock);

  if (messageReady) {
    msg = pendingMessage;
    messageReady = false;
    available = true;
  }

  portEXIT_CRITICAL(&msgLock);

  if (!available) return; 

  seen[msg.playerId] = true;

  if (PLAYER_ID == 0) {

    if (!peerConnected) {
      peerConnected = true;
      peerConnectedAt = millis();
    }

    // Rematch request
    if (msg.isReset && gameOver && millis() - gameOverAt > 1500) {
      startNewRound();
      return;
    }

    // Accept Player 1's presses only during their turn
    if (gameStarted && !gameOver && frozenPlayer != 1 && msg.roundId == roundId && msg.turnId == turnId) {

      if (msg.turnPresses > lastAcceptedPresses) {
        uint16_t difference = msg.turnPresses - lastAcceptedPresses;
        counts[1] += difference;
        lastAcceptedPresses = msg.turnPresses;
      }
    }
  }

  // Player 1 follows Player 0's game state
  else {
    if ((int32_t)(msg.sequence - lastReceivedSequence) <= 0) {
      return;
    }

    lastReceivedSequence = msg.sequence;

    if (msg.roundId != roundId || msg.turnId != turnId) {
      turnPresses = 0;
    }

    roundId = msg.roundId;
    turnId = msg.turnId;

    target = msg.goal;
    timeLimitMs = msg.timeLimit;

    gameStarted = msg.started;
    gameOver = msg.finished;
    teamWon = msg.won;
    frozenPlayer = msg.frozenId;

    counts[0] = msg.presses;
    counts[1] = msg.otherPresses;

    receivedTimeLeft = msg.timeLeft;
    receivedFreezeLeft = msg.freezeLeft;
    lastStateReceived = millis();

    if (gameOver && gameOverAt == 0) {
      gameOverAt = millis();
    }

    if (!gameOver) gameOverAt = 0;
  }

  needsRedraw = true;
}

// set up variables for drawing the shared progress bar for both players
const int BAR_X = 4, BAR_W = 232, BAR_H = 40, BAR_TOP = 10;

// drawScreen handles everything that is drawn to the built-in display
void drawScreen() {
  seen[PLAYER_ID] = true;                            

  int numSeen = 0;
  for (int i = 0; i < MAX_PLAYERS; i++) if (seen[i]) numSeen++;

  // Only wipe the whole screen when a lane appears; otherwise redraw in place
  // (this avoids flicker while people are mashing).
  if (numSeen != lastSeenCount) {
    tft.fillScreen(TFT_BLACK);
    lastSeenCount = numSeen;
  }

  // One shared bar: player 0's presses first, then player 1's stacked after it
  int fill0 = constrain((counts[0] * BAR_W) / target, 0, BAR_W);
  int fill1 = constrain((counts[1] * BAR_W) / target, 0, BAR_W - fill0);

  // Progress bar: filled part for each player, empty part, outline
  tft.fillRect(BAR_X, BAR_TOP, fill0, BAR_H, playerColors[0]);
  tft.fillRect(BAR_X + fill0, BAR_TOP, fill1, BAR_H, playerColors[1]);
  tft.fillRect(BAR_X + fill0 + fill1, BAR_TOP, BAR_W - fill0 - fill1, BAR_H, TFT_BLACK);
  tft.drawRect(BAR_X - 1, BAR_TOP - 1, BAR_W + 2, BAR_H + 2, TFT_WHITE);

  // Player label and count (opaque text with padding, so old text is wiped)
  tft.setTextPadding(110);
  for (int i = 0; i < MAX_PLAYERS; i++) {
    tft.setTextColor(playerColors[i], TFT_BLACK);
    String label = i == PLAYER_ID ? "YOU" : "P" + String(i + 1);
    tft.drawString(label + ": " + String(counts[i]), 4 + i * 120, 60, 2);
  }
  tft.setTextPadding(0);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);  // back to white for the banner text

  uint32_t timeLeft;
  uint32_t freezeLeft;

  if (PLAYER_ID == 0) {
    timeLeft = remainingTime(roundStartedAt + timeLimitMs);
    freezeLeft = remainingTime(freezeEndsAt);
  } else {
    uint32_t elapsed = millis() - lastStateReceived; 
    timeLeft = receivedTimeLeft > elapsed ? receivedTimeLeft - elapsed : 0;
    freezeLeft = receivedFreezeLeft > elapsed ? receivedFreezeLeft - elapsed : 0;
  }

  // Banner strip for notifications at the bottom
  tft.fillRect(0, 87, 240, 48, TFT_BLACK);
  if (!gameStarted && !gameOver) {
    tft.drawString("Waiting for teammate...", 4, 91, 2);
    return;
  }
  
  tft.drawString("TEAM: " + String(counts[0] + counts[1]) + "/" + String(target) + " TIME: " + String((timeLeft + 999) / 1000), 4, 91, 2);
  
  if(gameOver) {
    tft.setTextColor(teamWon ? TFT_GREEN : TFT_RED, TFT_BLACK);
    tft.drawString(teamWon ? "TEAM WINS! PRESS FOR REMATCH" : "TIME UP! PRESS FOR REMATCH", 4, 113, 2);
  } else if (frozenPlayer == PLAYER_ID) {
    tft.setTextColor(TFT_RED, TFT_BLACK);
    tft.drawString("FROZEN! " + String((freezeLeft + 999)/ 1000) + "s", 4, 113, 2);
  } else {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("YOUR TURN! " + String((freezeLeft + 999)/ 1000) + "s", 4, 113, 2);
  }
}

// Randomize challenge every round
void randomizeGame() {
  int difficulty = random(0, 3);

  if (difficulty == 0) {
    target = 65;
    timeLimitMs = 30000;
  } else if (difficulty == 1) {
    target = 70;
    timeLimitMs = 27000;
  } else {
    target = 75;
    timeLimitMs = 25000;
  }
}

// GAME LOGIC
void startNewRound() {
  if (PLAYER_ID != 0) return;

  randomizeGame();
  
  roundId++;
  turnId = 0;

  for (int i = 0; i < MAX_PLAYERS; i++) counts[i] = 0;

  gameOver = false;
  teamWon = false;
  gameStarted = true;
  gameOverAt = 0;

  turnPresses = 0;
  lastAcceptedPresses = 0;

  roundStartedAt = millis();

  frozenPlayer = random(0, 2);
  freezeEndsAt = millis() + random(2000, 5001);

  needsRedraw = true;
  sendState();
}

void updateFreeze() {
  if (PLAYER_ID != 0 || !gameStarted || gameOver) { return; }

  if (remainingTime(freezeEndsAt) == 0) {
    frozenPlayer = 1 - frozenPlayer;
    turnId++;

    turnPresses = 0;
    lastAcceptedPresses = 0;

    freezeEndsAt = millis() + random(2000, 5001);

    sendState();
    needsRedraw = true;
  }
}

void checkForWinner() {
  if (PLAYER_ID != 0 || !gameStarted || gameOver) { return; }

  uint32_t timeLeft = remainingTime(roundStartedAt + timeLimitMs);

  if (counts[0] + counts[1] >= target || timeLeft == 0) {
    gameOver = true;

    teamWon = counts[0] + counts[1] >= target && timeLeft > 0;

    gameOverAt = millis();
    needsRedraw = true;

    sendState();
  }
}

bool buttonPressed(uint8_t pin, bool &last) {
  bool now = digitalRead(pin);
  bool pressed = (last == HIGH && now == LOW);
  last = now;
  return pressed;
}

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_A, INPUT_PULLUP);
  pinMode(BUTTON_B, INPUT);          // GPIO 35 has an external pull-up on the board

  tft.init();
  tft.setRotation(1);                // landscape, 240 x 135
  tft.fillScreen(TFT_BLACK);

  randomSeed(esp_random());

  setupEspNow();
  drawScreen();
}

void loop() {
  processMessages();

  if (PLAYER_ID == 0) {
    if (!gameStarted && !gameOver && peerConnected && millis() - peerConnectedAt > 1000) {
      startNewRound();
    }

    updateFreeze();
    checkForWinner();
  }

  bool a = buttonPressed(BUTTON_A, lastA);
  bool b = buttonPressed(BUTTON_B, lastB);
  bool pressed = (a || b) && (millis() - lastPressTime > 20); // small debounce built in

  if (pressed) {
    lastPressTime = millis();

    if (gameStarted && !gameOver && frozenPlayer != PLAYER_ID) {
      if (PLAYER_ID == 0) {
        counts[PLAYER_ID]++;
        checkForWinner();
      } else {
        turnPresses++;
        sendState();
      }
      
      needsRedraw = true;
    }

    else if (gameOver && millis() - gameOverAt > 1500) {
      if (PLAYER_ID == 0) {
        startNewRound();
      } else {
        sendState(true);
      }
    }
  }

  if(millis() - lastHeartbeat > HEARTBEAT_MS) {
    lastHeartbeat = millis();
    sendState();
    needsRedraw = true;
  }

  // only redraw only when something changed
  if (needsRedraw) {
    needsRedraw = false;
    drawScreen();
  }
}
