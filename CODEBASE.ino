//Code Written In Arduino Language
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <LittleFS.h>
#include <time.h>
#include <Preferences.h>
#include <esp_system.h>
#include <math.h>

// =============================================================================
// HARDWARE DEFINITIONS & DISPLAY SETUP
// =============================================================================
#define TFT_CS    5
#define TFT_RST   4
#define TFT_DC    2

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
Preferences prefs;
WebServer server(80);

#define ENABLE_PHYSICAL_BUTTONS 1

#define PIN_BTN_NAV    14  // Hardware Button 0 (Red cap)
#define PIN_BTN_PREV   27  // Hardware Button 1 (Blue cap)
#define PIN_BTN_SEL    13  // Hardware Button 2 (White cap)
#define PIN_BTN_BACK   32  // Hardware Button 3 (Amber cap)

// =============================================================================
// WI-FI & NTP CONFIGURATION
// =============================================================================
const char* WIFI_SSID     = "AirFiber+";
const char* WIFI_PASSWORD = "11223344";

const char* AP_SSID        = "FRIDAY-CORE";
const char* AP_PASS        = "cyberdeck32";

const char* NTP_SERVER_1  = "pool.ntp.org";
const char* NTP_SERVER_2  = "time.nist.gov";
const long  NTP_GMT_OFFSET_SEC      = 19800; // IST UTC+5:30
const int   NTP_DAYLIGHT_OFFSET_SEC = 0;

const unsigned long WIFI_BOOT_TIMEOUT_MS = 6000;
const unsigned long NTP_BOOT_TIMEOUT_MS  = 4000;
const size_t MIN_SAFE_HEAP_BYTES = 40960; // 40 KB Safety ceiling

// =============================================================================
// COLOR PALETTES & THEME ENGINE
// =============================================================================
struct ThemePalette {
  const char* name;
  uint16_t bg;
  uint16_t primary;
  uint16_t secondary;
  uint16_t dim;
  uint16_t steel;
  uint16_t highlight;
};

const ThemePalette THEMES[] = {
  {"OBSIDIAN",   0x0082, 0x07FF, 0xFD00, 0x03F5, 0x1A29, 0xAD7F},
  {"MATRIX",     0x0000, 0x07E0, 0x05E0, 0x0320, 0x0200, 0x87F0},
  {"SYNTHWAVE",  0x1012, 0xF81F, 0x07FF, 0x8010, 0x3018, 0xFFE0}
};
const uint8_t TOTAL_THEMES = sizeof(THEMES) / sizeof(THEMES[0]);
uint8_t currentThemeIdx = 0;

#define COLOR_BG          THEMES[currentThemeIdx].bg
#define COLOR_AMBER       THEMES[currentThemeIdx].secondary
#define COLOR_CYAN        THEMES[currentThemeIdx].primary
#define COLOR_CYAN_DIM    THEMES[currentThemeIdx].dim
#define COLOR_STEEL       THEMES[currentThemeIdx].steel
#define COLOR_ICE         THEMES[currentThemeIdx].highlight
#define COLOR_LIME        0x07E0
#define COLOR_BLACK       0x0000
#define COLOR_WHITE       0xFFFF

// Hardware Button Colors
#define HW_COLOR_RED      0xF800
#define HW_COLOR_BLUE     0x001F
#define HW_COLOR_WHITE    0xFFFF
#define HW_COLOR_AMBER    0xFBE0

const uint16_t HW_BTN_COLORS[4] = {
  HW_COLOR_RED,   
  HW_COLOR_BLUE,  
  HW_COLOR_WHITE, 
  HW_COLOR_AMBER  
};

// =============================================================================
// SYSTEM & STATE DEFINITIONS
// =============================================================================
enum BootState {
  BOOT_STATE_VERSION,
  BOOT_STATE_LINUX_LOGS,
  BOOT_STATE_CRT_BEAM,
  BOOT_STATE_RASTER_EXPAND,
  BOOT_STATE_GRID_SCAN,
  BOOT_STATE_DECRYPT_NAME,
  BOOT_STATE_SYS_LOCK,
  BOOT_STATE_IDLE
};

enum AppState {
  STATE_BOOTING, STATE_HOMESCREEN, STATE_ROLL_MENU, STATE_APP_ACTIVE,
  STATE_APP_CLOCK, STATE_APP_GAMES, STATE_APP_SETTINGS, STATE_APP_FRIDAY,
  STATE_APP_TODO, STATE_APP_READER, STATE_APP_KEYBOARD, STATE_APP_EDITH
};

enum ClockView { CLOCK_VIEW_MENU, CLOCK_VIEW_STOPWATCH, CLOCK_VIEW_TIMER, CLOCK_VIEW_SYNC };
enum GamesView { GAMES_VIEW_SELECT_GAME, GAMES_VIEW_SELECT_ACTION, GAMES_VIEW_PLAYING, GAMES_VIEW_GAMEOVER, GAMES_VIEW_HIGHSCORES };
enum SettingsView { SETTINGS_VIEW_MENU, SETTINGS_VIEW_TELEMETRY, SETTINGS_VIEW_THEME, SETTINGS_VIEW_CONTROL_MAP, SETTINGS_VIEW_STORAGE, SETTINGS_VIEW_NET_MENU, SETTINGS_VIEW_NET_STATUS, SETTINGS_VIEW_WIFI_SCAN, SETTINGS_VIEW_WIFI_CONNECTING, SETTINGS_VIEW_REBOOT };
enum ReaderView { READER_VIEW_FILE_SELECT, READER_VIEW_FILE_ACTION, READER_VIEW_READING };
enum TodoView { TODO_VIEW_LIST, TODO_VIEW_ACTION };

enum KbTargetAction {
  KB_TARGET_NONE, KB_TARGET_WIFI_PASS, KB_TARGET_TODO_ADD, KB_TARGET_FILE_RENAME
};

enum SelectedGame {
  GAME_SPACE_IMPACT = 0,
  GAME_CYBER_SNAKE  = 1,
  GAME_CYBER_BIRD   = 2,
  GAME_FIREWALL     = 3,
  GAME_OVERDRIVE    = 4,
  GAME_LUNAR        = 5
};

AppState currentAppState = STATE_BOOTING;
ClockView currentClockView = CLOCK_VIEW_MENU;
GamesView currentGamesView = GAMES_VIEW_SELECT_GAME;
SettingsView currentSettingsView = SETTINGS_VIEW_MENU;
ReaderView currentReaderView = READER_VIEW_FILE_SELECT;
TodoView currentTodoView = TODO_VIEW_LIST;
SelectedGame activeGame = GAME_SPACE_IMPACT;
KbTargetAction currentKbTarget = KB_TARGET_NONE;

int readerActionIndex = 0;
const char* readerActions[] = {"READ", "RENAME", "DELETE"};

int todoActionIndex = 0;
const char* todoActions[] = {"TOGGLE", "DELETE"};

enum InputAction { ACTION_NONE, ACTION_NAV, ACTION_PREV, ACTION_SELECT, ACTION_BACK };
enum LogicalAction { LOG_ACTION_RIGHT = 0, LOG_ACTION_LEFT = 1, LOG_ACTION_SEL = 2, LOG_ACTION_ESC = 3 };

const char* actionNames[] = {"RIGHT", "LEFT", "SELECT", "ESCAPE"};
uint8_t buttonActionMap[4] = { LOG_ACTION_RIGHT, LOG_ACTION_LEFT, LOG_ACTION_SEL, LOG_ACTION_ESC };
int controlMapCursor = 0;

uint16_t getActionColor(uint8_t targetLogicalAction) {
  for (uint8_t i = 0; i < 4; i++) {
    if (buttonActionMap[i] == targetLogicalAction) return HW_BTN_COLORS[i];
  }
  return COLOR_WHITE;
}

#define COLOR_ACT_RIGHT   getActionColor(LOG_ACTION_RIGHT)
#define COLOR_ACT_LEFT    getActionColor(LOG_ACTION_LEFT)
#define COLOR_ACT_SEL     getActionColor(LOG_ACTION_SEL)
#define COLOR_ACT_ESC     getActionColor(LOG_ACTION_ESC)

bool rtcHasBeenSynced = false;
int clockMenuIndex = 0;
const int TOTAL_CLOCK_MENU_ITEMS = 3;
const char* clockMenuItems[] = {"STOPWATCH", "TIMER", "SYNC NTP"};

int gameListIndex = 0;
const int TOTAL_GAMES = 6;
const char* gameTitles[] = {"SPACE IMPACT", "CYBER SNAKE", "CYBER BIRD", "FIREWALL BREACH", "NEON OVERDRIVE", "LUNAR DESCENT"};

int gameActionIndex = 0;
const int TOTAL_GAME_ACTIONS = 2;
const char* gameActions[] = {"PLAY", "HIGHSCORE"};

int settingsMenuIndex = 0;
const int TOTAL_SETTINGS_MENU_ITEMS = 6;
const char* settingsMenuItems[] = {"SYS TELEMETRY", "DISPLAY THEME", "CONTROL MAPPER", "STORAGE/SCORES", "NETWORK CONFIG", "REBOOT SYSTEM"};

// Added DISCONNECT WIFI option
int netMenuIndex = 0;
const int TOTAL_NET_MENU_ITEMS = 3;
const char* netMenuItems[] = {"NET TELEMETRY", "SCAN CHANNELS", "DISCONNECT WIFI"};

int lastHomeMin = -1;

// Clock Context
bool swRunning = false; unsigned long swStartTime = 0; unsigned long swElapsedTime = 0; int lastSwSec = -1;
bool tmRunning = false; unsigned long tmDuration = 60000; unsigned long tmRemaining = 60000; unsigned long tmTargetEndTime = 0; int lastTmSec = -1; int lastBarWidth = -1;
enum SyncStep { SYNC_IDLE, SYNC_CONNECT, SYNC_FETCH, SYNC_OFF };
SyncStep syncStep = SYNC_IDLE; unsigned long syncTimer = 0; int lastHeaderMinute = -1;
const char* const DAYS_OF_WEEK[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};

// ToDo Context
#define MAX_TODOS 12
struct ToDoItem { uint32_t id; char text[36]; char priority[10]; bool done; };
ToDoItem todoList[MAX_TODOS]; int totalTodos = 0; int todoScrollIndex = 0; int todoSelectedIdx = 0;

// Stored Networks Context
#define MAX_SAVED_NETWORKS 5
struct SavedNetwork { char ssid[33]; char pass[65]; };
SavedNetwork savedNetworks[MAX_SAVED_NETWORKS];
int totalSavedNetworks = 0;

void loadSavedNetworks() {
  prefs.begin("retro_nets", true);
  totalSavedNetworks = prefs.getInt("net_count", 0);
  if (totalSavedNetworks > MAX_SAVED_NETWORKS) totalSavedNetworks = MAX_SAVED_NETWORKS;
  for (int i = 0; i < totalSavedNetworks; i++) {
    String pfx = "n_" + String(i);
    String s = prefs.getString((pfx + "_s").c_str(), "");
    String p = prefs.getString((pfx + "_p").c_str(), "");
    strncpy(savedNetworks[i].ssid, s.c_str(), 32); savedNetworks[i].ssid[32] = '\0';
    strncpy(savedNetworks[i].pass, p.c_str(), 64); savedNetworks[i].pass[64] = '\0';
  }
  prefs.end();
  
  if (totalSavedNetworks == 0) {
    strncpy(savedNetworks[0].ssid, WIFI_SSID, 32);
    strncpy(savedNetworks[0].pass, WIFI_PASSWORD, 64);
    totalSavedNetworks = 1;
    prefs.begin("retro_nets", false);
    prefs.putInt("net_count", 1);
    prefs.putString("n_0_s", WIFI_SSID);
    prefs.putString("n_0_p", WIFI_PASSWORD);
    prefs.end();
  }
}

void saveSavedNetwork(const char* ssid, const char* pass) {
  for (int i = 0; i < totalSavedNetworks; i++) {
    if (strcmp(savedNetworks[i].ssid, ssid) == 0) {
      strncpy(savedNetworks[i].pass, pass, 64);
      prefs.begin("retro_nets", false);
      prefs.putString(("n_" + String(i) + "_p").c_str(), pass);
      prefs.end();
      return;
    }
  }
  if (totalSavedNetworks < MAX_SAVED_NETWORKS) {
    strncpy(savedNetworks[totalSavedNetworks].ssid, ssid, 32);
    strncpy(savedNetworks[totalSavedNetworks].pass, pass, 64);
    totalSavedNetworks++;
    prefs.begin("retro_nets", false);
    prefs.putInt("net_count", totalSavedNetworks);
    prefs.putString(("n_" + String(totalSavedNetworks - 1) + "_s").c_str(), ssid);
    prefs.putString(("n_" + String(totalSavedNetworks - 1) + "_p").c_str(), pass);
    prefs.end();
  }
}

// --- GAME Contexts ---
#define MAX_BULLETS 4
#define MAX_ENEMIES 4
struct Bullet { int x, y; bool active; }; struct Enemy { int x, y; int type; bool active; };
int playerX = 64; int playerLastX = 64; const int playerY = 142;
Bullet bullets[MAX_BULLETS]; Enemy enemies[MAX_ENEMIES];
long siScore = 0; long siHighScore = 0; unsigned long lastSiTick = 0; unsigned long lastEnemySpawn = 0; uint8_t enemySubstep = 0;

#define SNAKE_MAX_LEN 128
#define GRID_CELL_SIZE 4
#define GRID_OFFSET_X  8
#define GRID_OFFSET_Y  20
#define GRID_COLS      28
#define GRID_ROWS      31
enum SnakeDir { DIR_UP, DIR_RIGHT, DIR_DOWN, DIR_LEFT }; struct Point { int8_t x, y; };
Point snakeBody[SNAKE_MAX_LEN]; int snakeLen = 4; SnakeDir snakeHeading = DIR_UP; Point foodPos;
long snScore = 0; long snHighScore = 0; unsigned long lastSnakeTick = 0; unsigned long snakeSpeedMs = 130;

#define BIRD_PLAY_X1 6
#define BIRD_PLAY_X2 121
#define BIRD_PLAY_Y1 18
#define BIRD_PLAY_Y2 154
#define BIRD_X_POS   28
#define BIRD_GAP_HEIGHT 36
#define BIRD_PIPE_W  14
struct Pipe { int x; int gapY; bool passed; bool active; };
float birdY = 70.0; float birdLastY = 70.0; float birdVel = 0.0; Pipe pipes[2];
long fbScore = 0; long fbHighScore = 0; unsigned long lastFbTick = 0;

#define FW_ROWS 4
#define FW_COLS 5
struct FwBrick { int x, y, w, h; bool active; uint16_t color; };
FwBrick fwBricks[FW_ROWS * FW_COLS];
float fwBallX, fwBallY, fwBallVx, fwBallVy;
int fwPadX = 64;
long fwScore = 0; long fwHighScore = 0;
unsigned long lastFwTick = 0;

#define NO_MAX_CARS 4
struct NoCar { float y; int lane; bool active; };
NoCar noCars[NO_MAX_CARS];
int noPlayerLane = 1; // 0, 1, 2
float noBaseSpeed = 2.0; bool noOverdrive = false;
long noScore = 0; long noHighScore = 0;
unsigned long lastNoTick = 0; unsigned long lastNoSpawn = 0;

#define LD_PTS 12
struct LdPt { int x, y; };
LdPt ldTerrain[LD_PTS];
int ldPadIdx;
float ldX, ldY, ldVx, ldVy, ldAngle, ldFuel;
bool ldLastThrust = false;
long ldScore = 0; long ldHighScore = 0;
unsigned long lastLdTick = 0;

// --- Settings/Network Context ---
int scanNetworkCount = -1; int scanScrollIndex = 0; int scanSelectedIndex = 0;
char wifiTargetSSID[33] = ""; char wifiTargetPass[65] = ""; unsigned long wifiConnectTimer = 0;
bool fridayServerRunning = false; File uploadFile;

// --- Text Reader Context ---
#define MAX_READER_FILES 16
#define READER_PAGE_LINES 11
#define READER_LINE_MAX_CHARS 20
String readerFiles[MAX_READER_FILES]; int totalReaderFiles = 0; int readerFileSelected = 0; int readerFileScroll = 0;
String currentReadingPath = ""; size_t readerTotalSize = 0; size_t readerPageOffsets[64]; int readerCurrentPage = 0; int readerTotalPages = 1;

// --- Keyboard Context ---
#define KB_MAX_CHARS 32
char kbText[KB_MAX_CHARS + 1] = ""; int kbTextLen = 0; int kbActiveRow = 0; int kbCursors[4] = {0, 0, 0, 0};
bool kbNeedsRender = true; bool lastKbBlink = false;
const char kbRow0[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"; const char kbRow1[] = "0123456789"; const char kbRow2[] = "!@#$%^&*()_+-=[]{};':\",./<>?";
const char* kbRow3[] = {"SPACE", "DEL", "ENTER", "CANCEL"}; const int kbLengths[] = {26, 10, 28, 4};

// --- E.D.I.T.H Context (Network Diagnostics) ---
enum EdithView { EDITH_VIEW_MENU, EDITH_VIEW_SUBNET_SCAN, EDITH_VIEW_SIGNAL_MAPPER, EDITH_VIEW_WIFI_LINK };
EdithView currentEdithView = EDITH_VIEW_MENU;
int edithMenuIndex = 0;
const int TOTAL_EDITH_MENU_ITEMS = 3;
const char* edithMenuItems[] = {"SUBNET DISCOVERY", "RSSI MAPPER", "LINK DEFAULT WIFI"};

unsigned long lastEdithTick = 0;
int currentPingHost = 1;
int activeHostsCount = 0;
bool subnetScanActive = false;

void setBootState(BootState newState);
void initHomeScreen(); void updateHomeScreen();
void initRollMenuScreen(); void transitionRollMenu(int direction);
void launchActiveApp();
void launchClockApp(); void initClockMenuView(); void renderClockMenu(); void updateClockApp(InputAction &input);

void launchGamesApp(); void initGamesListMenu(); void renderGamesListMenu(); void initGameActionMenu(); void renderGameActionMenu(); void updateGamesApp(InputAction &input);
void initSpaceImpact(); void updateSpaceImpact(InputAction input);
void initSnake(); void spawnSnakeFood(); void updateSnake(InputAction input);
void initCyberBird(); void updateCyberBird(InputAction input);
void initFirewall(); void updateFirewall(InputAction input);
void initOverdrive(); void updateOverdrive(InputAction input);
void initLunar(); void updateLunar(InputAction input);
void initGameOverView(); void initHighScoresView();

void launchSettingsApp(); void initSettingsMenuView(); void renderSettingsMenu(); void updateSettingsApp(InputAction &input);
void initTelemetryView(); void initThemeView(); void renderThemeView(); void initControlMapView(); void renderControlMapView();
void initStorageView(); void initNetMenuView(); void renderNetMenuView(); void initNetworkView(); void initWifiScanView(); void renderWifiScanView(); void initWifiConnectingView(); void initRebootView(); void executeRebootSequence();

void launchFridayApp(); void updateFridayApp(InputAction &input); void setupFridayServer(); void stopFridayServer();
void launchTodoApp(); void updateTodoApp(InputAction &input); void renderTodoList(); void renderTodoActionMenu(); void loadTodos(); void saveTodos();
void launchReaderApp(); void updateReaderApp(InputAction &input); void scanReaderFiles(); void renderReaderFileList(); void renderReaderActionMenu(); void openReaderDocument(const String& path); void renderReaderPage();
void launchKeyboardApp(KbTargetAction target = KB_TARGET_NONE); void updateKeyboardApp(InputAction &input);

void launchEdithApp(); void updateEdithApp(InputAction &input);
void initEdithMenu(); void renderEdithMenu();
void initEdithSubnetScan(); void initEdithSignalMapper(); void initEdithWifiLink();

// =============================================================================
// INPUT ENGINE: LOGICAL ACTION MAPPER & FILTER
// =============================================================================
const unsigned long BTN_DEBOUNCE_MS = 35;
struct ButtonChannel { uint8_t pin; bool isPressed; unsigned long pressStartTime; };
ButtonChannel buttons[] = { {PIN_BTN_NAV, false, 0}, {PIN_BTN_PREV, false, 0}, {PIN_BTN_SEL, false, 0}, {PIN_BTN_BACK, false, 0} };
const uint8_t TOTAL_BUTTONS = sizeof(buttons) / sizeof(buttons[0]);

InputAction mapLogicalToAction(uint8_t logical) {
  switch (logical) {
    case LOG_ACTION_RIGHT: return ACTION_NAV;
    case LOG_ACTION_LEFT:  return ACTION_PREV;
    case LOG_ACTION_SEL:   return ACTION_SELECT;
    case LOG_ACTION_ESC:   return ACTION_BACK;
    default:               return ACTION_NONE;
  }
}

InputAction pollInputs() {
  unsigned long now = millis();
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\r' || c == '\n' || c == ' ') continue;
    switch (c) {
      case 'n': case 'N': return ACTION_NAV;
      case 'p': case 'P': return ACTION_PREV;
      case 's': case 'S': return ACTION_SELECT;
      case 'b': case 'B': return ACTION_BACK;
      default: break;
    }
  }

#if ENABLE_PHYSICAL_BUTTONS
  for (uint8_t i = 0; i < TOTAL_BUTTONS; i++) {
    bool rawLow = (digitalRead(buttons[i].pin) == LOW);
    if (rawLow) {
      if (!buttons[i].isPressed) { buttons[i].isPressed = true; buttons[i].pressStartTime = now; }
    } else {
      if (buttons[i].isPressed) {
        unsigned long holdDuration = now - buttons[i].pressStartTime;
        buttons[i].isPressed = false;
        if (holdDuration >= BTN_DEBOUNCE_MS) return mapLogicalToAction(buttonActionMap[i]);
      }
    }
  }
#endif
  return ACTION_NONE;
}

// =============================================================================
// CAMERA-ROLL MENU DEFINITIONS & VECTOR ICONS
// =============================================================================
struct MenuItem { const char* title; void (*renderIcon)(int centerX, int centerY, bool isActive); };

void drawIconCall(int cx, int cy, bool isActive) {
  uint16_t primary = isActive ? COLOR_CYAN : COLOR_STEEL; uint16_t accent  = isActive ? COLOR_ICE  : COLOR_STEEL;
  tft.fillRect(cx - 7, cy - 8, 4, 3, primary); tft.fillRect(cx - 9, cy - 6, 4, 4, primary); tft.fillRect(cx - 5, cy - 5, 3, 3, primary);
  tft.fillRect(cx - 6, cy - 2, 4, 3, primary); tft.fillRect(cx - 4, cy,     4, 3, primary); tft.fillRect(cx - 2, cy + 2, 4, 3, primary); tft.fillRect(cx,     cy + 4, 4, 3, primary);
  tft.fillRect(cx + 2, cy + 5, 4, 3, primary); tft.fillRect(cx + 4, cy + 6, 4, 4, primary); tft.fillRect(cx + 7, cy + 4, 3, 4, primary);
  if (isActive) { tft.drawPixel(cx - 8, cy - 7, accent); tft.drawPixel(cx - 5, cy - 1, accent); tft.drawPixel(cx - 1, cy + 3, accent); tft.drawPixel(cx + 7, cy + 7, accent); }
}

void drawIconClock(int cx, int cy, bool isActive) {
  uint16_t primary = isActive ? COLOR_CYAN  : COLOR_STEEL; uint16_t accent  = isActive ? COLOR_AMBER : COLOR_STEEL;
  tft.fillRect(cx - 4, cy - 10, 8, 3, primary); tft.fillRect(cx - 4, cy + 8,  8, 3, primary);
  tft.drawRoundRect(cx - 9, cy - 7, 18, 15, 3, primary); tft.drawRect(cx - 7, cy - 5, 14, 11, primary);
  if (isActive) { tft.drawPixel(cx, cy, accent); tft.drawFastVLine(cx, cy - 3, 3, accent); tft.drawFastHLine(cx, cy, 4, accent); }
}

void drawIconGames(int cx, int cy, bool isActive) {
  uint16_t deckColor = isActive ? COLOR_ICE : COLOR_STEEL; uint16_t stickColor = isActive ? COLOR_CYAN : COLOR_STEEL;
  uint16_t ballColor = isActive ? HW_COLOR_RED : COLOR_STEEL; uint16_t btnAColor = isActive ? HW_COLOR_RED : COLOR_STEEL; uint16_t btnBColor = isActive ? COLOR_AMBER : COLOR_STEEL;
  tft.fillCircle(cx, cy - 6, 3, ballColor); tft.drawFastVLine(cx, cy - 3, 5, stickColor); tft.drawFastVLine(cx - 1, cy - 1, 3, stickColor); tft.drawFastVLine(cx + 1, cy - 1, 3, stickColor);
  tft.drawRoundRect(cx - 9, cy + 2, 19, 9, 2, deckColor); tft.drawFastHLine(cx - 8, cy + 8, 17, deckColor);
  tft.drawPixel(cx - 5, cy + 5, btnAColor); tft.drawPixel(cx + 4, cy + 5, btnBColor);
}

void drawIconFriday(int cx, int cy, bool isActive) {
  uint16_t ringOuter = isActive ? COLOR_CYAN : COLOR_STEEL; uint16_t ringInner = isActive ? COLOR_ICE : COLOR_STEEL; uint16_t core = isActive ? COLOR_WHITE : COLOR_STEEL;
  tft.drawCircle(cx, cy, 9, ringOuter); tft.drawCircle(cx, cy, 6, ringInner); tft.fillCircle(cx, cy, 2, core);
  if (isActive) { tft.drawFastHLine(cx - 10, cy, 3, COLOR_CYAN); tft.drawFastHLine(cx + 8, cy, 3, COLOR_CYAN); tft.drawFastVLine(cx, cy - 10, 3, COLOR_CYAN); tft.drawFastVLine(cx, cy + 8, 3, COLOR_CYAN); }
}

void drawIconEdith(int cx, int cy, bool isActive) {
  uint16_t crossCol = isActive ? COLOR_CYAN : COLOR_STEEL; uint16_t targetCol = isActive ? HW_COLOR_RED : COLOR_STEEL;
  tft.drawCircle(cx, cy, 8, crossCol);
  tft.drawFastHLine(cx - 12, cy, 6, crossCol); tft.drawFastHLine(cx + 7, cy, 6, crossCol);
  tft.drawFastVLine(cx, cy - 12, 6, crossCol); tft.drawFastVLine(cx, cy + 7, 6, crossCol);
  if(isActive) { tft.fillCircle(cx, cy, 2, targetCol); tft.drawPixel(cx - 4, cy - 4, targetCol); tft.drawPixel(cx + 4, cy + 4, targetCol); }
}

void drawIconTodo(int cx, int cy, bool isActive) {
  uint16_t boxCol = isActive ? COLOR_CYAN : COLOR_STEEL; uint16_t lineCol = isActive ? COLOR_AMBER : COLOR_STEEL; uint16_t check = isActive ? COLOR_LIME : COLOR_STEEL;
  tft.drawRect(cx - 8, cy - 8, 16, 16, boxCol); tft.drawFastHLine(cx - 2, cy - 3, 7, lineCol); tft.drawFastHLine(cx - 2, cy + 2, 7, lineCol);
  tft.drawPixel(cx - 6, cy - 2, check); tft.drawPixel(cx - 5, cy - 1, check); tft.drawPixel(cx - 4, cy - 3, check);
}

void drawIconReader(int cx, int cy, bool isActive) {
  uint16_t pageCol = isActive ? COLOR_CYAN : COLOR_STEEL; uint16_t spine = isActive ? COLOR_AMBER : COLOR_STEEL; uint16_t txt = isActive ? COLOR_ICE : COLOR_STEEL;
  tft.drawFastVLine(cx, cy - 8, 16, spine); tft.drawRect(cx - 9, cy - 8, 9, 16, pageCol); tft.drawRect(cx, cy - 8, 10, 16, pageCol);
  tft.drawFastHLine(cx - 7, cy - 4, 5, txt); tft.drawFastHLine(cx - 7, cy - 1, 5, txt); tft.drawFastHLine(cx - 7, cy + 2, 5, txt);
  tft.drawFastHLine(cx + 3, cy - 4, 5, txt); tft.drawFastHLine(cx + 3, cy - 1, 5, txt); tft.drawFastHLine(cx + 3, cy + 2, 5, txt);
}

void drawIconKeyboard(int cx, int cy, bool isActive) {
  uint16_t primary = isActive ? COLOR_CYAN : COLOR_STEEL; uint16_t accent = isActive ? COLOR_AMBER : COLOR_STEEL;
  tft.drawRoundRect(cx - 10, cy - 6, 20, 12, 2, primary);
  tft.drawFastHLine(cx - 7, cy - 3, 3, accent); tft.drawFastHLine(cx - 2, cy - 3, 3, accent); tft.drawFastHLine(cx + 3, cy - 3, 3, accent);
  tft.drawFastHLine(cx - 7, cy, 3, accent); tft.drawFastHLine(cx - 2, cy, 3, accent); tft.drawFastHLine(cx + 3, cy, 3, accent);
  tft.drawFastHLine(cx - 5, cy + 3, 10, primary); 
}

void drawIconMessages(int cx, int cy, bool isActive) {
  uint16_t bubbleColor = isActive ? COLOR_CYAN : COLOR_STEEL; uint16_t linesColor = isActive ? COLOR_AMBER : COLOR_STEEL;
  tft.drawRoundRect(cx - 9, cy - 9, 18, 15, 3, bubbleColor); tft.drawFastVLine(cx + 8, cy + 2, 5, bubbleColor); tft.drawLine(cx + 8, cy + 7, cx + 4, cy + 5, bubbleColor); tft.drawPixel(cx + 7, cy + 5, COLOR_BG);
  tft.drawFastHLine(cx - 5, cy - 5, 10, linesColor); tft.drawFastHLine(cx - 5, cy - 2, 10, linesColor); tft.drawFastHLine(cx - 5, cy + 1, 7,  linesColor);
}

void drawIconSettings(int cx, int cy, bool isActive) {
  uint16_t gearColor = isActive ? COLOR_CYAN : COLOR_STEEL;
  int g1x = cx - 3; int g1y = cy - 4; tft.drawCircle(g1x, g1y, 4, gearColor); tft.drawPixel(g1x, g1y, COLOR_BG); tft.drawFastVLine(g1x, g1y - 6, 2, gearColor); tft.drawFastVLine(g1x, g1y + 5, 2, gearColor); tft.drawFastHLine(g1x - 6, g1y, 2, gearColor); tft.drawFastHLine(g1x + 5, g1y, 2, gearColor);
  int g2x = cx + 4; int g2y = cy + 3; tft.drawCircle(g2x, g2y, 3, gearColor); tft.drawFastVLine(g2x, g2y - 5, 2, gearColor); tft.drawFastVLine(g2x, g2y + 4, 2, gearColor); tft.drawFastHLine(g2x - 5, g2y, 2, gearColor); tft.drawFastHLine(g2x + 4, g2y, 2, gearColor);
}

const MenuItem MENU_ITEMS[] = {
  {"CALL",        drawIconCall}, {"CLOCK",       drawIconClock}, {"GAMES",       drawIconGames},
  {"F.R.I.D.A.Y", drawIconFriday}, {"E.D.I.T.H", drawIconEdith}, {"READER",      drawIconReader}, 
  {"KEYBOARD",    drawIconKeyboard}, {"TODO",        drawIconTodo}, {"MESSAGES",    drawIconMessages}, 
  {"SETTINGS",    drawIconSettings}
};
const uint8_t TOTAL_MENU_ITEMS = sizeof(MENU_ITEMS) / sizeof(MENU_ITEMS[0]);
int activeRollIndex = 0;

struct BootContext {
  BootState currentState; unsigned long stateStartTime; unsigned long lastFrameTime;
  
  uint8_t linuxLogIndex;
  int lastRasterHeight;
  int horizonScanOffset;

  static constexpr const char* TARGET_GREETING = "HELLO SLOKE";
  static constexpr uint8_t GREETING_LEN = 11;
  char displayBuffer[12];
  uint8_t resolvedChars;
  uint8_t scrambleCycles;
} bootCtx;

// =============================================================================
// GRAPHICAL ATOMS: HUD & HOMESCREEN
// =============================================================================
void drawIndustrialHUD() {
  tft.drawFastHLine(3, 3, 14, COLOR_CYAN); tft.drawFastVLine(3, 3, 14, COLOR_CYAN); tft.drawFastHLine(111, 3, 14, COLOR_CYAN); tft.drawFastVLine(124, 3, 14, COLOR_CYAN);
  tft.drawFastHLine(3, 156, 14, COLOR_CYAN); tft.drawFastVLine(3, 145, 14, COLOR_CYAN); tft.drawFastHLine(111, 156, 14, COLOR_CYAN); tft.drawFastVLine(124, 145, 14, COLOR_CYAN);
  for (int y = 24; y <= 136; y += 16) { tft.drawFastHLine(5, y, 2, COLOR_STEEL); tft.drawFastHLine(121, y, 2, COLOR_STEEL); }
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(20, 8); tft.print("SYS://OS_ONLINE");
}

void initHomeScreen() {
  currentAppState = STATE_HOMESCREEN; lastHomeMin = -1;
  tft.fillScreen(COLOR_BG); drawIndustrialHUD();
  tft.drawFastHLine(10, 22, 108, COLOR_STEEL);
  tft.drawRect(10, 28, 108, 44, COLOR_CYAN); tft.drawFastHLine(14, 28, 8, COLOR_AMBER); tft.drawFastHLine(106, 28, 8, COLOR_AMBER); tft.drawFastHLine(14, 71, 8, COLOR_AMBER); tft.drawFastHLine(106, 71, 8, COLOR_AMBER);
  tft.drawRect(10, 78, 108, 18, COLOR_STEEL);
  tft.setTextSize(1); tft.setTextColor(COLOR_STEEL, COLOR_BG); tft.setCursor(12, 102); tft.print("RADIO: SHUTDOWN");
  tft.setCursor(12, 114); tft.print("PWR  : NORMAL");
  tft.drawRect(10, 128, 108, 20, COLOR_STEEL); tft.setTextSize(1); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(52, 134); tft.print("menu");
  updateHomeScreen();
}

void updateHomeScreen() {
  time_t rawTime; struct tm ti; time(&rawTime); localtime_r(&rawTime, &ti);
  if (ti.tm_min != lastHomeMin) {
    lastHomeMin = ti.tm_min;
    char timeBuf[8]; char dateBuf[16];
    if (rtcHasBeenSynced) {
      snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", ti.tm_hour, ti.tm_min);
      snprintf(dateBuf, sizeof(dateBuf), "%04d.%02d.%02d %s", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday, DAYS_OF_WEEK[ti.tm_wday % 7]);
    } else {
      snprintf(timeBuf, sizeof(timeBuf), "--:--");
      snprintf(dateBuf, sizeof(dateBuf), "----.--.-- ---");
    }
    tft.fillRect(12, 30, 104, 40, COLOR_BG); tft.setTextSize(3); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.setCursor(19, 39); tft.print(timeBuf);
    tft.fillRect(12, 80, 104, 14, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(14, 83); tft.print(dateBuf);
    tft.fillCircle(112, 106, 2, rtcHasBeenSynced ? COLOR_LIME : HW_COLOR_RED); tft.fillRect(72, 114, 46, 8, COLOR_BG); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(72, 114); tft.print(rtcHasBeenSynced ? "SYNCED" : "OFFLINE");
  }
}

void drawViewfinderBox() {
  tft.drawRect(45, 51, 38, 38, COLOR_CYAN); tft.drawFastHLine(42, 51, 3, COLOR_AMBER); tft.drawFastVLine(45, 48, 3, COLOR_AMBER); tft.drawFastHLine(80, 51, 3, COLOR_AMBER); tft.drawFastVLine(82, 48, 3, COLOR_AMBER);
  tft.drawFastHLine(42, 88, 3, COLOR_AMBER); tft.drawFastVLine(45, 89, 3, COLOR_AMBER); tft.drawFastHLine(80, 88, 3, COLOR_AMBER); tft.drawFastVLine(82, 89, 3, COLOR_AMBER);
}

void initRollMenuScreen() {
  currentAppState = STATE_ROLL_MENU; tft.fillScreen(COLOR_BG); drawIndustrialHUD();
  for (int x = 12; x <= 116; x += 12) { tft.fillRect(x, 26, 6, 4, COLOR_STEEL); tft.fillRect(x, 130, 6, 4, COLOR_STEEL); }
  tft.drawFastHLine(8, 33, 112, COLOR_STEEL); tft.drawFastHLine(8, 127, 112, COLOR_STEEL);
  tft.drawRect(12, 60, 24, 24, COLOR_STEEL); tft.drawRect(92, 60, 24, 24, COLOR_STEEL); drawViewfinderBox();
  transitionRollMenu(0);
}

void transitionRollMenu(int direction) {
  int prevIndex = (activeRollIndex - 1 + TOTAL_MENU_ITEMS) % TOTAL_MENU_ITEMS; int nextIndex = (activeRollIndex + 1) % TOTAL_MENU_ITEMS;
  if (direction != 0) {
    tft.fillRect(46, 52, 36, 6, COLOR_BG); tft.fillRect(46, 82, 36, 6, COLOR_BG); delay(12);
    tft.fillRect(46, 52, 36, 36, COLOR_BG); tft.fillRect(13, 61, 22, 22, COLOR_BG); tft.fillRect(93, 61, 22, 22, COLOR_BG);
  } else {
    tft.fillRect(46, 52, 36, 36, COLOR_BG); tft.fillRect(13, 61, 22, 22, COLOR_BG); tft.fillRect(93, 61, 22, 22, COLOR_BG);
  }
  MENU_ITEMS[prevIndex].renderIcon(24, 72, false); MENU_ITEMS[nextIndex].renderIcon(104, 72, false); MENU_ITEMS[activeRollIndex].renderIcon(64, 70, true);
  drawViewfinderBox();
  tft.fillRect(8, 96, 112, 14, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG);
  int titleLen = strlen(MENU_ITEMS[activeRollIndex].title); int titleX = 64 - ((titleLen * 6) / 2); tft.setCursor(titleX, 98); tft.print(MENU_ITEMS[activeRollIndex].title);
  tft.drawFastHLine(titleX - 10, 101, 6, COLOR_LIME); tft.drawFastHLine(titleX + (titleLen * 6) + 4, 101, 6, COLOR_LIME);
  int totalDotsWidth = (TOTAL_MENU_ITEMS * 6) - 2; int startDotX = 64 - (totalDotsWidth / 2);
  for (int i = 0; i < TOTAL_MENU_ITEMS; i++) {
    int dotX = startDotX + (i * 6);
    if (i == activeRollIndex) { tft.fillRect(dotX, 114, 4, 4, COLOR_CYAN); } else { tft.fillRect(dotX, 114, 4, 4, COLOR_BG); tft.fillRect(dotX, 115, 2, 2, COLOR_STEEL); }
  }
}

void launchActiveApp() {
  if (strcmp(MENU_ITEMS[activeRollIndex].title, "CLOCK") == 0) { launchClockApp(); return; }
  else if (strcmp(MENU_ITEMS[activeRollIndex].title, "GAMES") == 0) { launchGamesApp(); return; }
  else if (strcmp(MENU_ITEMS[activeRollIndex].title, "SETTINGS") == 0) { launchSettingsApp(); return; }
  else if (strcmp(MENU_ITEMS[activeRollIndex].title, "F.R.I.D.A.Y") == 0) { launchFridayApp(); return; }
  else if (strcmp(MENU_ITEMS[activeRollIndex].title, "E.D.I.T.H") == 0) { launchEdithApp(); return; }
  else if (strcmp(MENU_ITEMS[activeRollIndex].title, "TODO") == 0) { launchTodoApp(); return; }
  else if (strcmp(MENU_ITEMS[activeRollIndex].title, "READER") == 0) { launchReaderApp(); return; }
  else if (strcmp(MENU_ITEMS[activeRollIndex].title, "KEYBOARD") == 0) { launchKeyboardApp(KB_TARGET_NONE); return; }

  currentAppState = STATE_APP_ACTIVE; tft.fillScreen(COLOR_BLACK); drawIndustrialHUD();
  tft.setTextSize(1); tft.setTextColor(COLOR_CYAN, COLOR_BLACK); tft.setCursor(24, 45); tft.print("RUNNING MODULE:");
  tft.setTextSize(2); tft.setTextColor(COLOR_AMBER, COLOR_BLACK); tft.setCursor(24, 65); tft.print(MENU_ITEMS[activeRollIndex].title);
  tft.setTextSize(1); tft.setTextColor(COLOR_ACT_ESC, COLOR_BLACK); tft.setCursor(46, 120); tft.print("escape");
}

void drawCharCarousel(int row, int y, const char* chars, bool isActive) {
  int len = kbLengths[row]; int selected = kbCursors[row];
  tft.fillRect(10, y - 6, 108, 16, COLOR_BG);
  for (int offset = -3; offset <= 3; offset++) {
    int idx = (selected + offset + len) % len; int x = 64 + (offset * 14);
    if (x > 10 && x < 118) {
      tft.setTextSize(1);
      if (offset == 0) {
        tft.setTextColor(isActive ? COLOR_BLACK : COLOR_CYAN, isActive ? COLOR_CYAN : COLOR_BG);
        tft.fillRect(x - 5, y - 4, 11, 15, isActive ? COLOR_CYAN : COLOR_BG);
        if (!isActive) tft.drawRect(x - 5, y - 4, 11, 15, COLOR_STEEL);
      } else { tft.setTextColor(COLOR_STEEL, COLOR_BG); }
      tft.setCursor(x - 2, y); tft.print(chars[idx]);
    }
  }
}

void drawWordCarousel(int row, int y, const char** words, bool isActive) {
  int len = kbLengths[row]; int selected = kbCursors[row];
  tft.fillRect(10, y - 6, 108, 16, COLOR_BG);
  for (int offset = -1; offset <= 1; offset++) {
    int idx = (selected + offset + len) % len; int x = 64 + (offset * 38);
    if (x > 0 && x < 128) {
      int wordLen = strlen(words[idx]); int textX = x - (wordLen * 6) / 2; tft.setTextSize(1);
      if (offset == 0) {
        tft.setTextColor(isActive ? COLOR_BLACK : COLOR_CYAN, isActive ? COLOR_CYAN : COLOR_BG);
        tft.fillRect(textX - 4, y - 4, (wordLen * 6) + 8, 15, isActive ? COLOR_CYAN : COLOR_BG);
        if (!isActive) tft.drawRect(textX - 4, y - 4, (wordLen * 6) + 8, 15, COLOR_STEEL);
      } else { tft.setTextColor(COLOR_STEEL, COLOR_BG); }
      tft.setCursor(textX, y); tft.print(words[idx]);
    }
  }
}

void launchKeyboardApp(KbTargetAction target) {
  currentAppState = STATE_APP_KEYBOARD; currentKbTarget = target;
  kbText[0] = '\0'; kbTextLen = 0; kbActiveRow = 0; memset(kbCursors, 0, sizeof(kbCursors));
  kbNeedsRender = true; lastKbBlink = false;
  tft.fillScreen(COLOR_BG); drawIndustrialHUD(); tft.drawRect(10, 10, 108, 22, COLOR_STEEL);
}

void updateKeyboardApp(InputAction &input) {
  if (input == ACTION_BACK) { kbActiveRow = (kbActiveRow + 1) % 4; kbNeedsRender = true; input = ACTION_NONE; }
  else if (input == ACTION_NAV) { kbCursors[kbActiveRow] = (kbCursors[kbActiveRow] + 1) % kbLengths[kbActiveRow]; kbNeedsRender = true; }
  else if (input == ACTION_PREV) { kbCursors[kbActiveRow] = (kbCursors[kbActiveRow] - 1 + kbLengths[kbActiveRow]) % kbLengths[kbActiveRow]; kbNeedsRender = true; }
  else if (input == ACTION_SELECT) {
    kbNeedsRender = true;
    if (kbActiveRow == 3) {
      if (kbCursors[3] == 0 && kbTextLen < KB_MAX_CHARS) { kbText[kbTextLen++] = ' '; kbText[kbTextLen] = '\0'; }
      else if (kbCursors[3] == 1 && kbTextLen > 0) { kbText[--kbTextLen] = '\0'; }
      else if (kbCursors[3] == 2) { 
        if (currentKbTarget == KB_TARGET_WIFI_PASS) {
          strncpy(wifiTargetPass, kbText, 64);
          saveSavedNetwork(wifiTargetSSID, wifiTargetPass);
          currentAppState = STATE_APP_SETTINGS; initWifiConnectingView(); input = ACTION_NONE; return;
        } else if (currentKbTarget == KB_TARGET_TODO_ADD) {
          if (totalTodos < MAX_TODOS && kbTextLen > 0) {
            todoList[totalTodos].id = millis(); strncpy(todoList[totalTodos].text, kbText, 35); todoList[totalTodos].text[35] = '\0';
            strcpy(todoList[totalTodos].priority, "ROUTINE"); todoList[totalTodos].done = false; totalTodos++; saveTodos();
          }
          currentAppState = STATE_APP_TODO; launchTodoApp(); input = ACTION_NONE; return;
        } else if (currentKbTarget == KB_TARGET_FILE_RENAME) {
          if (kbTextLen > 0) {
            String oldExt = ""; int dotIdx = currentReadingPath.lastIndexOf('.'); if (dotIdx != -1) oldExt = currentReadingPath.substring(dotIdx);
            String newPath = "/" + String(kbText) + oldExt; LittleFS.rename(currentReadingPath, newPath);
          }
          currentAppState = STATE_APP_READER; launchReaderApp(); input = ACTION_NONE; return;
        } else {
          kbText[0] = '\0'; kbTextLen = 0;
        }
      } else if (kbCursors[3] == 3) { 
        if (currentKbTarget == KB_TARGET_WIFI_PASS) { currentAppState = STATE_APP_SETTINGS; initWifiScanView(); }
        else if (currentKbTarget == KB_TARGET_TODO_ADD) { currentAppState = STATE_APP_TODO; launchTodoApp(); }
        else if (currentKbTarget == KB_TARGET_FILE_RENAME) { currentAppState = STATE_APP_READER; currentReaderView = READER_VIEW_FILE_ACTION; renderReaderActionMenu(); }
        else { initRollMenuScreen(); }
        input = ACTION_NONE; return;
      }
    } else if (kbTextLen < KB_MAX_CHARS) {
      char c = (kbActiveRow == 0) ? kbRow0[kbCursors[0]] : (kbActiveRow == 1) ? kbRow1[kbCursors[1]] : kbRow2[kbCursors[2]];
      kbText[kbTextLen++] = c; kbText[kbTextLen] = '\0';
    }
  }

  bool currentBlink = (millis() / 400) % 2 == 0;
  if (currentBlink != lastKbBlink || kbNeedsRender) {
    lastKbBlink = currentBlink; tft.fillRect(11, 11, 106, 20, COLOR_BG);
    tft.setTextSize(1); tft.setTextColor(COLOR_WHITE, COLOR_BG);
    int startIdx = 0; if (kbTextLen > 15) startIdx = kbTextLen - 15;
    tft.setCursor(14, 18); tft.print(&kbText[startIdx]);
    if (currentBlink && kbTextLen < KB_MAX_CHARS) {
       int drawLen = (kbTextLen > 15) ? 15 : kbTextLen; int cursorX = 14 + (drawLen * 6);
       tft.drawFastVLine(cursorX, 16, 10, COLOR_CYAN);
    }
  }

  if (kbNeedsRender) {
    drawCharCarousel(0, 48, kbRow0, kbActiveRow == 0); drawCharCarousel(1, 74, kbRow1, kbActiveRow == 1);
    drawCharCarousel(2, 100, kbRow2, kbActiveRow == 2); drawWordCarousel(3, 126, kbRow3, kbActiveRow == 3);
    kbNeedsRender = false;
  }
}

// =============================================================================
// APP: E.D.I.T.H. TACTICAL NETWORK DIAGNOSTICS MODULE
// =============================================================================
void renderEdithMenu() {
  tft.fillRect(8, 24, 112, 110, COLOR_BG);
  for (int i = 0; i < TOTAL_EDITH_MENU_ITEMS; i++) {
    int y = 32 + (i * 30); bool isSel = (i == edithMenuIndex);
    if (isSel) { tft.fillRect(12, y, 104, 22, COLOR_CYAN); tft.setTextColor(COLOR_BLACK, COLOR_CYAN); tft.setCursor(20, y + 7); tft.print("> "); tft.print(edithMenuItems[i]); }
    else { tft.drawRect(12, y, 104, 22, COLOR_STEEL); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(24, y + 7); tft.print(edithMenuItems[i]); }
  }
  tft.fillRect(8, 136, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(20, 138); tft.print("enter");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("escape");
}

void initEdithMenu() {
  currentEdithView = EDITH_VIEW_MENU; edithMenuIndex = 0; tft.fillScreen(COLOR_BG);
  drawIndustrialHUD();
  tft.fillRect(8, 6, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(16, 8); tft.print("E.D.I.T.H. TACTICAL"); tft.drawFastHLine(8, 20, 112, COLOR_STEEL);
  renderEdithMenu();
}

void initEdithSubnetScan() {
  currentEdithView = EDITH_VIEW_SUBNET_SCAN;
  currentPingHost = 1;
  activeHostsCount = 0;
  subnetScanActive = true;
  
  tft.fillScreen(COLOR_BG); drawIndustrialHUD();
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(12, 14); tft.print("SUBNET DISCOVERY");
  tft.drawFastHLine(10, 24, 108, COLOR_STEEL);
  tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.setCursor(14, 32); tft.print("LOCAL PING SCAN");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(46, 145); tft.print("escape");
}

void initEdithSignalMapper() {
  currentEdithView = EDITH_VIEW_SIGNAL_MAPPER;
  lastEdithTick = millis();
  tft.fillScreen(COLOR_BG); drawIndustrialHUD();
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(12, 14); tft.print("RSSI SIGNAL MAPPER");
  tft.drawFastHLine(10, 24, 108, COLOR_STEEL);
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(46, 145); tft.print("escape");
  
  WiFi.mode(WIFI_STA);
  WiFi.scanNetworks(true, true);
}

void initEdithWifiLink() {
  currentEdithView = EDITH_VIEW_WIFI_LINK;
  tft.fillScreen(COLOR_BG); drawIndustrialHUD();
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(12, 14); tft.print("DEFAULT NETWORK");
  tft.drawFastHLine(10, 24, 108, COLOR_STEEL);
  tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.setCursor(14, 40); tft.print("LINKING SSID:");
  tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.setCursor(14, 52); tft.print(WIFI_SSID);
  tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(14, 75); tft.print("ESTABLISHING...");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(46, 145); tft.print("escape");

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  lastEdithTick = millis();
}

void launchEdithApp() {
  currentAppState = STATE_APP_EDITH; initEdithMenu();
}

void updateEdithApp(InputAction &input) {
  unsigned long now = millis();
  
  if (currentEdithView == EDITH_VIEW_MENU) {
    if (input == ACTION_BACK) { 
      // Changed: No longer explicitly shutting down WiFi radio here to preserve active connection
      initRollMenuScreen(); 
      input = ACTION_NONE; 
      return; 
    }
    if (input == ACTION_NAV) { edithMenuIndex = (edithMenuIndex + 1) % TOTAL_EDITH_MENU_ITEMS; renderEdithMenu(); }
    else if (input == ACTION_PREV) { edithMenuIndex = (edithMenuIndex - 1 + TOTAL_EDITH_MENU_ITEMS) % TOTAL_EDITH_MENU_ITEMS; renderEdithMenu(); }
    else if (input == ACTION_SELECT) {
      if (edithMenuIndex == 0) initEdithSubnetScan();
      else if (edithMenuIndex == 1) initEdithSignalMapper();
      else if (edithMenuIndex == 2) initEdithWifiLink();
    }
  } 
  else if (currentEdithView == EDITH_VIEW_SUBNET_SCAN) {
    if (input == ACTION_BACK) { currentEdithView = EDITH_VIEW_MENU; renderEdithMenu(); input = ACTION_NONE; return; }
    if (subnetScanActive && now - lastEdithTick > 150) {
      lastEdithTick = now;
      if (WiFi.status() == WL_CONNECTED) {
        IPAddress localIp = WiFi.localIP();
        IPAddress targetIp(localIp[0], localIp[1], localIp[2], currentPingHost);
        
        tft.fillRect(14, 45, 100, 75, COLOR_BG);
        tft.setTextSize(1); tft.setTextColor(COLOR_ICE, COLOR_BG);
        tft.setCursor(14, 48); tft.print("PROBING IP:");
        tft.setCursor(14, 60); tft.print(targetIp.toString());
        
        WiFiClient testClient;
        if (testClient.connect(targetIp, 80, 120) || testClient.connect(targetIp, 443, 120)) {
          activeHostsCount++;
          tft.setTextColor(COLOR_LIME, COLOR_BG);
          tft.setCursor(14, 78); tft.print("[ ACTIVE HOST ]");
          testClient.stop();
        } else {
          tft.setTextColor(COLOR_STEEL, COLOR_BG);
          tft.setCursor(14, 78); tft.print("NO RESPONSE");
        }
        
        tft.setTextColor(COLOR_AMBER, COLOR_BG);
        tft.setCursor(14, 102); tft.print("HOSTS UP: "); tft.print(activeHostsCount);
      } else {
        tft.fillRect(14, 60, 100, 20, COLOR_BG);
        tft.setTextColor(HW_COLOR_RED, COLOR_BG);
        tft.setCursor(14, 68); tft.print("WIFI NOT LINKED!");
      }
      
      currentPingHost++;
      if (currentPingHost > 25) { subnetScanActive = false; }
    }
  }
  else if (currentEdithView == EDITH_VIEW_SIGNAL_MAPPER) {
    if (input == ACTION_BACK) { WiFi.scanDelete(); currentEdithView = EDITH_VIEW_MENU; renderEdithMenu(); input = ACTION_NONE; return; }
    if (now - lastEdithTick > 1500) {
      lastEdithTick = now;
      int n = WiFi.scanComplete();
      if (n >= 0) {
        tft.fillRect(12, 30, 104, 100, COLOR_BG);
        tft.setTextSize(1);
        for(int i=0; i<min(n, 7); i++) {
          int y = 32 + (i * 14);
          tft.setTextColor(COLOR_CYAN, COLOR_BG);
          tft.setCursor(14, y);
          String ssid = WiFi.SSID(i); if(ssid.length() > 8) ssid = ssid.substring(0,7) + "..";
          tft.print(ssid);
          int rssi = WiFi.RSSI(i);
          tft.setCursor(72, y);
          tft.setTextColor((rssi > -65) ? COLOR_LIME : ((rssi > -80) ? COLOR_AMBER : HW_COLOR_RED), COLOR_BG);
          tft.print(rssi); tft.print("dBm");
        }
        WiFi.scanNetworks(true, true);
      }
    }
  }
  else if (currentEdithView == EDITH_VIEW_WIFI_LINK) {
    if (input == ACTION_BACK) { currentEdithView = EDITH_VIEW_MENU; renderEdithMenu(); input = ACTION_NONE; return; }
    if (WiFi.status() == WL_CONNECTED) {
      tft.fillRect(14, 70, 100, 50, COLOR_BG);
      tft.setTextColor(COLOR_LIME, COLOR_BG);
      tft.setCursor(14, 75); tft.print("[ LINK SUCCESS ]");
      tft.setTextColor(COLOR_WHITE, COLOR_BG);
      IPAddress ip = WiFi.localIP();
      tft.setCursor(14, 95); tft.print("IP: "); tft.print(ip.toString());
    } else if (now - lastEdithTick > 8000) {
      tft.fillRect(14, 70, 100, 30, COLOR_BG);
      tft.setTextColor(HW_COLOR_RED, COLOR_BG);
      tft.setCursor(14, 75); tft.print("TIMEOUT / OFFLINE");
    }
  }
}

void scanReaderFiles() {
  totalReaderFiles = 0; File root = LittleFS.open("/"); File f = root.openNextFile();
  while (f && totalReaderFiles < MAX_READER_FILES) {
    String n = String(f.name()); if (!n.startsWith("/")) n = "/" + n;
    if (!n.endsWith(".sys") && !n.endsWith(".dat") && !n.startsWith("/todo")) readerFiles[totalReaderFiles++] = n;
    f = root.openNextFile();
  }
  if (totalReaderFiles == 0) {
    File dummy = LittleFS.open("/welcome.txt", FILE_WRITE);
    if (dummy) { dummy.println("F.R.I.D.A.Y. OS"); dummy.println("Reading Mode Active."); dummy.close(); readerFiles[0] = "/welcome.txt"; totalReaderFiles = 1; }
  }
}

void renderReaderFileList() {
  tft.fillRect(8, 24, 112, 110, COLOR_BG);
  for (int i = 0; i < 4; i++) {
    int idx = readerFileScroll + i; if (idx >= totalReaderFiles) break;
    int y = 30 + (i * 24); bool isSel = (idx == readerFileSelected);
    if (isSel) { tft.fillRect(10, y, 108, 20, COLOR_CYAN); tft.setTextColor(COLOR_BLACK, COLOR_CYAN); } else { tft.drawRect(10, y, 108, 20, COLOR_STEEL); tft.setTextColor(COLOR_WHITE, COLOR_BG); }
    tft.setTextSize(1); tft.setCursor(14, y + 6); String displayName = readerFiles[idx];
    if (displayName.startsWith("/")) displayName = displayName.substring(1);
    if (displayName.length() > 14) displayName = displayName.substring(0, 13) + "..";
    tft.print(displayName);
  }
  tft.fillRect(8, 136, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(20, 138); tft.print("opts");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("escape");
}

void renderReaderActionMenu() {
  tft.fillRect(8, 24, 112, 110, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(14, 28); tft.print("FILE:"); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.setCursor(14, 40);
  String shortTitle = currentReadingPath; if (shortTitle.startsWith("/")) shortTitle = shortTitle.substring(1);
  if (shortTitle.length() > 14) shortTitle = shortTitle.substring(0, 13) + ".."; tft.print(shortTitle);
  tft.drawFastHLine(12, 52, 104, COLOR_STEEL);
  for (int i = 0; i < 3; i++) {
    int y = 60 + (i * 24); bool isSel = (i == readerActionIndex);
    if (isSel) { tft.fillRect(12, y, 104, 20, COLOR_CYAN); tft.setTextColor(COLOR_BLACK, COLOR_CYAN); tft.setCursor(18, y + 6); tft.print("> "); tft.print(readerActions[i]); }
    else { tft.drawRect(12, y, 104, 20, COLOR_STEEL); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(24, y + 6); tft.print(readerActions[i]); }
  }
  tft.fillRect(8, 136, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(20, 138); tft.print("enter");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("back");
}

void openReaderDocument(const String& path) {
  currentReadingPath = path; currentReaderView = READER_VIEW_READING; readerCurrentPage = 0; readerPageOffsets[0] = 0;
  File f = LittleFS.open(path, "r"); if (!f) return; readerTotalSize = f.size();
  int pageIdx = 0; int lineCount = 0;
  while (f.available() && pageIdx < 63) {
    int chars = 0; while (f.available()) { char c = f.read(); if (c == '\n') break; if (c != '\r') chars++; if (chars >= READER_LINE_MAX_CHARS) break; }
    lineCount++; if (lineCount >= READER_PAGE_LINES) { pageIdx++; readerPageOffsets[pageIdx] = f.position(); lineCount = 0; }
  }
  readerTotalPages = pageIdx + 1; f.close(); renderReaderPage();
}

void renderReaderPage() {
  tft.fillScreen(COLOR_BG); tft.fillRect(0, 0, 128, 16, COLOR_STEEL); tft.drawFastHLine(0, 16, 128, COLOR_CYAN);
  tft.setTextSize(1); tft.setTextColor(COLOR_CYAN, COLOR_STEEL); tft.setCursor(6, 4);
  String shortTitle = currentReadingPath; if (shortTitle.startsWith("/")) shortTitle = shortTitle.substring(1);
  if (shortTitle.length() > 9) shortTitle = shortTitle.substring(0, 8) + ".."; tft.print(shortTitle);
  char pageBuf[12]; snprintf(pageBuf, sizeof(pageBuf), "P.%02d/%02d", readerCurrentPage + 1, readerTotalPages);
  tft.setTextColor(COLOR_AMBER, COLOR_STEEL); tft.setCursor(78, 4); tft.print(pageBuf);
  tft.setTextSize(1); tft.setTextColor(COLOR_WHITE, COLOR_BG);
  File f = LittleFS.open(currentReadingPath, "r");
  if (f) {
    f.seek(readerPageOffsets[readerCurrentPage]); int line = 0;
    while (f.available() && line < READER_PAGE_LINES) {
      String lineStr = ""; while (f.available()) { char c = f.read(); if (c == '\n') break; if (c != '\r') lineStr += c; if (lineStr.length() >= READER_LINE_MAX_CHARS) break; }
      int yPos = 22 + (line * 11); tft.setCursor(6, yPos); tft.print(lineStr); line++;
    }
    f.close();
  }
  tft.drawFastHLine(0, 146, 128, COLOR_STEEL); int progW = map(readerCurrentPage + 1, 1, readerTotalPages, 8, 128); tft.fillRect(0, 146, progW, 2, COLOR_CYAN);
  tft.setTextSize(1); tft.setTextColor(COLOR_ACT_LEFT, COLOR_BG); tft.setCursor(6, 150); tft.print("<prev"); tft.setTextColor(COLOR_ACT_RIGHT, COLOR_BG); tft.setCursor(92, 150); tft.print("next>"); tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(46, 150); tft.print("escape");
}

void launchReaderApp() {
  currentAppState = STATE_APP_READER; currentReaderView = READER_VIEW_FILE_SELECT; readerFileSelected = 0; readerFileScroll = 0; scanReaderFiles();
  tft.fillScreen(COLOR_BG);
  tft.drawFastHLine(4, 4, 10, COLOR_CYAN); tft.drawFastVLine(4, 4, 10, COLOR_CYAN); tft.drawFastHLine(114, 4, 10, COLOR_CYAN); tft.drawFastVLine(123, 4, 10, COLOR_CYAN); tft.drawFastHLine(4, 155, 10, COLOR_CYAN); tft.drawFastVLine(4, 146, 10, COLOR_CYAN); tft.drawFastHLine(114, 155, 10, COLOR_CYAN); tft.drawFastVLine(123, 146, 10, COLOR_CYAN);
  tft.fillRect(8, 6, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(16, 8); tft.print("DOCUMENT DECK"); tft.drawFastHLine(8, 20, 112, COLOR_STEEL);
  renderReaderFileList();
}

void updateReaderApp(InputAction &input) {
  if (currentReaderView == READER_VIEW_FILE_SELECT) {
    if (input == ACTION_BACK) { initRollMenuScreen(); input = ACTION_NONE; return; }
    if (totalReaderFiles > 0) {
      if (input == ACTION_NAV) { if (readerFileSelected < totalReaderFiles - 1) { readerFileSelected++; if (readerFileSelected >= readerFileScroll + 4) readerFileScroll++; renderReaderFileList(); } }
      else if (input == ACTION_PREV) { if (readerFileSelected > 0) { readerFileSelected--; if (readerFileSelected < readerFileScroll) readerFileScroll--; renderReaderFileList(); } }
      else if (input == ACTION_SELECT) { currentReadingPath = readerFiles[readerFileSelected]; currentReaderView = READER_VIEW_FILE_ACTION; readerActionIndex = 0; renderReaderActionMenu(); }
    }
  } else if (currentReaderView == READER_VIEW_FILE_ACTION) {
    if (input == ACTION_BACK) { currentReaderView = READER_VIEW_FILE_SELECT; renderReaderFileList(); input = ACTION_NONE; return; }
    if (input == ACTION_NAV) { readerActionIndex = (readerActionIndex + 1) % 3; renderReaderActionMenu(); }
    else if (input == ACTION_PREV) { readerActionIndex = (readerActionIndex - 1 + 3) % 3; renderReaderActionMenu(); }
    else if (input == ACTION_SELECT) {
      if (readerActionIndex == 0) { openReaderDocument(currentReadingPath); }
      else if (readerActionIndex == 1) { launchKeyboardApp(KB_TARGET_FILE_RENAME); }
      else if (readerActionIndex == 2) {
        LittleFS.remove(currentReadingPath); scanReaderFiles(); currentReaderView = READER_VIEW_FILE_SELECT;
        if (readerFileSelected >= totalReaderFiles && totalReaderFiles > 0) readerFileSelected = totalReaderFiles - 1;
        readerFileScroll = 0; renderReaderFileList();
      }
    }
  } else if (currentReaderView == READER_VIEW_READING) {
    if (input == ACTION_BACK) { launchReaderApp(); input = ACTION_NONE; return; }
    if (input == ACTION_NAV) { if (readerCurrentPage < readerTotalPages - 1) { readerCurrentPage++; renderReaderPage(); } }
    else if (input == ACTION_PREV) { if (readerCurrentPage > 0) { readerCurrentPage--; renderReaderPage(); } }
  }
}

const char FRIDAY_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1.0">
<title>F.R.I.D.A.Y. // ESP32 INTERFACE</title>
<style>
:root{--c-glow:#00e5ff;--c-dim:#007588;--c-faint:rgba(0,229,255,0.08);--c-alert:#ff3344;--c-bg:#03080d;--c-panel:rgba(5,15,25,0.92);--font-mono:"Consolas","Courier New",monospace;}
*{box-sizing:border-box;margin:0;padding:0;}
body{background:var(--c-bg);color:var(--c-glow);font-family:var(--font-mono);font-size:12px;padding:12px;line-height:1.4;}
.container{max-width:960px;margin:0 auto;}
.hud-box{border:1px solid var(--c-dim);background:var(--c-panel);padding:12px;margin-bottom:12px;box-shadow:0 0 10px rgba(0,229,255,0.05);}
.hud-hdr{display:flex;justify-content:space-between;border-bottom:1px solid var(--c-dim);padding-bottom:4px;margin-bottom:8px;font-weight:bold;letter-spacing:1px;}
.btn{background:transparent;color:var(--c-glow);border:1px solid var(--c-glow);font-family:var(--font-mono);font-size:11px;padding:4px 10px;cursor:pointer;text-transform:uppercase;}
.btn:hover{background:var(--c-glow);color:#000;}
.btn-danger{border-color:var(--c-alert);color:var(--c-alert);}
.btn-danger:hover{background:var(--c-alert);color:#000;}
.hud-input{background:#010408;border:1px solid var(--c-dim);color:var(--c-glow);font-family:var(--font-mono);padding:4px 6px;font-size:11px;margin-bottom:6px;width:100%;}
table{width:100%;border-collapse:collapse;}
th,td{padding:6px;border-bottom:1px solid rgba(0,229,255,0.15);text-align:left;}
.directive-item{display:flex;justify-content:space-between;align-items:center;padding:5px 6px;border:1px solid rgba(0,229,255,0.15);margin-bottom:4px;}
.directive-item.completed{opacity:0.45;text-decoration:line-through;}
.drop-zone{border:2px dashed var(--c-dim);padding:16px;text-align:center;cursor:pointer;background:var(--c-faint);margin-bottom:8px;}
</style>
</head>
<body>
<div class="container">
<div class="hud-box">
<div class="hud-hdr"><span>F.R.I.D.A.Y. // NODE TERMINAL</span><span id="ipDisplay">192.168.4.1</span></div>
<div style="display:flex;justify-content:space-between;">
<span>CPU: <b id="cpu">240 MHz</b></span>
<span>FREE HEAP: <b id="heap">-- KB</b></span>
<span>UPTIME: <b id="uptime">--</b></span>
</div>
</div>
<div class="hud-box">
<div class="hud-hdr"><span>STORED AUTOCONNECT NETWORKS</span><span id="netStats">0 NETS</span></div>
<div id="netList"></div>
<div style="margin-top:8px;">
<input type="text" id="netSsid" class="hud-input" placeholder="SSID Name...">
<input type="password" id="netPass" class="hud-input" placeholder="Password (leave blank if open)...">
<button class="btn" onclick="addNetwork()">[ + Save Network ]</button>
</div>
</div>
<div class="hud-box">
<div class="hud-hdr"><span>TACTICAL DIRECTIVES (TODO)</span><span id="todoStats">0 / 0</span></div>
<div id="todoList"></div>
<div style="display:flex;gap:6px;margin-top:8px;">
<input type="text" id="todoInp" class="hud-input" style="flex:1;margin-bottom:0;" placeholder="New Directive...">
<button class="btn" onclick="addTodo()">[ + Commit ]</button>
</div>
</div>
<div class="hud-box">
<div class="hud-hdr"><span>FLASH TEXT-FILE UPLINK (HEAP GUARDED)</span><span id="fCount">0 FILES</span></div>
<div class="drop-zone" onclick="document.getElementById('fInp').click()">
<div>CLICK OR DROP TEXT PAYLOAD HERE (TXT, JSON, LOG, CSV, MD)</div>
<input type="file" id="fInp" style="display:none;" onchange="uploadSelected(event)">
</div>
<table>
<thead><tr><th>PATH</th><th>SIZE</th><th style="text-align:right;">ACTION</th></tr></thead>
<tbody id="fRows"><tr><td colspan="3" style="text-align:center;color:var(--c-dim);">NO ENTRIES</td></tr></tbody>
</table>
<div style="margin-top:10px;text-align:right;">
<button class="btn" onclick="refreshFiles()">[ Query Disk ]</button>
<button class="btn btn-danger" onclick="fetch('/reboot',{method:'POST'})">[ Reboot ESP ]</button>
</div>
</div>
</div>
<script>
function fetchStats(){fetch('/status').then(r=>r.json()).then(d=>{document.getElementById('heap').innerText=Math.round(d.free_heap/1024)+" KB";document.getElementById('uptime').innerText=d.uptime;document.getElementById('cpu').innerText=d.cpu+" MHz";});}
function refreshNets(){fetch('/api/networks').then(r=>r.json()).then(items=>{const box=document.getElementById('netList');box.innerHTML='';document.getElementById('netStats').innerText=items.length+" NETS";items.forEach(n=>{const d=document.createElement('div');d.className='directive-item';d.innerHTML='<div><span>SSID: <b>'+n.ssid+'</b></span></div><button class="btn btn-danger" style="padding:1px 4px;font-size:9px;" onclick="delNet(\''+n.ssid+'\')">DEL</button>';box.appendChild(d);});});}
function addNetwork(){const s=document.getElementById('netSsid').value.trim();const p=document.getElementById('netPass').value;if(!s)return;fetch('/api/networks?action=add&ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(p),{method:'POST'}).then(()=>{document.getElementById('netSsid').value='';document.getElementById('netPass').value='';refreshNets();});}
function delNet(ssid){fetch('/api/networks?action=delete&ssid='+encodeURIComponent(ssid),{method:'POST'}).then(refreshNets);}
function refreshTodos(){fetch('/api/todos').then(r=>r.json()).then(items=>{const box=document.getElementById('todoList');box.innerHTML='';let done=items.filter(x=>x.done).length;document.getElementById('todoStats').innerText=done+" / "+items.length+" DONE";items.forEach(t=>{const d=document.createElement('div');d.className='directive-item '+(t.done?'completed':'');d.innerHTML='<div><input type="checkbox" '+(t.done?'checked':'')+' onchange="toggleTodo('+t.id+')"> <span>'+t.text+'</span></div><button class="btn btn-danger" style="padding:1px 4px;font-size:9px;" onclick="delTodo('+t.id+')">X</button>';box.appendChild(d);});});}
function addTodo(){const v=document.getElementById('todoInp').value.trim();if(!v)return;fetch('/api/todos?action=add&text='+encodeURIComponent(v),{method:'POST'}).then(()=>{document.getElementById('todoInp').value='';refreshTodos();});}
function toggleTodo(id){fetch('/api/todos?action=toggle&id='+id,{method:'POST'}).then(refreshTodos);}
function delTodo(id){fetch('/api/todos?action=delete&id='+id,{method:'POST'}).then(refreshTodos);}
function refreshFiles(){fetch('/list').then(r=>r.json()).then(files=>{const b=document.getElementById('fRows');b.innerHTML='';document.getElementById('fCount').innerText=files.length+" OBJECTS";if(files.length===0){b.innerHTML='<tr><td colspan="3" style="text-align:center;">FLASH STORAGE EMPTY</td></tr>';return;}files.forEach(f=>{const r=document.createElement('tr');r.innerHTML='<td>'+f.name+'</td><td>'+f.size+' B</td><td style="text-align:right;"><a href="'+f.name+'" download class="btn" style="padding:2px 4px;font-size:9px;">GET</a> <button class="btn btn-danger" style="padding:2px 4px;font-size:9px;" onclick="delFile(\''+f.name+'\')">DEL</button></td>';b.appendChild(r);});});}
function delFile(p){if(!confirm("Purge "+p+"?"))return;fetch('/delete?file='+encodeURIComponent(p),{method:'DELETE'}).then(refreshFiles);}
function uploadSelected(e){const file=e.target.files[0];if(!file)return;const fd=new FormData();fd.append("data",file,"/"+file.name);fetch('/upload',{method:'POST',body:fd}).then(r=>{if(!r.ok)alert("Upload rejected: Heap safety limit reached!");refreshFiles();});}
setInterval(fetchStats,2500);fetchStats();refreshNets();refreshTodos();refreshFiles();
</script>
</body>
</html>
)rawliteral";

void setupFridayServer() {
  if (fridayServerRunning) return;
  WiFi.mode(WIFI_AP_STA); WiFi.softAP(AP_SSID, AP_PASS);
  server.on("/", HTTP_GET, []() { server.send_P(200, "text/html", FRIDAY_HTML); });
  server.on("/status", HTTP_GET, []() {
    time_t raw; struct tm ti; time(&raw); localtime_r(&raw, &ti); char upBuf[16]; unsigned long s = millis() / 1000;
    snprintf(upBuf, sizeof(upBuf), "%02lu:%02lu:%02lu", s/3600, (s%3600)/60, s%60);
    String json = "{"; json += "\"cpu\":" + String(ESP.getCpuFreqMHz()) + ","; json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ","; json += "\"total_heap\":" + String(ESP.getHeapSize()) + ","; json += "\"uptime\":\"" + String(upBuf) + "\""; json += "}"; server.send(200, "application/json", json);
  });
  server.on("/api/networks", HTTP_ANY, []() {
    if (server.hasArg("action")) {
      String act = server.arg("action");
      if (act == "add" && server.hasArg("ssid")) {
        saveSavedNetwork(server.arg("ssid").c_str(), server.arg("pass").c_str());
      } else if (act == "delete" && server.hasArg("ssid")) {
        String target = server.arg("ssid");
        int newCount = 0;
        SavedNetwork temp[MAX_SAVED_NETWORKS];
        for (int i = 0; i < totalSavedNetworks; i++) {
          if (strcmp(savedNetworks[i].ssid, target.c_str()) != 0) {
            temp[newCount++] = savedNetworks[i];
          }
        }
        totalSavedNetworks = newCount;
        prefs.begin("retro_nets", false);
        prefs.putInt("net_count", totalSavedNetworks);
        for (int i = 0; i < totalSavedNetworks; i++) {
          prefs.putString(("n_" + String(i) + "_s").c_str(), temp[i].ssid);
          prefs.putString(("n_" + String(i) + "_p").c_str(), temp[i].pass);
          savedNetworks[i] = temp[i];
        }
        prefs.end();
      }
    }
    String json = "[";
    for (int i = 0; i < totalSavedNetworks; i++) {
      if (i > 0) json += ",";
      json += "{\"ssid\":\"" + String(savedNetworks[i].ssid) + "\"}";
    }
    json += "]";
    server.send(200, "application/json", json);
  });
  server.on("/list", HTTP_GET, []() {
    File root = LittleFS.open("/"); String json = "["; bool first = true; File file = root.openNextFile();
    while (file) {
      String fn = String(file.name()); if (!fn.startsWith("/")) fn = "/" + fn;
      if (!fn.endsWith(".sys") && !fn.endsWith(".dat") && !fn.startsWith("/todo")) { if (!first) json += ","; json += "{\"name\":\"" + fn + "\",\"size\":" + String(file.size()) + "}"; first = false; }
      file = root.openNextFile();
    } json += "]"; server.send(200, "application/json", json);
  });
  server.on("/upload", HTTP_POST, []() { server.send(200, "text/plain", "OK"); }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) { if (ESP.getFreeHeap() < MIN_SAFE_HEAP_BYTES) return; String filename = upload.filename; if (!filename.startsWith("/")) filename = "/" + filename; uploadFile = LittleFS.open(filename, FILE_WRITE); }
    else if (upload.status == UPLOAD_FILE_WRITE) { if (uploadFile && ESP.getFreeHeap() >= MIN_SAFE_HEAP_BYTES) { uploadFile.write(upload.buf, upload.currentSize); } }
    else if (upload.status == UPLOAD_FILE_END) { if (uploadFile) uploadFile.close(); }
  });
  server.on("/delete", HTTP_DELETE, []() {
    if (server.hasArg("file")) { String p = server.arg("file"); if (!p.startsWith("/")) p = "/" + p; LittleFS.remove(p); server.send(200, "text/plain", "DELETED"); } else { server.send(400, "text/plain", "BAD ARG"); }
  });
  server.on("/api/todos", HTTP_ANY, []() {
    if (server.hasArg("action")) {
      String act = server.arg("action");
      if (act == "add" && server.hasArg("text")) {
        if (totalTodos < MAX_TODOS) {
          todoList[totalTodos].id = millis(); strncpy(todoList[totalTodos].text, server.arg("text").c_str(), 35); todoList[totalTodos].text[35] = '\0';
          strcpy(todoList[totalTodos].priority, "ROUTINE"); todoList[totalTodos].done = false; totalTodos++; saveTodos();
        }
      } else if (act == "toggle" && server.hasArg("id")) {
        uint32_t tid = server.arg("id").toInt();
        for (int i = 0; i < totalTodos; i++) { if (todoList[i].id == tid) { todoList[i].done = !todoList[i].done; saveTodos(); break; } }
      } else if (act == "delete" && server.hasArg("id")) {
        uint32_t tid = server.arg("id").toInt();
        for (int i = 0; i < totalTodos; i++) { if (todoList[i].id == tid) { for (int j = i; j < totalTodos - 1; j++) todoList[j] = todoList[j + 1]; totalTodos--; saveTodos(); break; } }
      }
    }
    String json = "["; for (int i = 0; i < totalTodos; i++) { if (i > 0) json += ","; json += "{\"id\":" + String(todoList[i].id) + ",\"text\":\"" + String(todoList[i].text) + "\",\"done\":" + String(todoList[i].done ? "true" : "false") + "}"; } json += "]"; server.send(200, "application/json", json);
  });
  server.on("/reboot", HTTP_POST, []() { server.send(200, "text/plain", "REBOOTING"); delay(200); ESP.restart(); });
  server.onNotFound([]() { String uri = server.uri(); if (LittleFS.exists(uri)) { File f = LittleFS.open(uri, "r"); server.streamFile(f, "text/plain"); f.close(); } else { server.send(404, "text/plain", "NOT FOUND"); } });
  server.begin(); fridayServerRunning = true;
}

void stopFridayServer() { if (fridayServerRunning) { server.stop(); WiFi.softAPdisconnect(true); fridayServerRunning = false; } }

void launchFridayApp() {
  currentAppState = STATE_APP_FRIDAY; setupFridayServer();
  tft.fillScreen(COLOR_BG); drawIndustrialHUD(); tft.drawFastHLine(10, 22, 108, COLOR_STEEL);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(14, 28); tft.print("F.R.I.D.A.Y UPLINK");
  tft.drawRect(10, 38, 108, 92, COLOR_CYAN); drawIconFriday(64, 58, true);
  tft.setTextColor(COLOR_LIME, COLOR_BG); tft.setCursor(14, 76); tft.print("NODE AP BROADCAST");
  tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(14, 88); tft.print("SSID: "); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.print(AP_SSID);
  tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(14, 100); tft.print("PASS: "); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.print(AP_PASS);
  tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(14, 114); tft.print("IP: "); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.print(WiFi.softAPIP());
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(46, 138); tft.print("escape");
}

void updateFridayApp(InputAction &input) {
  if (fridayServerRunning) server.handleClient();
  if (input == ACTION_BACK) { stopFridayServer(); initRollMenuScreen(); input = ACTION_NONE; }
}

void loadTodos() {
  prefs.begin("retro_todo", true); totalTodos = prefs.getInt("count", 0); if (totalTodos > MAX_TODOS) totalTodos = MAX_TODOS;
  for (int i = 0; i < totalTodos; i++) {
    String pfx = "t_" + String(i); todoList[i].id = prefs.getULong((pfx + "_id").c_str(), millis() + i);
    String txt = prefs.getString((pfx + "_tx").c_str(), "Directive"); strncpy(todoList[i].text, txt.c_str(), 35); todoList[i].text[35] = '\0';
    todoList[i].done = prefs.getBool((pfx + "_dn").c_str(), false);
  }
  prefs.end();
  if (totalTodos == 0) { totalTodos = 2; todoList[0] = {1, "Calibrate telemetry bus", "ROUTINE", false}; todoList[1] = {2, "Check flash sector table", "CRITICAL", true}; saveTodos(); }
}

void saveTodos() {
  prefs.begin("retro_todo", false); prefs.putInt("count", totalTodos);
  for (int i = 0; i < totalTodos; i++) { String pfx = "t_" + String(i); prefs.putULong((pfx + "_id").c_str(), todoList[i].id); prefs.putString((pfx + "_tx").c_str(), todoList[i].text); prefs.putBool((pfx + "_dn").c_str(), todoList[i].done); }
  prefs.end();
}

void renderTodoList() {
  tft.fillRect(8, 24, 112, 110, COLOR_BG);
  int listCount = totalTodos + (totalTodos < MAX_TODOS ? 1 : 0);
  if (listCount == 0) { tft.setTextSize(1); tft.setTextColor(COLOR_STEEL, COLOR_BG); tft.setCursor(22, 68); tft.print("NO DIRECTIVES"); } 
  else {
    for (int i = 0; i < 5; i++) {
      int idx = todoScrollIndex + i; if (idx >= listCount) break;
      int y = 28 + (i * 21); bool isSel = (idx == todoSelectedIdx);
      if (isSel) { tft.fillRect(10, y, 108, 19, COLOR_STEEL); } else { tft.drawRect(10, y, 108, 19, COLOR_STEEL); }
      if (idx == totalTodos) { tft.setTextSize(1); tft.setTextColor(COLOR_LIME, isSel ? COLOR_STEEL : COLOR_BG); tft.setCursor(14, y + 6); tft.print("[+] NEW DIRECTIVE"); } 
      else {
        tft.setTextSize(1); tft.setTextColor(todoList[idx].done ? COLOR_LIME : COLOR_CYAN, isSel ? COLOR_STEEL : COLOR_BG); tft.setCursor(14, y + 6); tft.print(todoList[idx].done ? "[X]" : "[ ]");
        tft.setTextColor(todoList[idx].done ? COLOR_ICE : COLOR_WHITE, isSel ? COLOR_STEEL : COLOR_BG); tft.setCursor(34, y + 6);
        String label = String(todoList[idx].text); if (label.length() > 11) label = label.substring(0, 10) + ".."; tft.print(label);
        if (todoList[idx].done) { tft.drawFastHLine(34, y + 9, label.length() * 6, HW_COLOR_RED); }
      }
    }
  }
  tft.fillRect(8, 136, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(16, 138); tft.print("opts");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("escape");
}

void renderTodoActionMenu() {
  tft.fillRect(8, 24, 112, 110, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(14, 28); tft.print("TASK:"); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.setCursor(14, 40);
  String shortTask = String(todoList[todoSelectedIdx].text); if (shortTask.length() > 14) shortTask = shortTask.substring(0, 13) + ".."; tft.print(shortTask);
  tft.drawFastHLine(12, 52, 104, COLOR_STEEL);
  for (int i = 0; i < 2; i++) {
    int y = 60 + (i * 24); bool isSel = (i == todoActionIndex);
    if (isSel) { tft.fillRect(12, y, 104, 20, COLOR_CYAN); tft.setTextColor(COLOR_BLACK, COLOR_CYAN); tft.setCursor(18, y + 6); tft.print("> "); tft.print(todoActions[i]); }
    else { tft.drawRect(12, y, 104, 20, COLOR_STEEL); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(24, y + 6); tft.print(todoActions[i]); }
  }
  tft.fillRect(8, 136, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(20, 138); tft.print("enter");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("back");
}

void launchTodoApp() {
  currentAppState = STATE_APP_TODO; currentTodoView = TODO_VIEW_LIST; todoSelectedIdx = 0; todoScrollIndex = 0; tft.fillScreen(COLOR_BG);
  tft.drawFastHLine(4, 4, 10, COLOR_CYAN); tft.drawFastVLine(4, 4, 10, COLOR_CYAN); tft.drawFastHLine(114, 4, 10, COLOR_CYAN); tft.drawFastVLine(123, 4, 10, COLOR_CYAN); tft.drawFastHLine(4, 155, 10, COLOR_CYAN); tft.drawFastVLine(4, 146, 10, COLOR_CYAN); tft.drawFastHLine(114, 155, 10, COLOR_CYAN); tft.drawFastVLine(123, 146, 10, COLOR_CYAN);
  tft.fillRect(8, 6, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(16, 8); tft.print("TASK DIRECTIVES"); tft.drawFastHLine(8, 20, 112, COLOR_STEEL);
  renderTodoList();
}

void updateTodoApp(InputAction &input) {
  if (currentTodoView == TODO_VIEW_LIST) {
    if (input == ACTION_BACK) { initRollMenuScreen(); input = ACTION_NONE; return; }
    int listCount = totalTodos + (totalTodos < MAX_TODOS ? 1 : 0);
    if (listCount > 0) {
      if (input == ACTION_NAV) { if (todoSelectedIdx < listCount - 1) { todoSelectedIdx++; if (todoSelectedIdx >= todoScrollIndex + 5) todoScrollIndex++; renderTodoList(); } }
      else if (input == ACTION_PREV) { if (todoSelectedIdx > 0) { todoSelectedIdx--; if (todoSelectedIdx < todoScrollIndex) todoScrollIndex--; renderTodoList(); } }
      else if (input == ACTION_SELECT) {
        if (todoSelectedIdx == totalTodos) { launchKeyboardApp(KB_TARGET_TODO_ADD); } else { currentTodoView = TODO_VIEW_ACTION; todoActionIndex = 0; renderTodoActionMenu(); }
      }
    }
  } else if (currentTodoView == TODO_VIEW_ACTION) {
    if (input == ACTION_BACK) { currentTodoView = TODO_VIEW_LIST; renderTodoList(); input = ACTION_NONE; return; }
    if (input == ACTION_NAV) { todoActionIndex = (todoActionIndex + 1) % 2; renderTodoActionMenu(); }
    else if (input == ACTION_PREV) { todoActionIndex = (todoActionIndex - 1 + 2) % 2; renderTodoActionMenu(); }
    else if (input == ACTION_SELECT) {
      if (todoActionIndex == 0) { todoList[todoSelectedIdx].done = !todoList[todoSelectedIdx].done; saveTodos(); }
      else if (todoActionIndex == 1) { for (int j = todoSelectedIdx; j < totalTodos - 1; j++) { todoList[j] = todoList[j + 1]; } totalTodos--; saveTodos(); }
      currentTodoView = TODO_VIEW_LIST; renderTodoList();
    }
  }
}

void drawClockHeader(bool force) {
  time_t rawTime; struct tm ti; time(&rawTime); localtime_r(&rawTime, &ti);
  if (force || ti.tm_min != lastHeaderMinute) {
    lastHeaderMinute = ti.tm_min; tft.fillRect(8, 6, 112, 12, COLOR_BG); char timeStr[8];
    if (rtcHasBeenSynced) { snprintf(timeStr, sizeof(timeStr), "%02d:%02d", ti.tm_hour, ti.tm_min); } else { snprintf(timeStr, sizeof(timeStr), "--:--"); }
    tft.setTextSize(1); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.setCursor(12, 8); tft.print("TIME:"); tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.print(timeStr);
    tft.fillCircle(112, 11, 2, rtcHasBeenSynced ? COLOR_LIME : HW_COLOR_RED); tft.drawFastHLine(8, 20, 112, COLOR_STEEL);
  }
}

void renderClockMenu() {
  tft.fillRect(8, 24, 112, 110, COLOR_BG);
  for (int i = 0; i < TOTAL_CLOCK_MENU_ITEMS; i++) {
    int y = 32 + (i * 30); bool isSel = (i == clockMenuIndex);
    if (isSel) { tft.fillRect(12, y, 104, 22, COLOR_CYAN); tft.setTextColor(COLOR_BLACK, COLOR_CYAN); tft.setCursor(20, y + 7); tft.print("> "); tft.print(clockMenuItems[i]); }
    else { tft.drawRect(12, y, 104, 22, COLOR_STEEL); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(24, y + 7); tft.print(clockMenuItems[i]); }
  }
  tft.fillRect(8, 136, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(20, 138); tft.print("enter");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("escape");
}

void initClockMenuView() {
  currentClockView = CLOCK_VIEW_MENU; lastHeaderMinute = -1; tft.fillScreen(COLOR_BG);
  tft.drawFastHLine(4, 4, 10, COLOR_CYAN); tft.drawFastVLine(4, 4, 10, COLOR_CYAN); tft.drawFastHLine(114, 4, 10, COLOR_CYAN); tft.drawFastVLine(123, 4, 10, COLOR_CYAN); tft.drawFastHLine(4, 155, 10, COLOR_CYAN); tft.drawFastVLine(4, 146, 10, COLOR_CYAN); tft.drawFastHLine(114, 155, 10, COLOR_CYAN); tft.drawFastVLine(123, 146, 10, COLOR_CYAN);
  drawClockHeader(true); renderClockMenu();
}

void launchClockApp() { currentAppState = STATE_APP_CLOCK; clockMenuIndex = 0; initClockMenuView(); }

void initStopwatchView() {
  currentClockView = CLOCK_VIEW_STOPWATCH; lastSwSec = -1; tft.fillRect(8, 22, 112, 132, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(16, 28); tft.print("CHRONO STOPWATCH"); tft.drawRect(12, 48, 104, 38, COLOR_CYAN);
  tft.setCursor(16, 105); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.print(swRunning ? "pause" : "start");
  tft.setCursor(16, 120); tft.setTextColor(COLOR_ACT_RIGHT, COLOR_BG); tft.print("reset");
  tft.setCursor(16, 138); tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.print("escape");
}

void updateStopwatch(InputAction input) {
  unsigned long now = millis();
  if (input == ACTION_SELECT) { if (!swRunning) { swRunning = true; swStartTime = now - swElapsedTime; } else { swRunning = false; swElapsedTime = now - swStartTime; } initStopwatchView(); }
  else if (input == ACTION_NAV) { swRunning = false; swElapsedTime = 0; initStopwatchView(); }
  unsigned long currentElapsed = swRunning ? (now - swStartTime) : swElapsedTime; unsigned int totalSec = currentElapsed / 1000;
  if ((int)totalSec != lastSwSec) {
    lastSwSec = totalSec; unsigned int sec = totalSec % 60; unsigned int min = totalSec / 60; char buf[8]; snprintf(buf, sizeof(buf), "%02u:%02u", min, sec);
    tft.setTextSize(2); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.setCursor(34, 58); tft.print(buf);
  }
}

void initTimerView() {
  currentClockView = CLOCK_VIEW_TIMER; lastTmSec = -1; lastBarWidth = -1; tft.fillRect(8, 22, 112, 132, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(20, 28); tft.print("COUNTDOWN TIMER");
  tft.drawRect(12, 44, 104, 42, COLOR_CYAN); tft.drawRect(15, 76, 98, 6, COLOR_STEEL);
  tft.setTextSize(1); tft.setCursor(20, 94); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.print(tmRunning ? "pause" : "start");
  tft.setCursor(20, 110); tft.setTextColor(COLOR_ACT_RIGHT, COLOR_BG); tft.print("+1m"); tft.setCursor(72, 110); tft.setTextColor(COLOR_ACT_LEFT, COLOR_BG); tft.print("-1m");
  tft.setCursor(20, 138); tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.print("escape");
}

void updateTimer(InputAction input) {
  unsigned long now = millis();
  if (input == ACTION_SELECT) { if (!tmRunning && tmRemaining > 0) { tmRunning = true; tmTargetEndTime = now + tmRemaining; } else if (tmRunning) { tmRunning = false; if (now < tmTargetEndTime) { tmRemaining = tmTargetEndTime - now; } else { tmRemaining = 0; } } initTimerView(); } 
  else if (input == ACTION_NAV && !tmRunning) { if (tmDuration < 3600000) { tmDuration += 60000; } tmRemaining = tmDuration; initTimerView(); } 
  else if (input == ACTION_PREV && !tmRunning) { if (tmDuration > 60000) { tmDuration -= 60000; } tmRemaining = tmDuration; initTimerView(); }
  if (tmRunning) { if (now >= tmTargetEndTime) { tmRemaining = 0; tmRunning = false; initTimerView(); } else { tmRemaining = tmTargetEndTime - now; } }
  int currentSecTotal = (tmRemaining + 999) / 1000;
  if (currentSecTotal != lastTmSec) {
    lastTmSec = currentSecTotal; unsigned int sec = currentSecTotal % 60; unsigned int min = currentSecTotal / 60; char buf[8]; snprintf(buf, sizeof(buf), "%02u:%02u", min, sec);
    tft.fillRect(20, 50, 88, 20, COLOR_BG); tft.setTextSize(2); tft.setTextColor((tmRemaining == 0) ? HW_COLOR_RED : COLOR_WHITE, COLOR_BG); tft.setCursor(34, 52); tft.print(buf);
  }
  int currentBarW = map(constrain(tmRemaining, 0, tmDuration), 0, tmDuration, 0, 96);
  if (currentBarW != lastBarWidth) { lastBarWidth = currentBarW; if (currentBarW > 0) { tft.fillRect(16, 77, currentBarW, 4, COLOR_CYAN); } if (currentBarW < 96) { tft.fillRect(16 + currentBarW, 77, 96 - currentBarW, 4, COLOR_STEEL); } }
}

void initSyncView() {
  currentClockView = CLOCK_VIEW_SYNC; tft.fillRect(8, 22, 112, 132, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(18, 28); tft.print("NETWORK TIME SYNC");
  tft.drawRect(12, 50, 104, 38, COLOR_STEEL); 
  
  if (WiFi.status() == WL_CONNECTED) {
    tft.setCursor(16, 56); tft.setTextColor(COLOR_LIME, COLOR_BG); tft.print("WIFI ALREADY LINKED");
    syncStep = SYNC_FETCH; syncTimer = millis(); 
  } else {
    tft.setCursor(16, 56); tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.print("LINKING WIFI...");
    syncStep = SYNC_CONNECT; syncTimer = millis(); WiFi.mode(WIFI_STA); WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  }
}

void updateSyncView() {
  unsigned long now = millis();
  switch (syncStep) {
    case SYNC_CONNECT:
      if (WiFi.status() == WL_CONNECTED) { syncStep = SYNC_FETCH; syncTimer = now; tft.fillRect(14, 52, 100, 34, COLOR_BG); tft.setCursor(16, 56); tft.setTextColor(COLOR_LIME, COLOR_BG); tft.print("WIFI LINKED"); tft.setCursor(16, 70); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.print("NTP SYNCING..."); configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2); }
      else if (now - syncTimer > 5000) { syncStep = SYNC_OFF; tft.fillRect(14, 52, 100, 34, COLOR_BG); tft.setCursor(16, 62); tft.setTextColor(HW_COLOR_RED, COLOR_BG); tft.print("LINK TIMEOUT"); } break;
    case SYNC_FETCH: {
      time_t t; struct tm ti; time(&t); localtime_r(&t, &ti);
      if (ti.tm_year > (2020 - 1900)) { rtcHasBeenSynced = true; tft.fillRect(14, 52, 100, 34, COLOR_BG); tft.setCursor(16, 62); tft.setTextColor(COLOR_LIME, COLOR_BG); tft.print("LOCK ACQUIRED"); syncStep = SYNC_OFF; }
      else if (now - syncTimer > 3000) { syncStep = SYNC_OFF; } break;
    }
    case SYNC_OFF:
      // Removed automatic WiFi radio shutdown here to preserve active state
      tft.setTextSize(1); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(16, 102); tft.print(WiFi.status() == WL_CONNECTED ? "RADIO: ACTIVE" : "RADIO: OFFLINE"); tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(16, 138); tft.print("escape");
      drawClockHeader(true); syncStep = SYNC_IDLE; break;
    case SYNC_IDLE: default: break;
  }
}

void updateClockApp(InputAction &input) {
  drawClockHeader(false);
  if (input == ACTION_BACK && currentClockView != CLOCK_VIEW_MENU) {
    if (currentClockView == CLOCK_VIEW_SYNC && syncStep != SYNC_IDLE) { syncStep = SYNC_IDLE; } // Removed explicit disconnect
    initClockMenuView(); input = ACTION_NONE; return;
  }
  switch (currentClockView) {
    case CLOCK_VIEW_MENU:
      if (input == ACTION_NAV) { clockMenuIndex = (clockMenuIndex + 1) % TOTAL_CLOCK_MENU_ITEMS; renderClockMenu(); }
      else if (input == ACTION_PREV) { clockMenuIndex = (clockMenuIndex - 1 + TOTAL_CLOCK_MENU_ITEMS) % TOTAL_CLOCK_MENU_ITEMS; renderClockMenu(); }
      else if (input == ACTION_SELECT) { if (clockMenuIndex == 0) initStopwatchView(); else if (clockMenuIndex == 1) initTimerView(); else if (clockMenuIndex == 2) initSyncView(); } break;
    case CLOCK_VIEW_STOPWATCH: updateStopwatch(input); break;
    case CLOCK_VIEW_TIMER: updateTimer(input); break;
    case CLOCK_VIEW_SYNC: updateSyncView(); break;
  }
}

// =============================================================================
// APP: GAMES - HIERARCHICAL NAVIGATION ENGINE
// =============================================================================
void renderGamesListMenu() {
  tft.fillRect(8, 24, 112, 110, COLOR_BG);
  int startIndex = 0; if (gameListIndex > 3) startIndex = gameListIndex - 3;
  for (int i = 0; i < 4; i++) {
    int gIdx = startIndex + i; if (gIdx >= TOTAL_GAMES) break;
    int y = 32 + (i * 24); bool isSel = (gIdx == gameListIndex);
    if (isSel) { tft.fillRect(12, y, 104, 20, COLOR_CYAN); tft.setTextColor(COLOR_BLACK, COLOR_CYAN); tft.setCursor(18, y + 6); tft.print("> "); tft.print(gameTitles[gIdx]); }
    else { tft.drawRect(12, y, 104, 20, COLOR_STEEL); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(24, y + 6); tft.print(gameTitles[gIdx]); }
  }
  tft.fillRect(8, 136, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(20, 138); tft.print("enter");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("escape");
}

void initGamesListMenu() {
  currentGamesView = GAMES_VIEW_SELECT_GAME; tft.fillScreen(COLOR_BG);
  tft.drawFastHLine(4, 4, 10, COLOR_CYAN); tft.drawFastVLine(4, 4, 10, COLOR_CYAN); tft.drawFastHLine(114, 4, 10, COLOR_CYAN); tft.drawFastVLine(123, 4, 10, COLOR_CYAN); tft.drawFastHLine(4, 155, 10, COLOR_CYAN); tft.drawFastVLine(4, 146, 10, COLOR_CYAN); tft.drawFastHLine(114, 155, 10, COLOR_CYAN); tft.drawFastVLine(123, 146, 10, COLOR_CYAN);
  tft.fillRect(8, 6, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(16, 8); tft.print("ARCADE DECK"); tft.drawFastHLine(8, 20, 112, COLOR_STEEL);
  renderGamesListMenu();
}

void renderGameActionMenu() {
  tft.fillRect(8, 24, 112, 110, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(14, 28); tft.print("MODULE:"); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.setCursor(14, 40);
  String title = gameTitles[activeGame]; if(title.length() > 14) title = title.substring(0,14); tft.print(title);
  tft.drawFastHLine(12, 52, 104, COLOR_STEEL);
  for (int i = 0; i < TOTAL_GAME_ACTIONS; i++) {
    int y = 60 + (i * 32); bool isSel = (i == gameActionIndex);
    if (isSel) { tft.fillRect(12, y, 104, 24, COLOR_CYAN); tft.setTextColor(COLOR_BLACK, COLOR_CYAN); tft.setCursor(18, y + 8); tft.print("> "); tft.print(gameActions[i]); }
    else { tft.drawRect(12, y, 104, 24, COLOR_STEEL); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(24, y + 8); tft.print(gameActions[i]); }
  }
  tft.fillRect(8, 136, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(20, 138); tft.print("enter");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("escape");
}

void initGameActionMenu() {
  currentGamesView = GAMES_VIEW_SELECT_ACTION; gameActionIndex = 0; tft.fillScreen(COLOR_BG);
  tft.drawFastHLine(4, 4, 10, COLOR_CYAN); tft.drawFastVLine(4, 4, 10, COLOR_CYAN); tft.drawFastHLine(114, 4, 10, COLOR_CYAN); tft.drawFastVLine(123, 4, 10, COLOR_CYAN); tft.drawFastHLine(4, 155, 10, COLOR_CYAN); tft.drawFastVLine(4, 146, 10, COLOR_CYAN); tft.drawFastHLine(114, 155, 10, COLOR_CYAN); tft.drawFastVLine(123, 146, 10, COLOR_CYAN);
  tft.fillRect(8, 6, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.setCursor(14, 8); tft.print("OPTIONS MENU"); tft.drawFastHLine(8, 20, 112, COLOR_STEEL);
  renderGameActionMenu();
}

void launchGamesApp() {
  currentAppState = STATE_APP_GAMES; gameListIndex = 0;
  prefs.begin("retro_games", true);
  siHighScore = prefs.getLong("si_hi", 0); snHighScore = prefs.getLong("sn_hi", 0); fbHighScore = prefs.getLong("fb_hi", 0);
  fwHighScore = prefs.getLong("fw_hi", 0); noHighScore = prefs.getLong("no_hi", 0); ldHighScore = prefs.getLong("ld_hi", 0);
  prefs.end();
  initGamesListMenu();
}

void drawShip(int x, int y, uint16_t color) { tft.drawPixel(x, y - 4, color); tft.drawFastHLine(x - 1, y - 3, 3, color); tft.drawFastHLine(x - 2, y - 2, 5, color); tft.drawFastHLine(x - 3, y - 1, 7, color); tft.drawFastHLine(x - 4, y, 9, color); tft.drawPixel(x - 4, y + 1, color); tft.drawPixel(x + 4, y + 1, color); }
void drawEnemy(int x, int y, int type, uint16_t color) { if (type == 0) { tft.drawFastHLine(x - 3, y - 2, 7, color); tft.drawFastHLine(x - 4, y - 1, 9, color); tft.drawFastHLine(x - 2, y, 5, color); tft.drawPixel(x - 3, y + 1, color); tft.drawPixel(x + 3, y + 1, color); } else { tft.drawFastHLine(x - 2, y - 2, 5, color); tft.drawFastHLine(x - 4, y - 1, 9, color); tft.drawFastHLine(x - 3, y, 7, color); tft.drawPixel(x, y + 1, color); } }
void renderSiScoreHUD() { tft.fillRect(36, 4, 34, 10, COLOR_BLACK); tft.setTextSize(1); tft.setTextColor(COLOR_WHITE, COLOR_BLACK); tft.setCursor(36, 6); char scoreStr[8]; snprintf(scoreStr, sizeof(scoreStr), "%04ld", siScore); tft.print(scoreStr); }
void initSpaceImpact() {
  currentGamesView = GAMES_VIEW_PLAYING; tft.fillScreen(COLOR_BLACK); tft.drawRect(6, 18, 116, 138, COLOR_STEEL); tft.drawFastHLine(6, 18, 116, COLOR_CYAN);
  playerX = 64; playerLastX = 64; siScore = 0; enemySubstep = 0;
  for (int i = 0; i < MAX_BULLETS; i++) bullets[i].active = false; for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = false;
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BLACK); tft.setCursor(8, 6); tft.print("SCR:"); renderSiScoreHUD(); tft.setTextColor(COLOR_CYAN, COLOR_BLACK); tft.setCursor(76, 6); tft.print("HI:"); tft.print(siHighScore);
  drawShip(playerX, playerY, COLOR_CYAN); lastSiTick = millis(); lastEnemySpawn = millis();
}
void updateSpaceImpact(InputAction input) {
  unsigned long now = millis();
  if (input == ACTION_PREV) { if (playerX > 16) { playerLastX = playerX; playerX -= 8; } } else if (input == ACTION_NAV) { if (playerX < 112) { playerLastX = playerX; playerX += 8; } } else if (input == ACTION_SELECT) { for (int i = 0; i < MAX_BULLETS; i++) { if (!bullets[i].active) { bullets[i].active = true; bullets[i].x = playerX; bullets[i].y = playerY - 6; break; } } }
  if (now - lastSiTick >= 33) {
    lastSiTick = now; enemySubstep++;
    if (playerX != playerLastX) { drawShip(playerLastX, playerY, COLOR_BLACK); drawShip(playerX, playerY, COLOR_CYAN); playerLastX = playerX; }
    for (int i = 0; i < MAX_BULLETS; i++) { if (bullets[i].active) { tft.drawFastVLine(bullets[i].x, bullets[i].y, 3, COLOR_BLACK); bullets[i].y -= 4; if (bullets[i].y < 20) { bullets[i].active = false; } else { tft.drawFastVLine(bullets[i].x, bullets[i].y, 3, COLOR_AMBER); } } }
    if (now - lastEnemySpawn >= 1500) { lastEnemySpawn = now; for (int i = 0; i < MAX_ENEMIES; i++) { if (!enemies[i].active) { enemies[i].active = true; enemies[i].x = random(18, 110); enemies[i].y = 22; enemies[i].type = random(0, 2); break; } } }
    bool moveDrone = (enemySubstep % 2 == 0); int interceptorDelta = 1 + ((enemySubstep % 4 == 0) ? 1 : 0);
    for (int i = 0; i < MAX_ENEMIES; i++) {
      if (enemies[i].active) {
        bool willMove = (enemies[i].type == 0) ? moveDrone : true; int deltaY = (enemies[i].type == 0) ? 1 : interceptorDelta;
        if (willMove) { drawEnemy(enemies[i].x, enemies[i].y, enemies[i].type, COLOR_BLACK); enemies[i].y += deltaY; }
        if (abs(enemies[i].x - playerX) < 8 && abs(enemies[i].y - playerY) < 6) { initGameOverView(); return; }
        if (enemies[i].y > 150) { drawEnemy(enemies[i].x, enemies[i].y, enemies[i].type, COLOR_BLACK); enemies[i].active = false; long penalty = (enemies[i].type == 0) ? 25 : 50; siScore -= penalty; renderSiScoreHUD(); if (siScore < 0) { initGameOverView(); return; }
        } else {
          bool destroyed = false;
          for (int b = 0; b < MAX_BULLETS; b++) { if (bullets[b].active && abs(bullets[b].x - enemies[i].x) < 6 && abs(bullets[b].y - enemies[i].y) < 6) { bullets[b].active = false; tft.drawFastVLine(bullets[b].x, bullets[b].y, 3, COLOR_BLACK); destroyed = true; break; } }
          if (destroyed) { drawEnemy(enemies[i].x, enemies[i].y, enemies[i].type, COLOR_BLACK); enemies[i].active = false; siScore += (enemies[i].type == 0) ? 50 : 100; renderSiScoreHUD(); } else if (willMove) { drawEnemy(enemies[i].x, enemies[i].y, enemies[i].type, (enemies[i].type == 0) ? HW_COLOR_RED : COLOR_ICE); }
        }
      }
    }
  }
}

void spawnSnakeFood() {
  bool collision; do { collision = false; foodPos.x = random(0, GRID_COLS); foodPos.y = random(0, GRID_ROWS);
    for (int i = 0; i < snakeLen; i++) { if (snakeBody[i].x == foodPos.x && snakeBody[i].y == foodPos.y) { collision = true; break; } }
  } while (collision);
  int px = GRID_OFFSET_X + (foodPos.x * GRID_CELL_SIZE); int py = GRID_OFFSET_Y + (foodPos.y * GRID_CELL_SIZE); tft.fillRect(px + 1, py + 1, GRID_CELL_SIZE - 2, GRID_CELL_SIZE - 2, COLOR_AMBER);
}
void renderSnScoreHUD() { tft.fillRect(36, 4, 34, 10, COLOR_BLACK); tft.setTextSize(1); tft.setTextColor(COLOR_WHITE, COLOR_BLACK); tft.setCursor(36, 6); char scoreStr[8]; snprintf(scoreStr, sizeof(scoreStr), "%04ld", snScore); tft.print(scoreStr); }
void initSnake() {
  currentGamesView = GAMES_VIEW_PLAYING; tft.fillScreen(COLOR_BLACK); tft.drawRect(GRID_OFFSET_X - 2, GRID_OFFSET_Y - 2, (GRID_COLS * GRID_CELL_SIZE) + 4, (GRID_ROWS * GRID_CELL_SIZE) + 4, COLOR_STEEL); tft.drawFastHLine(GRID_OFFSET_X - 2, GRID_OFFSET_Y - 2, (GRID_COLS * GRID_CELL_SIZE) + 4, COLOR_CYAN);
  snScore = 0; snakeLen = 4; snakeHeading = DIR_RIGHT; snakeSpeedMs = 130;
  for (int i = 0; i < snakeLen; i++) { snakeBody[i].x = 10 - i; snakeBody[i].y = 15; }
  for (int i = 0; i < snakeLen; i++) { int px = GRID_OFFSET_X + (snakeBody[i].x * GRID_CELL_SIZE); int py = GRID_OFFSET_Y + (snakeBody[i].y * GRID_CELL_SIZE); tft.fillRect(px, py, GRID_CELL_SIZE, GRID_CELL_SIZE, (i == 0) ? COLOR_LIME : COLOR_CYAN); }
  spawnSnakeFood(); tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BLACK); tft.setCursor(8, 6); tft.print("SCR:"); renderSnScoreHUD(); tft.setTextColor(COLOR_CYAN, COLOR_BLACK); tft.setCursor(76, 6); tft.print("HI:"); tft.print(snHighScore); lastSnakeTick = millis();
}
void updateSnake(InputAction input) {
  unsigned long now = millis();
  if (input == ACTION_PREV) { switch (snakeHeading) { case DIR_UP: snakeHeading = DIR_LEFT; break; case DIR_LEFT: snakeHeading = DIR_DOWN; break; case DIR_DOWN: snakeHeading = DIR_RIGHT; break; case DIR_RIGHT: snakeHeading = DIR_UP; break; } } else if (input == ACTION_NAV) { switch (snakeHeading) { case DIR_UP: snakeHeading = DIR_RIGHT; break; case DIR_RIGHT: snakeHeading = DIR_DOWN; break; case DIR_DOWN: snakeHeading = DIR_LEFT; break; case DIR_LEFT: snakeHeading = DIR_UP; break; } } else if (input == ACTION_SELECT) { lastSnakeTick = 0; }
  if (now - lastSnakeTick >= snakeSpeedMs) {
    lastSnakeTick = now; Point nextHead = snakeBody[0];
    switch (snakeHeading) { case DIR_UP: nextHead.y--; break; case DIR_RIGHT: nextHead.x++; break; case DIR_DOWN: nextHead.y++; break; case DIR_LEFT: nextHead.x--; break; }
    if (nextHead.x < 0 || nextHead.x >= GRID_COLS || nextHead.y < 0 || nextHead.y >= GRID_ROWS) { initGameOverView(); return; }
    for (int i = 0; i < snakeLen; i++) { if (snakeBody[i].x == nextHead.x && snakeBody[i].y == nextHead.y) { initGameOverView(); return; } }
    bool ateFood = (nextHead.x == foodPos.x && nextHead.y == foodPos.y);
    if (ateFood) { if (snakeLen < SNAKE_MAX_LEN - 1) snakeLen++; snScore += 25; renderSnScoreHUD(); if (snakeSpeedMs > 60) snakeSpeedMs -= 2; spawnSnakeFood(); } else { int tailX = GRID_OFFSET_X + (snakeBody[snakeLen - 1].x * GRID_CELL_SIZE); int tailY = GRID_OFFSET_Y + (snakeBody[snakeLen - 1].y * GRID_CELL_SIZE); tft.fillRect(tailX, tailY, GRID_CELL_SIZE, GRID_CELL_SIZE, COLOR_BLACK); }
    int oldHeadX = GRID_OFFSET_X + (snakeBody[0].x * GRID_CELL_SIZE); int oldHeadY = GRID_OFFSET_Y + (snakeBody[0].y * GRID_CELL_SIZE); tft.fillRect(oldHeadX, oldHeadY, GRID_CELL_SIZE, GRID_CELL_SIZE, COLOR_CYAN);
    for (int i = snakeLen - 1; i > 0; i--) { snakeBody[i] = snakeBody[i - 1]; } snakeBody[0] = nextHead;
    int headX = GRID_OFFSET_X + (nextHead.x * GRID_CELL_SIZE); int headY = GRID_OFFSET_Y + (nextHead.y * GRID_CELL_SIZE); tft.fillRect(headX, headY, GRID_CELL_SIZE, GRID_CELL_SIZE, COLOR_LIME);
  }
}

void drawBird(int y, uint16_t color) { tft.fillRect(BIRD_X_POS, y, 6, 4, color); tft.drawPixel(BIRD_X_POS + 6, y + 1, (color == COLOR_BLACK) ? COLOR_BLACK : COLOR_AMBER); tft.drawPixel(BIRD_X_POS + 1, y - 1, (color == COLOR_BLACK) ? COLOR_BLACK : COLOR_ICE); }
void drawPipes(int x, int gapY, uint16_t color) {
  int drawX = x; int drawW = BIRD_PIPE_W;
  if (drawX < BIRD_PLAY_X1) { drawW -= (BIRD_PLAY_X1 - drawX); drawX = BIRD_PLAY_X1; } if (drawX + drawW > BIRD_PLAY_X2) { drawW = BIRD_PLAY_X2 - drawX; } if (drawW <= 0) return;
  int topH = gapY - BIRD_PLAY_Y1; if (topH > 0) { tft.fillRect(drawX, BIRD_PLAY_Y1, drawW, topH, color); }
  int botY = gapY + BIRD_GAP_HEIGHT; int botH = BIRD_PLAY_Y2 - botY; if (botH > 0) { tft.fillRect(drawX, botY, drawW, botH, color); }
}
void renderFbScoreHUD() { tft.fillRect(36, 4, 34, 10, COLOR_BLACK); tft.setTextSize(1); tft.setTextColor(COLOR_WHITE, COLOR_BLACK); tft.setCursor(36, 6); char scoreStr[8]; snprintf(scoreStr, sizeof(scoreStr), "%04ld", fbScore); tft.print(scoreStr); }
void initCyberBird() {
  currentGamesView = GAMES_VIEW_PLAYING; tft.fillScreen(COLOR_BLACK);
  tft.drawRect(BIRD_PLAY_X1 - 1, BIRD_PLAY_Y1 - 1, (BIRD_PLAY_X2 - BIRD_PLAY_X1) + 2, (BIRD_PLAY_Y2 - BIRD_PLAY_Y1) + 2, COLOR_STEEL); tft.drawFastHLine(BIRD_PLAY_X1 - 1, BIRD_PLAY_Y1 - 1, (BIRD_PLAY_X2 - BIRD_PLAY_X1) + 2, COLOR_CYAN);
  birdY = 70.0; birdLastY = 70.0; birdVel = 0.0; fbScore = 0;
  pipes[0].x = 100; pipes[0].gapY = 50; pipes[0].passed = false; pipes[0].active = true;
  pipes[1].x = 100 + ((BIRD_PLAY_X2 - BIRD_PLAY_X1 + BIRD_PIPE_W) / 2) + 10; pipes[1].gapY = 70; pipes[1].passed = false; pipes[1].active = true;
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BLACK); tft.setCursor(8, 6); tft.print("SCR:"); renderFbScoreHUD();
  tft.setTextColor(COLOR_CYAN, COLOR_BLACK); tft.setCursor(76, 6); tft.print("HI:"); tft.print(fbHighScore);
  drawBird((int)birdY, COLOR_CYAN); drawPipes(pipes[0].x, pipes[0].gapY, COLOR_STEEL); drawPipes(pipes[1].x, pipes[1].gapY, COLOR_STEEL); lastFbTick = millis();
}
void updateCyberBird(InputAction input) {
  unsigned long now = millis();
  if (input == ACTION_SELECT || input == ACTION_NAV || input == ACTION_PREV) { birdVel = -3.2; }
  if (now - lastFbTick >= 33) {
    lastFbTick = now; birdVel += 0.32; if (birdVel > 4.5) birdVel = 4.5; birdY += birdVel;
    if ((int)birdY <= BIRD_PLAY_Y1 || (int)birdY >= (BIRD_PLAY_Y2 - 4)) { initGameOverView(); return; }
    if ((int)birdY != (int)birdLastY) { drawBird((int)birdLastY, COLOR_BLACK); drawBird((int)birdY, COLOR_CYAN); birdLastY = birdY; }
    for (int i = 0; i < 2; i++) {
      if (pipes[i].active) {
        drawPipes(pipes[i].x, pipes[i].gapY, COLOR_BLACK); pipes[i].x -= 2;
        if (!pipes[i].passed && pipes[i].x + BIRD_PIPE_W < BIRD_X_POS) { pipes[i].passed = true; fbScore += 1; renderFbScoreHUD(); }
        if (pipes[i].x + BIRD_PIPE_W < BIRD_PLAY_X1) { int other = (i == 0) ? 1 : 0; pipes[i].x = pipes[other].x + 64; pipes[i].gapY = random(BIRD_PLAY_Y1 + 10, BIRD_PLAY_Y2 - BIRD_GAP_HEIGHT - 10); pipes[i].passed = false; }
        drawPipes(pipes[i].x, pipes[i].gapY, COLOR_STEEL);
        if (BIRD_X_POS + 6 >= pipes[i].x && BIRD_X_POS <= pipes[i].x + BIRD_PIPE_W) { if ((int)birdY < pipes[i].gapY || ((int)birdY + 4) > pipes[i].gapY + BIRD_GAP_HEIGHT) { initGameOverView(); return; } }
      }
    }
    drawBird((int)birdY, COLOR_CYAN);
  }
}

void drawFwPad(int x, uint16_t color) { tft.fillRect(x - 12, 145, 24, 4, color); tft.drawFastHLine(x-6, 145, 12, (color==COLOR_BLACK)?COLOR_BLACK:COLOR_ICE); }
void drawFwBrick(int idx, uint16_t color) { tft.fillRect(fwBricks[idx].x, fwBricks[idx].y, fwBricks[idx].w - 1, fwBricks[idx].h - 1, color); }
void renderFwScoreHUD() { tft.fillRect(36, 4, 34, 10, COLOR_BLACK); tft.setTextSize(1); tft.setTextColor(COLOR_WHITE, COLOR_BLACK); tft.setCursor(36, 6); char scoreStr[8]; snprintf(scoreStr, sizeof(scoreStr), "%04ld", fwScore); tft.print(scoreStr); }
void spawnFwBricks() {
  int bw = 114 / FW_COLS; int bh = 10;
  for(int r=0; r<FW_ROWS; r++){
    for(int c=0; c<FW_COLS; c++){
      int i = r*FW_COLS + c; fwBricks[i].x = 7 + c*bw; fwBricks[i].y = 24 + r*bh; fwBricks[i].w = bw; fwBricks[i].h = bh; fwBricks[i].active = true;
      fwBricks[i].color = (r%2==0) ? COLOR_AMBER : COLOR_STEEL;
      drawFwBrick(i, fwBricks[i].color);
    }
  }
}
void initFirewall() {
  currentGamesView = GAMES_VIEW_PLAYING; tft.fillScreen(COLOR_BLACK); tft.drawRect(6, 18, 116, 138, COLOR_STEEL); tft.drawFastHLine(6, 18, 116, COLOR_CYAN);
  fwScore = 0; fwPadX = 64; fwBallX = 64.0; fwBallY = 100.0; fwBallVx = 1.8; fwBallVy = -2.2;
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BLACK); tft.setCursor(8, 6); tft.print("SCR:"); renderFwScoreHUD(); tft.setTextColor(COLOR_CYAN, COLOR_BLACK); tft.setCursor(76, 6); tft.print("HI:"); tft.print(fwHighScore);
  spawnFwBricks(); drawFwPad(fwPadX, COLOR_CYAN); tft.fillCircle((int)fwBallX, (int)fwBallY, 2, COLOR_ICE); lastFwTick = millis();
}
void updateFirewall(InputAction input) {
  unsigned long now = millis();
  if(input == ACTION_PREV) { drawFwPad(fwPadX, COLOR_BLACK); fwPadX -= 8; if(fwPadX < 18) fwPadX = 18; drawFwPad(fwPadX, COLOR_CYAN); }
  else if(input == ACTION_NAV) { drawFwPad(fwPadX, COLOR_BLACK); fwPadX += 8; if(fwPadX > 110) fwPadX = 110; drawFwPad(fwPadX, COLOR_CYAN); }
  
  if (now - lastFwTick >= 25) {
    lastFwTick = now; tft.fillCircle((int)fwBallX, (int)fwBallY, 2, COLOR_BLACK);
    fwBallX += fwBallVx; fwBallY += fwBallVy;
    if(fwBallX <= 8) { fwBallX = 8; fwBallVx = -fwBallVx; } else if(fwBallX >= 118) { fwBallX = 118; fwBallVx = -fwBallVx; }
    if(fwBallY <= 20) { fwBallY = 20; fwBallVy = -fwBallVy; }
    if(fwBallY >= 143 && fwBallY <= 147 && fwBallVy > 0) {
      if(fwBallX >= fwPadX - 14 && fwBallX <= fwPadX + 14) {
        fwBallY = 142; fwBallVy = -fwBallVy; fwBallVx = (fwBallX - fwPadX) * 0.2;
        if(fwBallVx > 3.0) fwBallVx = 3.0; if(fwBallVx < -3.0) fwBallVx = -3.0;
      }
    }
    if(fwBallY > 155) { initGameOverView(); return; }
    
    bool allDead = true;
    for(int i=0; i<FW_ROWS*FW_COLS; i++) {
      if(fwBricks[i].active) {
        allDead = false;
        if(fwBallX > fwBricks[i].x && fwBallX < fwBricks[i].x + fwBricks[i].w && fwBallY > fwBricks[i].y && fwBallY < fwBricks[i].y + fwBricks[i].h) {
          fwBricks[i].active = false; drawFwBrick(i, COLOR_BLACK); fwBallVy = -fwBallVy; fwScore += 10; renderFwScoreHUD(); break;
        }
      }
    }
    if(allDead) { fwBallY = 100.0; spawnFwBricks(); fwBallVx *= 1.1; fwBallVy *= 1.1; }
    tft.fillCircle((int)fwBallX, (int)fwBallY, 2, COLOR_ICE);
  }
}

void drawNoCar(int lane, float y, uint16_t color, bool isPlayer) {
  int x = 24 + (lane * 34); int iy = (int)y;
  if(isPlayer) {
    tft.drawTriangle(x, iy-8, x-6, iy+6, x+6, iy+6, color);
    if(color != COLOR_BLACK && noOverdrive) tft.drawLine(x, iy+8, x, iy+14, COLOR_ICE);
  } else {
    tft.fillRect(x-5, iy-8, 11, 16, color); tft.drawFastHLine(x-3, iy+8, 7, (color==COLOR_BLACK)?COLOR_BLACK:HW_COLOR_RED);
  }
}
void renderNoScoreHUD() { tft.fillRect(36, 4, 34, 10, COLOR_BLACK); tft.setTextSize(1); tft.setTextColor(COLOR_WHITE, COLOR_BLACK); tft.setCursor(36, 6); char scoreStr[8]; snprintf(scoreStr, sizeof(scoreStr), "%04ld", noScore); tft.print(scoreStr); }
void initOverdrive() {
  currentGamesView = GAMES_VIEW_PLAYING; tft.fillScreen(COLOR_BLACK); tft.drawRect(6, 18, 116, 138, COLOR_STEEL);
  noScore = 0; noPlayerLane = 1; noBaseSpeed = 2.5; noOverdrive = false;
  for(int i=0; i<NO_MAX_CARS; i++) noCars[i].active = false;
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BLACK); tft.setCursor(8, 6); tft.print("SCR:"); renderNoScoreHUD(); tft.setTextColor(COLOR_CYAN, COLOR_BLACK); tft.setCursor(76, 6); tft.print("HI:"); tft.print(noHighScore);
  drawNoCar(noPlayerLane, 136.0, COLOR_CYAN, true); lastNoTick = millis(); lastNoSpawn = millis();
}
void updateOverdrive(InputAction input) {
  unsigned long now = millis(); noOverdrive = (input == ACTION_SELECT);
  if (input == ACTION_PREV && noPlayerLane > 0) { drawNoCar(noPlayerLane, 136.0, COLOR_BLACK, true); noPlayerLane--; drawNoCar(noPlayerLane, 136.0, COLOR_CYAN, true); }
  else if (input == ACTION_NAV && noPlayerLane < 2) { drawNoCar(noPlayerLane, 136.0, COLOR_BLACK, true); noPlayerLane++; drawNoCar(noPlayerLane, 136.0, COLOR_CYAN, true); }
  if (now - lastNoTick >= 33) {
    lastNoTick = now; float currentSpeed = noOverdrive ? (noBaseSpeed * 2.0) : noBaseSpeed;
    int dashOffset = (now / 20) % 20;
    for(int y=18; y<156; y+=20) {
      int dy = y + dashOffset; if(dy < 154 && dy > 18) { tft.drawFastVLine(41, dy, 10, COLOR_STEEL); tft.drawFastVLine(75, dy, 10, COLOR_STEEL); tft.drawFastVLine(41, dy-10, 10, COLOR_BLACK); tft.drawFastVLine(75, dy-10, 10, COLOR_BLACK); }
    }
    if(now - lastNoSpawn > (1200 / (noBaseSpeed/2.0))) {
      lastNoSpawn = now;
      for(int i=0; i<NO_MAX_CARS; i++) {
        if(!noCars[i].active) { noCars[i].active = true; noCars[i].y = 0.0; noCars[i].lane = random(0,3); noBaseSpeed += 0.05; if(noBaseSpeed > 5.5) noBaseSpeed = 5.5; break; }
      }
    }
    drawNoCar(noPlayerLane, 136.0, COLOR_CYAN, true);
    for(int i=0; i<NO_MAX_CARS; i++) {
      if(noCars[i].active) {
        drawNoCar(noCars[i].lane, noCars[i].y, COLOR_BLACK, false);
        noCars[i].y += (currentSpeed * 0.8);
        if(noCars[i].y > 160) { noCars[i].active = false; noScore += (noOverdrive?20:10); renderNoScoreHUD(); }
        else { drawNoCar(noCars[i].lane, noCars[i].y, COLOR_AMBER, false); if(noCars[i].lane == noPlayerLane && abs((int)noCars[i].y - 136) < 14) { initGameOverView(); return; } }
      }
    }
  }
}

void generateLdTerrain() {
  ldPadIdx = random(1, LD_PTS - 4); int padY = random(110, 140);
  for(int i=0; i<LD_PTS; i++) {
    ldTerrain[i].x = 6 + (i * 10);
    if(i == ldPadIdx || i == ldPadIdx+1 || i == ldPadIdx+2 || i == ldPadIdx+3) { ldTerrain[i].y = padY; } else { ldTerrain[i].y = random(80, 150); }
  }
  for(int i=0; i<LD_PTS-1; i++) { tft.drawLine(ldTerrain[i].x, ldTerrain[i].y, ldTerrain[i+1].x, ldTerrain[i+1].y, (i >= ldPadIdx && i <= ldPadIdx+2) ? COLOR_LIME : COLOR_STEEL); }
}
void drawLdLander(float x, float y, float a, uint16_t c, bool thrust) {
  float s = sin(a); float cs = cos(a);
  int v1x = x + 6*s; int v1y = y - 6*cs; int v2x = x + (-4*cs - 4*s); int v2y = y + (-4*s + 4*cs); int v3x = x + (4*cs - 4*s); int v3y = y + (4*s + 4*cs);
  tft.drawTriangle(v1x, v1y, v2x, v2y, v3x, v3y, c);
  if (thrust) { int flameX = x - 8*s; int flameY = y + 8*cs; tft.drawLine(x, y, flameX, flameY, (c == COLOR_BLACK) ? COLOR_BLACK : COLOR_AMBER); }
}
void renderLdHUD() {
  tft.fillRect(36, 4, 34, 10, COLOR_BLACK); tft.setTextSize(1); tft.setTextColor(COLOR_WHITE, COLOR_BLACK); tft.setCursor(36, 6); tft.print(ldScore);
  tft.fillRect(94, 6, 26, 8, COLOR_BLACK); tft.setCursor(94, 6); tft.setTextColor((ldFuel<200)?HW_COLOR_RED:COLOR_ICE, COLOR_BLACK); tft.print((int)ldFuel);
}
void initLunar() {
  currentGamesView = GAMES_VIEW_PLAYING; tft.fillScreen(COLOR_BLACK); tft.drawRect(6, 18, 116, 138, COLOR_STEEL); tft.drawFastHLine(6, 18, 116, COLOR_CYAN);
  generateLdTerrain(); ldX = 64.0; ldY = 30.0; ldVx = random(-10,10)/15.0; ldVy = 0.0; ldAngle = 0.0; ldFuel = 1000.0; ldLastThrust = false;
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BLACK); tft.setCursor(8, 6); tft.print("SCR:"); tft.setTextColor(COLOR_CYAN, COLOR_BLACK); tft.setCursor(80, 6); tft.print("F:");
  renderLdHUD(); drawLdLander(ldX, ldY, ldAngle, COLOR_CYAN, false); lastLdTick = millis();
}
void updateLunar(InputAction input) {
  unsigned long now = millis();
  if (now - lastLdTick >= 33) {
    lastLdTick = now; drawLdLander(ldX, ldY, ldAngle, COLOR_BLACK, ldLastThrust);
    for(int i=0; i<LD_PTS-1; i++) { if(ldX >= ldTerrain[i].x - 10 && ldX <= ldTerrain[i+1].x + 10) { tft.drawLine(ldTerrain[i].x, ldTerrain[i].y, ldTerrain[i+1].x, ldTerrain[i+1].y, (i >= ldPadIdx && i <= ldPadIdx+2) ? COLOR_LIME : COLOR_STEEL); } }
    
    bool thrusting = false;
    
#if ENABLE_PHYSICAL_BUTTONS
    for (uint8_t i = 0; i < 4; i++) {
      if (buttonActionMap[i] == LOG_ACTION_SEL && digitalRead(buttons[i].pin) == LOW) thrusting = true;
      if (buttonActionMap[i] == LOG_ACTION_LEFT && digitalRead(buttons[i].pin) == LOW) ldAngle -= 0.08;
      if (buttonActionMap[i] == LOG_ACTION_RIGHT && digitalRead(buttons[i].pin) == LOW) ldAngle += 0.08;
    }
#else
    if (input == ACTION_PREV) ldAngle -= 0.15; else if (input == ACTION_NAV) ldAngle += 0.15;
    if (input == ACTION_SELECT) thrusting = true;
#endif

    if (thrusting && ldFuel > 0) { ldVx += sin(ldAngle) * 0.08; ldVy -= cos(ldAngle) * 0.08; ldFuel -= 0.8; ldLastThrust = true; } else { ldLastThrust = false; }
    ldVy += 0.02; ldX += ldVx; ldY += ldVy;
    if(ldX < 6) { ldX = 6; ldVx = -ldVx*0.5; } else if(ldX > 116) { ldX = 116; ldVx = -ldVx*0.5; } 
    if(ldY < 20) { ldY = 20; ldVy = 0; }
    
    for(int i=0; i<LD_PTS-1; i++) {
      if(ldX >= ldTerrain[i].x && ldX <= ldTerrain[i+1].x) {
        float tY = ldTerrain[i].y + ((ldTerrain[i+1].y - ldTerrain[i].y) * (ldX - ldTerrain[i].x) / 10.0);
        if(ldY >= tY - 5) {
          if((i >= ldPadIdx && i <= ldPadIdx+2) && abs(ldAngle) < 0.6) { 
            ldScore += 100 + (int)(ldFuel / 10); 
            tft.fillRect(6, 18, 116, 138, COLOR_BLACK); generateLdTerrain();
            ldX = 64.0; ldY = 30.0; ldVx = random(-10,10)/15.0; ldVy = 0.0; ldAngle = 0.0; 
          } else { initGameOverView(); return; }
        }
      }
    }
    drawLdLander(ldX, ldY, ldAngle, COLOR_CYAN, ldLastThrust); renderLdHUD();
  }
}

void initGameOverView() {
  currentGamesView = GAMES_VIEW_GAMEOVER; long currentScore = 0; long *hiScorePtr = nullptr; const char* keyName = ""; const char* titleStr = "";
  if (activeGame == GAME_SPACE_IMPACT) { currentScore = siScore; hiScorePtr = &siHighScore; keyName = "si_hi"; titleStr = "SYSTEM CRASH"; }
  else if (activeGame == GAME_CYBER_SNAKE) { currentScore = snScore; hiScorePtr = &snHighScore; keyName = "sn_hi"; titleStr = "SNAKE CRASH"; }
  else if (activeGame == GAME_CYBER_BIRD) { currentScore = fbScore; hiScorePtr = &fbHighScore; keyName = "fb_hi"; titleStr = "HULL BREACH"; }
  else if (activeGame == GAME_FIREWALL) { currentScore = fwScore; hiScorePtr = &fwHighScore; keyName = "fw_hi"; titleStr = "NODE REJECTED"; }
  else if (activeGame == GAME_OVERDRIVE) { currentScore = noScore; hiScorePtr = &noHighScore; keyName = "no_hi"; titleStr = "TOTALED"; }
  else if (activeGame == GAME_LUNAR) { currentScore = ldScore; hiScorePtr = &ldHighScore; keyName = "ld_hi"; titleStr = "LANDER LOST"; }

  bool isNewBest = false;
  if (currentScore > *hiScorePtr) { *hiScorePtr = currentScore; isNewBest = true; prefs.begin("retro_games", false); prefs.putLong(keyName, *hiScorePtr); prefs.end(); }
  tft.fillScreen(COLOR_BG); drawIndustrialHUD(); tft.drawRect(10, 30, 108, 94, COLOR_CYAN);
  tft.setTextSize(1); tft.setTextColor(HW_COLOR_RED, COLOR_BG); tft.setCursor(24, 40); tft.print(titleStr);
  tft.drawFastHLine(14, 54, 100, COLOR_STEEL); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(20, 62); tft.print("SCORE: "); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.print(currentScore);
  tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(20, 78); tft.print("BEST : "); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.print(*hiScorePtr);
  if (isNewBest) { tft.setTextColor(COLOR_LIME, COLOR_BG); tft.setCursor(24, 98); tft.print("[ NEW RECORD ]"); }
  tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(24, 134); tft.print("retry"); tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(68, 134); tft.print("escape");
}

void initHighScoresView() {
  currentGamesView = GAMES_VIEW_HIGHSCORES; tft.fillRect(8, 22, 112, 132, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(18, 28); const char* titleStr = ""; const char* deviceStr = ""; long hiVal = 0;
  if (activeGame == GAME_SPACE_IMPACT) { titleStr = "IMPACT TELEMETRY"; deviceStr = "DEVICE: SHOCKWAVE"; hiVal = siHighScore; }
  else if (activeGame == GAME_CYBER_SNAKE) { titleStr = "SNAKE TELEMETRY"; deviceStr = "ENGINE: QUANTUM"; hiVal = snHighScore; }
  else if (activeGame == GAME_CYBER_BIRD) { titleStr = "AVIAN TELEMETRY"; deviceStr = "THRUST: VECTOR-1"; hiVal = fbHighScore; }
  else if (activeGame == GAME_FIREWALL) { titleStr = "FIREWALL STATS"; deviceStr = "PAYLOAD: DEFLECT"; hiVal = fwHighScore; }
  else if (activeGame == GAME_OVERDRIVE) { titleStr = "OVERDRIVE STATS"; deviceStr = "ENGINE: PLASMA"; hiVal = noHighScore; }
  else if (activeGame == GAME_LUNAR) { titleStr = "LUNAR TELEMETRY"; deviceStr = "THRUST: MICRO-V"; hiVal = ldHighScore; }

  tft.print(titleStr); tft.drawRect(12, 46, 104, 60, COLOR_STEEL); tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.setCursor(20, 56); tft.print("PILOT : SLOKE"); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(20, 72); tft.print(deviceStr); tft.setTextColor(COLOR_LIME, COLOR_BG); tft.setCursor(20, 88); tft.print("RECORD: "); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.print(hiVal);
  tft.setCursor(20, 138); tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.print("escape");
}

void updateGamesApp(InputAction &input) {
  if (input == ACTION_BACK) {
    if (currentGamesView == GAMES_VIEW_SELECT_ACTION) { initGamesListMenu(); input = ACTION_NONE; return; } 
    else if (currentGamesView == GAMES_VIEW_PLAYING || currentGamesView == GAMES_VIEW_GAMEOVER || currentGamesView == GAMES_VIEW_HIGHSCORES) { initGameActionMenu(); input = ACTION_NONE; return; }
  }

  switch (currentGamesView) {
    case GAMES_VIEW_SELECT_GAME:
      if (input == ACTION_NAV) { gameListIndex = (gameListIndex + 1) % TOTAL_GAMES; renderGamesListMenu(); }
      else if (input == ACTION_PREV) { gameListIndex = (gameListIndex - 1 + TOTAL_GAMES) % TOTAL_GAMES; renderGamesListMenu(); }
      else if (input == ACTION_SELECT) { activeGame = (SelectedGame)gameListIndex; initGameActionMenu(); } break;
    case GAMES_VIEW_SELECT_ACTION:
      if (input == ACTION_NAV) { gameActionIndex = (gameActionIndex + 1) % TOTAL_GAME_ACTIONS; renderGameActionMenu(); }
      else if (input == ACTION_PREV) { gameActionIndex = (gameActionIndex - 1 + TOTAL_GAME_ACTIONS) % TOTAL_GAME_ACTIONS; renderGameActionMenu(); }
      else if (input == ACTION_SELECT) {
        if (gameActionIndex == 0) {
          if (activeGame == GAME_SPACE_IMPACT) initSpaceImpact(); else if (activeGame == GAME_CYBER_SNAKE) initSnake(); else if (activeGame == GAME_CYBER_BIRD) initCyberBird();
          else if (activeGame == GAME_FIREWALL) initFirewall(); else if (activeGame == GAME_OVERDRIVE) initOverdrive(); else if (activeGame == GAME_LUNAR) initLunar();
        } else if (gameActionIndex == 1) { initHighScoresView(); }
      } break;
    case GAMES_VIEW_PLAYING:
      if (activeGame == GAME_SPACE_IMPACT) updateSpaceImpact(input); else if (activeGame == GAME_CYBER_SNAKE) updateSnake(input); else if (activeGame == GAME_CYBER_BIRD) updateCyberBird(input);
      else if (activeGame == GAME_FIREWALL) updateFirewall(input); else if (activeGame == GAME_OVERDRIVE) updateOverdrive(input); else if (activeGame == GAME_LUNAR) updateLunar(input); break;
    case GAMES_VIEW_GAMEOVER:
      if (input == ACTION_SELECT) {
        if (activeGame == GAME_SPACE_IMPACT) initSpaceImpact(); else if (activeGame == GAME_CYBER_SNAKE) initSnake(); else if (activeGame == GAME_CYBER_BIRD) initCyberBird();
        else if (activeGame == GAME_FIREWALL) initFirewall(); else if (activeGame == GAME_OVERDRIVE) initOverdrive(); else if (activeGame == GAME_LUNAR) initLunar();
      } break;
    case GAMES_VIEW_HIGHSCORES: break;
  }
}

// =============================================================================
// APP: SETTINGS & ENHANCED WIFI SCANNER
// =============================================================================
void renderSettingsMenu() {
  tft.fillRect(8, 24, 112, 110, COLOR_BG);
  for (int i = 0; i < TOTAL_SETTINGS_MENU_ITEMS; i++) {
    int y = 26 + (i * 18); bool isSel = (i == settingsMenuIndex);
    if (isSel) { tft.fillRect(10, y, 108, 16, COLOR_CYAN); tft.setTextColor(COLOR_BLACK, COLOR_CYAN); tft.setCursor(14, y + 4); tft.print("> "); tft.print(settingsMenuItems[i]); } 
    else { tft.drawRect(10, y, 108, 16, COLOR_STEEL); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(18, y + 4); tft.print(settingsMenuItems[i]); }
  }
  tft.fillRect(8, 136, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(20, 138); tft.print("enter");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("escape");
}

void initSettingsMenuView() {
  currentSettingsView = SETTINGS_VIEW_MENU; tft.fillScreen(COLOR_BG);
  tft.drawFastHLine(4, 4, 10, COLOR_CYAN); tft.drawFastVLine(4, 4, 10, COLOR_CYAN); tft.drawFastHLine(114, 4, 10, COLOR_CYAN); tft.drawFastVLine(123, 4, 10, COLOR_CYAN); tft.drawFastHLine(4, 155, 10, COLOR_CYAN); tft.drawFastVLine(4, 146, 10, COLOR_CYAN); tft.drawFastHLine(114, 155, 10, COLOR_CYAN); tft.drawFastVLine(123, 146, 10, COLOR_CYAN);
  tft.fillRect(8, 6, 112, 12, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(16, 8); tft.print("SYSTEM SETTINGS"); tft.drawFastHLine(8, 20, 112, COLOR_STEEL);
  renderSettingsMenu();
}

void launchSettingsApp() { currentAppState = STATE_APP_SETTINGS; settingsMenuIndex = 0; initSettingsMenuView(); }

void initTelemetryView() {
  currentSettingsView = SETTINGS_VIEW_TELEMETRY; tft.fillRect(8, 22, 112, 132, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(16, 26); tft.print("CORE TELEMETRY");
  tft.drawRect(10, 38, 108, 92, COLOR_STEEL);
  tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.setCursor(14, 44); tft.print("CPU: "); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.print(ESP.getCpuFreqMHz()); tft.print(" MHz");
  tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.setCursor(14, 58); tft.print("HEAP: "); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.print(ESP.getFreeHeap() / 1024); tft.print(" KB");
  tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.setCursor(14, 72); tft.print("FLASH: "); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.print(ESP.getFlashChipSize() / (1024 * 1024)); tft.print(" MB");
  tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.setCursor(14, 86); tft.print("UPTIME:"); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.setCursor(14, 98);
  unsigned long secs = millis() / 1000; unsigned int h = secs / 3600; unsigned int m = (secs % 3600) / 60; unsigned int s = secs % 60; char upBuf[12]; snprintf(upBuf, sizeof(upBuf), "%02u:%02u:%02u", h, m, s); tft.print(upBuf);
  tft.setTextColor(COLOR_LIME, COLOR_BG); tft.setCursor(14, 114); tft.print("STATUS: OPTIMAL");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(16, 138); tft.print("escape");
}

void renderThemeView() {
  tft.fillRect(8, 22, 112, 132, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(18, 26); tft.print("THEME PRESETS");
  for (int i = 0; i < TOTAL_THEMES; i++) {
    int y = 42 + (i * 28); bool isSel = (i == currentThemeIdx);
    if (isSel) { tft.fillRect(10, y, 108, 22, COLOR_CYAN); tft.setTextColor(COLOR_BLACK, COLOR_CYAN); tft.setCursor(16, y + 7); tft.print("> "); tft.print(THEMES[i].name); } 
    else { tft.drawRect(10, y, 108, 22, COLOR_STEEL); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(20, y + 7); tft.print(THEMES[i].name); }
  }
  tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(16, 138); tft.print("toggle"); tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("escape");
}
void initThemeView() { currentSettingsView = SETTINGS_VIEW_THEME; renderThemeView(); }

void renderControlMapView() {
  tft.fillRect(8, 22, 112, 132, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(16, 26); tft.print("CONTROL MAPPER");
  const char* btnColors[] = {"RED:  ", "BLUE: ", "WHITE:", "AMBR: "};
  for (int i = 0; i < 4; i++) {
    int y = 40 + (i * 22); bool isSel = (i == controlMapCursor);
    if (isSel) { tft.fillRect(10, y, 108, 18, COLOR_CYAN); tft.setTextColor(COLOR_BLACK, COLOR_CYAN); } else { tft.drawRect(10, y, 108, 18, COLOR_STEEL); tft.setTextColor(HW_BTN_COLORS[i], COLOR_BG); }
    tft.setCursor(14, y + 5); tft.print(btnColors[i]); tft.print(actionNames[buttonActionMap[i]]);
  }
  tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(16, 138); tft.print("swap"); tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("escape");
}
void initControlMapView() { currentSettingsView = SETTINGS_VIEW_CONTROL_MAP; controlMapCursor = 0; renderControlMapView(); }

void initStorageView() {
  currentSettingsView = SETTINGS_VIEW_STORAGE; tft.fillRect(8, 22, 112, 132, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(18, 26); tft.print("NVS DATABASE");
  tft.drawRect(10, 38, 108, 92, COLOR_STEEL);
  tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(14, 44); tft.print("GAMES RECORDS:");
  tft.setTextColor(COLOR_WHITE, COLOR_BG);
  tft.setCursor(14, 56); tft.print("IMPACT : "); tft.print(siHighScore);
  tft.setCursor(14, 68); tft.print("SNAKE  : "); tft.print(snHighScore);
  tft.setCursor(14, 80); tft.print("BIRD   : "); tft.print(fbHighScore);
  tft.setCursor(14, 92); tft.print("BRKOUT : "); tft.print(fwHighScore);
  tft.setCursor(14, 104); tft.print("RACING : "); tft.print(noHighScore);
  tft.setCursor(14, 116); tft.print("LUNAR  : "); tft.print(ldHighScore);
  tft.setCursor(14, 138); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.print("clear"); tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("escape");
}

void renderNetMenuView() {
  tft.fillRect(8, 22, 112, 132, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(16, 26); tft.print("NETWORK CONFIG");
  for (int i = 0; i < TOTAL_NET_MENU_ITEMS; i++) {
    int y = 44 + (i * 28); bool isSel = (i == netMenuIndex);
    if (isSel) { tft.fillRect(10, y, 108, 22, COLOR_CYAN); tft.setTextColor(COLOR_BLACK, COLOR_CYAN); tft.setCursor(16, y + 7); tft.print("> "); tft.print(netMenuItems[i]); } 
    else { tft.drawRect(10, y, 108, 22, COLOR_STEEL); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(20, y + 7); tft.print(netMenuItems[i]); }
  }
  tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(20, 138); tft.print("enter"); tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("escape");
}
void initNetMenuView() { currentSettingsView = SETTINGS_VIEW_NET_MENU; renderNetMenuView(); }

void initNetworkView() {
  currentSettingsView = SETTINGS_VIEW_NET_STATUS; tft.fillRect(8, 22, 112, 132, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(16, 26); tft.print("NET TELEMETRY");
  tft.drawRect(10, 38, 108, 92, COLOR_STEEL);
  tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.setCursor(14, 44); tft.print("SSID:"); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.setCursor(14, 54); tft.print((WiFi.status() == WL_CONNECTED) ? WiFi.SSID().c_str() : "DISCONNECTED");
  tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.setCursor(14, 68); tft.print("MAC ADDR:"); tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.setCursor(14, 78); String mac = WiFi.macAddress(); tft.print(mac.substring(9));
  tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.setCursor(14, 92); tft.print("NTP SYNC:"); tft.setTextColor(rtcHasBeenSynced ? COLOR_LIME : HW_COLOR_RED, COLOR_BG); tft.setCursor(14, 104); tft.print(rtcHasBeenSynced ? "LOCKED" : "UNLOCKED");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(16, 138); tft.print("escape");
}

void renderWifiScanView() {
  tft.fillRect(8, 22, 112, 132, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(16, 26); tft.print("WIFI SCANNER");
  if (scanNetworkCount == -1) { tft.drawRect(10, 38, 108, 92, COLOR_STEEL); tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.setCursor(16, 68); tft.print("SCANNING..."); return; }
  if (scanNetworkCount == 0) { tft.drawRect(10, 38, 108, 92, COLOR_STEEL); tft.setTextColor(HW_COLOR_RED, COLOR_BG); tft.setCursor(16, 68); tft.print("NO AP FOUND"); } 
  else {
    for (int i = 0; i < 4; i++) {
      int idx = scanScrollIndex + i; if (idx >= scanNetworkCount) break;
      int y = 38 + (i * 22); bool isSel = (idx == scanSelectedIndex);
      if (isSel) { tft.fillRect(10, y, 108, 20, COLOR_CYAN); tft.setTextColor(COLOR_BLACK, COLOR_CYAN); } else { tft.drawRect(10, y, 108, 20, COLOR_STEEL); tft.setTextColor(COLOR_WHITE, COLOR_BG); }
      tft.setTextSize(1); tft.setCursor(14, y + 3); String ssid = WiFi.SSID(idx); if (ssid.length() > 8) ssid = ssid.substring(0, 7) + ".."; tft.print(ssid);
      
      bool isOpen = (WiFi.encryptionType(idx) == WIFI_AUTH_OPEN);
      if (isOpen) {
        tft.setTextColor(COLOR_LIME, isSel ? COLOR_CYAN : COLOR_BG);
        tft.setCursor(72, y + 3); tft.print("[OPEN]");
      }
      
      tft.setCursor(14, y + 11); tft.setTextColor(isSel ? COLOR_BLACK : COLOR_CYAN, isSel ? COLOR_CYAN : COLOR_BG); tft.print(WiFi.RSSI(idx)); tft.print("dBm");
      tft.setCursor(68, y + 11); tft.setTextColor(isSel ? COLOR_BLACK : COLOR_ICE, isSel ? COLOR_CYAN : COLOR_BG); tft.print("CH:"); tft.print(WiFi.channel(idx));
    }
  }
  tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(16, 138); tft.print("connect"); tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(72, 138); tft.print("escape");
}

void initWifiScanView() {
  currentSettingsView = SETTINGS_VIEW_WIFI_SCAN; scanNetworkCount = -1; scanScrollIndex = 0; scanSelectedIndex = 0; renderWifiScanView();
  WiFi.mode(WIFI_STA); 
  // Removed explicit WiFi disconnect to allow scanning while keeping existing connection active
  delay(50); scanNetworkCount = WiFi.scanNetworks(false, false); renderWifiScanView();
}

void initWifiConnectingView() {
  currentSettingsView = SETTINGS_VIEW_WIFI_CONNECTING;
  tft.fillRect(8, 22, 112, 132, COLOR_BG); tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(16, 26); tft.print("UPLINK STATUS");
  tft.drawRect(10, 38, 108, 92, COLOR_STEEL); tft.setTextColor(COLOR_CYAN, COLOR_BG); tft.setCursor(14, 48); tft.print("TARGET:");
  tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.setCursor(14, 60); String dSsid = String(wifiTargetSSID); if(dSsid.length() > 14) dSsid = dSsid.substring(0, 13) + ".."; tft.print(dSsid);
  tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(14, 82); tft.print("AUTHENTICATING...");
  tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(16, 138); tft.print("abort");
  
  if (strlen(wifiTargetPass) > 0) {
    WiFi.begin(wifiTargetSSID, wifiTargetPass);
  } else {
    WiFi.begin(wifiTargetSSID);
  }
  wifiConnectTimer = millis();
}

void initRebootView() {
  currentSettingsView = SETTINGS_VIEW_REBOOT; tft.fillRect(8, 22, 112, 132, COLOR_BG);
  tft.setTextSize(1); tft.setTextColor(HW_COLOR_RED, COLOR_BG); tft.setCursor(20, 26); tft.print("REBOOT SYSTEM"); tft.drawRect(10, 38, 108, 92, COLOR_CYAN);
  tft.setTextColor(COLOR_WHITE, COLOR_BG); tft.setCursor(14, 48); tft.print("CONFIRM RESTART?"); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(14, 66); tft.print("ALL SUBSYSTEMS"); tft.setCursor(14, 78); tft.print("WILL CYCLICLY"); tft.setCursor(14, 90); tft.print("REINITIALIZE.");
  tft.drawRect(14, 106, 100, 18, COLOR_STEEL); tft.setTextColor(COLOR_ACT_SEL, COLOR_BG); tft.setCursor(42, 112); tft.print("reboot"); tft.setTextColor(COLOR_ACT_ESC, COLOR_BG); tft.setCursor(16, 138); tft.print("escape");
}

void executeRebootSequence() {
  tft.fillScreen(COLOR_BLACK); tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BLACK); tft.setCursor(18, 64); tft.print("CYCLING POWER...");
  tft.drawRect(18, 80, 92, 6, COLOR_STEEL); for (int w = 0; w <= 90; w += 6) { tft.fillRect(19, 81, w, 4, COLOR_CYAN); delay(25); }
  tft.fillRect(0, 0, 128, 160, COLOR_BLACK); tft.fillCircle(64, 80, 2, COLOR_CYAN); delay(120); ESP.restart();
}

void updateSettingsApp(InputAction &input) {
  if (input == ACTION_BACK) {
    if (currentSettingsView == SETTINGS_VIEW_NET_STATUS || currentSettingsView == SETTINGS_VIEW_WIFI_SCAN || currentSettingsView == SETTINGS_VIEW_WIFI_CONNECTING) {
      if (currentSettingsView == SETTINGS_VIEW_WIFI_SCAN) { WiFi.scanDelete(); } 
      else if (currentSettingsView == SETTINGS_VIEW_WIFI_CONNECTING) { WiFi.disconnect(); } // Explicitly disconnect only to cancel a fresh auth attempt
      initNetMenuView(); input = ACTION_NONE; return;
    } else if (currentSettingsView != SETTINGS_VIEW_MENU) { initSettingsMenuView(); input = ACTION_NONE; return; }
  }

  switch (currentSettingsView) {
    case SETTINGS_VIEW_MENU:
      if (input == ACTION_NAV) { settingsMenuIndex = (settingsMenuIndex + 1) % TOTAL_SETTINGS_MENU_ITEMS; renderSettingsMenu(); }
      else if (input == ACTION_PREV) { settingsMenuIndex = (settingsMenuIndex - 1 + TOTAL_SETTINGS_MENU_ITEMS) % TOTAL_SETTINGS_MENU_ITEMS; renderSettingsMenu(); }
      else if (input == ACTION_SELECT) {
        if (settingsMenuIndex == 0) initTelemetryView(); else if (settingsMenuIndex == 1) initThemeView(); else if (settingsMenuIndex == 2) initControlMapView();
        else if (settingsMenuIndex == 3) initStorageView(); else if (settingsMenuIndex == 4) initNetMenuView(); else if (settingsMenuIndex == 5) initRebootView();
      } break;
    case SETTINGS_VIEW_THEME:
      if (input == ACTION_NAV || input == ACTION_PREV) { currentThemeIdx = (currentThemeIdx + 1) % TOTAL_THEMES; renderThemeView(); }
      else if (input == ACTION_SELECT) { prefs.begin("retro_sys", false); prefs.putUChar("theme_id", currentThemeIdx); prefs.end(); initSettingsMenuView(); } break;
    case SETTINGS_VIEW_CONTROL_MAP:
      if (input == ACTION_NAV) { controlMapCursor = (controlMapCursor + 1) % 4; renderControlMapView(); }
      else if (input == ACTION_PREV) { controlMapCursor = (controlMapCursor - 1 + 4) % 4; renderControlMapView(); }
      else if (input == ACTION_SELECT) { buttonActionMap[controlMapCursor] = (buttonActionMap[controlMapCursor] + 1) % 4; prefs.begin("retro_sys", false); prefs.putBytes("btn_map", buttonActionMap, 4); prefs.end(); renderControlMapView(); } break;
    case SETTINGS_VIEW_STORAGE:
      if (input == ACTION_SELECT) {
        prefs.begin("retro_games", false); prefs.clear(); prefs.end();
        siHighScore = 0; snHighScore = 0; fbHighScore = 0; fwHighScore = 0; noHighScore = 0; ldHighScore = 0;
        tft.fillRect(14, 40, 100, 88, COLOR_BG); tft.setTextColor(COLOR_LIME, COLOR_BG); tft.setCursor(14, 70); tft.print("DATA CLEARED");
      } break;
    case SETTINGS_VIEW_NET_MENU:
      if (input == ACTION_NAV) { netMenuIndex = (netMenuIndex + 1) % TOTAL_NET_MENU_ITEMS; renderNetMenuView(); }
      else if (input == ACTION_PREV) { netMenuIndex = (netMenuIndex - 1 + TOTAL_NET_MENU_ITEMS) % TOTAL_NET_MENU_ITEMS; renderNetMenuView(); }
      else if (input == ACTION_SELECT) { 
        if (netMenuIndex == 0) initNetworkView(); 
        else if (netMenuIndex == 1) initWifiScanView(); 
        else if (netMenuIndex == 2) { 
            WiFi.disconnect(true); 
            WiFi.mode(WIFI_OFF); 
            tft.fillRect(12, 100, 104, 20, COLOR_BG); 
            tft.setTextColor(HW_COLOR_RED, COLOR_BG); 
            tft.setCursor(14, 106); 
            tft.print("WIFI DISCONNECTED"); 
            delay(1000); 
            renderNetMenuView(); 
        } 
      } break;
    case SETTINGS_VIEW_WIFI_SCAN:
      if (scanNetworkCount > 0) {
        if (input == ACTION_NAV) { if (scanSelectedIndex < scanNetworkCount - 1) { scanSelectedIndex++; if (scanSelectedIndex >= scanScrollIndex + 4) scanScrollIndex++; renderWifiScanView(); } }
        else if (input == ACTION_PREV) { if (scanSelectedIndex > 0) { scanSelectedIndex--; if (scanSelectedIndex < scanScrollIndex) scanScrollIndex--; renderWifiScanView(); } }
        else if (input == ACTION_SELECT) { 
          strncpy(wifiTargetSSID, WiFi.SSID(scanSelectedIndex).c_str(), 32); wifiTargetSSID[32] = '\0';
          bool isOpen = (WiFi.encryptionType(scanSelectedIndex) == WIFI_AUTH_OPEN);
          if (isOpen) {
            wifiTargetPass[0] = '\0';
            saveSavedNetwork(wifiTargetSSID, "");
            initWifiConnectingView();
          } else {
            wifiTargetPass[0] = '\0';
            for (int i = 0; i < totalSavedNetworks; i++) {
              if (strcmp(savedNetworks[i].ssid, wifiTargetSSID) == 0) {
                strncpy(wifiTargetPass, savedNetworks[i].pass, 64);
                break;
              }
            }
            if (strlen(wifiTargetPass) > 0) {
              initWifiConnectingView();
            } else {
              launchKeyboardApp(KB_TARGET_WIFI_PASS); 
            }
          }
        }
      } break;
    case SETTINGS_VIEW_WIFI_CONNECTING: {
      unsigned long now = millis();
      if (WiFi.status() == WL_CONNECTED) { tft.fillRect(14, 100, 100, 16, COLOR_STEEL); tft.setTextColor(COLOR_LIME, COLOR_STEEL); tft.setCursor(16, 104); tft.print("CONNECTED!"); delay(1500); initNetworkView(); } 
      else if (now - wifiConnectTimer > 10000) { tft.fillRect(14, 100, 100, 16, COLOR_STEEL); tft.setTextColor(HW_COLOR_RED, COLOR_STEEL); tft.setCursor(16, 104); tft.print("AUTH TIMEOUT"); WiFi.disconnect(); delay(2000); initWifiScanView(); } break;
    }
    case SETTINGS_VIEW_REBOOT: if (input == ACTION_SELECT) { executeRebootSequence(); } break;
    default: break;
  }
}

// =============================================================================
// BOOT STAGES
// =============================================================================
void initVersionFrame() {
  tft.fillScreen(COLOR_BLACK);
  bootCtx.resolvedChars = 0;
}

void tickVersionFrame(unsigned long now) {
  unsigned long elapsed = now - bootCtx.stateStartTime;
  if (elapsed > 200 && bootCtx.resolvedChars == 0) {
    tft.setTextSize(2); tft.setTextColor(COLOR_CYAN, COLOR_BLACK); tft.setCursor(28, 60); tft.print("JARVIS"); bootCtx.resolvedChars = 1;
  }
  if (elapsed > 700 && bootCtx.resolvedChars == 1) {
    tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BLACK); tft.setCursor(19, 80); tft.print("LOADING V.01..."); bootCtx.resolvedChars = 2;
  }
  if (elapsed > 1500) { setBootState(BOOT_STATE_LINUX_LOGS); }
}

enum LinuxBootSubStage { LNX_STEP_ROOTFS, LNX_STEP_SPI, LNX_STEP_WIFI_INIT, LNX_STEP_WIFI_WAIT, LNX_STEP_NTP_SYNC, LNX_STEP_RADIO_OFF, LNX_STEP_DONE };
LinuxBootSubStage lnxStep = LNX_STEP_ROOTFS; 
unsigned long wifiConnectStart = 0;

void printLogLine(int line, const char* status, uint16_t statusColor, const char* msg, uint16_t msgColor) {
  int yPos = 18 + (line * 18);
  tft.setTextSize(1); tft.setCursor(4, yPos); tft.setTextColor(COLOR_CYAN, COLOR_BLACK); tft.print("["); tft.setTextColor(statusColor, COLOR_BLACK); tft.print(status); tft.setTextColor(COLOR_CYAN, COLOR_BLACK); tft.print("] ");
  tft.setCursor(40, yPos); tft.setTextColor(msgColor, COLOR_BLACK); tft.print(msg);
}

void initLinuxBoot() { 
  tft.fillScreen(COLOR_BLACK); 
  lnxStep = LNX_STEP_ROOTFS; 
  bootCtx.linuxLogIndex = 0; 
}

void tickLinuxBoot(unsigned long now) {
  switch (lnxStep) {
    case LNX_STEP_ROOTFS:
      if (now - bootCtx.lastFrameTime >= 100) { bootCtx.lastFrameTime = now; printLogLine(0, " OK ", COLOR_LIME, "vfs: mount", COLOR_ICE); lnxStep = LNX_STEP_SPI; } break;
    case LNX_STEP_SPI:
      if (now - bootCtx.lastFrameTime >= 100) { bootCtx.lastFrameTime = now; printLogLine(1, " OK ", COLOR_LIME, "spi: 80MHz", COLOR_ICE); lnxStep = LNX_STEP_WIFI_INIT; } break;
    case LNX_STEP_WIFI_INIT:
      if (now - bootCtx.lastFrameTime >= 100) { 
        bootCtx.lastFrameTime = now; 
        printLogLine(2, "WAIT", COLOR_AMBER, "net: linking", COLOR_CYAN); 
        WiFi.mode(WIFI_STA); 
        
        int n = WiFi.scanNetworks(false, true);
        bool autoConnected = false;
        for (int i = 0; i < n; i++) {
          String foundSsid = WiFi.SSID(i);
          for (int j = 0; j < totalSavedNetworks; j++) {
            if (foundSsid.equals(savedNetworks[j].ssid)) {
              if (strlen(savedNetworks[j].pass) > 0) {
                WiFi.begin(savedNetworks[j].ssid, savedNetworks[j].pass);
              } else {
                WiFi.begin(savedNetworks[j].ssid);
              }
              autoConnected = true;
              break;
            }
          }
          if (autoConnected) break;
        }
        if (!autoConnected) {
          WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        }
        WiFi.scanDelete();

        wifiConnectStart = now; 
        lnxStep = LNX_STEP_WIFI_WAIT; 
      } break;
    case LNX_STEP_WIFI_WAIT:
      if (WiFi.status() == WL_CONNECTED) { printLogLine(2, " OK ", COLOR_LIME, "net: linked ", COLOR_ICE); printLogLine(3, "WAIT", COLOR_AMBER, "ntp: sync...", COLOR_CYAN); configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2); wifiConnectStart = now; lnxStep = LNX_STEP_NTP_SYNC; } else if (now - wifiConnectStart > WIFI_BOOT_TIMEOUT_MS) { printLogLine(2, "FAIL", HW_COLOR_RED, "net: offline", COLOR_STEEL); lnxStep = LNX_STEP_RADIO_OFF; } break;
    case LNX_STEP_NTP_SYNC: {
      time_t t; struct tm ti; time(&t); localtime_r(&t, &ti);
      if (ti.tm_year > (2020 - 1900)) { rtcHasBeenSynced = true; printLogLine(3, " OK ", COLOR_LIME, "ntp: locked ", COLOR_ICE); lnxStep = LNX_STEP_RADIO_OFF; } else if (now - wifiConnectStart > NTP_BOOT_TIMEOUT_MS) { printLogLine(3, "FAIL", HW_COLOR_RED, "ntp: timeout", COLOR_STEEL); lnxStep = LNX_STEP_RADIO_OFF; } break;
    }
    case LNX_STEP_RADIO_OFF:
      WiFi.disconnect(true); WiFi.mode(WIFI_OFF); printLogLine(4, "DONE", COLOR_CYAN, ">> SYS READY", COLOR_AMBER); lnxStep = LNX_STEP_DONE; bootCtx.stateStartTime = now; break;
    case LNX_STEP_DONE:
      if (now - bootCtx.stateStartTime > 700) { setBootState(BOOT_STATE_CRT_BEAM); } break;
  }
}

void initCRTBeam() { tft.fillScreen(COLOR_BLACK); }
void tickCRTBeam(unsigned long now) {
  unsigned long elapsed = now - bootCtx.stateStartTime;
  if (elapsed < 180) { tft.fillCircle(64, 80, 2, COLOR_CYAN); } else if (elapsed < 420) { int w = map(elapsed, 180, 420, 4, 128); int x = 64 - (w / 2); tft.drawFastHLine(x, 80, w, COLOR_CYAN); tft.drawFastHLine(x, 79, w, COLOR_CYAN_DIM); tft.drawFastHLine(x, 81, w, COLOR_CYAN_DIM); } else { setBootState(BOOT_STATE_RASTER_EXPAND); }
}

void initRasterExpand() { bootCtx.lastRasterHeight = 1; }
void tickRasterExpand(unsigned long now) {
  unsigned long elapsed = now - bootCtx.stateStartTime;
  if (elapsed < 320) {
    int currentHeight = map(elapsed, 0, 320, 1, 80);
    if (currentHeight > bootCtx.lastRasterHeight) {
      int topY = 80 - currentHeight; int botY = 80 + currentHeight - 1; int deltaH = currentHeight - bootCtx.lastRasterHeight;
      tft.fillRect(0, topY, 128, deltaH, COLOR_BG); tft.fillRect(0, botY - deltaH + 1, 128, deltaH, COLOR_BG); tft.drawFastHLine(0, topY, 128, COLOR_CYAN); tft.drawFastHLine(0, botY, 128, COLOR_CYAN); bootCtx.lastRasterHeight = currentHeight;
    }
  } else { tft.fillRect(0, 0, 128, 160, COLOR_BG); drawIndustrialHUD(); setBootState(BOOT_STATE_GRID_SCAN); }
}

void initGridScan() { bootCtx.horizonScanOffset = 0; }
void tickGridScan(unsigned long now) {
  if (now - bootCtx.lastFrameTime >= 35) {
    bootCtx.lastFrameTime = now; bootCtx.horizonScanOffset = (bootCtx.horizonScanOffset + 2) % 16;
    for (int x = 8; x <= 120; x += 28) { tft.drawLine(x, 150, 64, 86, COLOR_STEEL); }
    for (int d = 0; d < 64; d += 16) { int lineY = 88 + ((d + bootCtx.horizonScanOffset) % 62); if (lineY <= 150) { tft.drawFastHLine(12, lineY, 104, (lineY > 138) ? COLOR_CYAN : COLOR_STEEL); } }
    tft.drawFastHLine(48, 86, 32, COLOR_ICE); tft.drawFastVLine(64, 82, 9, COLOR_ICE);
  }
  if (now - bootCtx.stateStartTime > 1000) { tft.fillRect(8, 54, 112, 52, COLOR_BG); setBootState(BOOT_STATE_DECRYPT_NAME); }
}

void initDecryptName() { bootCtx.resolvedChars = 0; bootCtx.scrambleCycles = 0; memset(bootCtx.displayBuffer, ' ', BootContext::GREETING_LEN); bootCtx.displayBuffer[BootContext::GREETING_LEN] = '\0'; }
void tickDecryptName(unsigned long now) {
  if (now - bootCtx.lastFrameTime >= 40) {
    bootCtx.lastFrameTime = now;
    for (uint8_t i = bootCtx.resolvedChars; i < BootContext::GREETING_LEN; i++) { if (BootContext::TARGET_GREETING[i] == ' ') { bootCtx.displayBuffer[i] = ' '; } else { bootCtx.displayBuffer[i] = (char)random(33, 90); } }
    bootCtx.scrambleCycles++; if (bootCtx.scrambleCycles >= 3) { bootCtx.scrambleCycles = 0; bootCtx.displayBuffer[bootCtx.resolvedChars] = BootContext::TARGET_GREETING[bootCtx.resolvedChars]; bootCtx.resolvedChars++; }
    tft.drawRect(10, 58, 108, 38, COLOR_STEEL); tft.drawFastHLine(12, 58, 18, COLOR_CYAN); tft.drawFastHLine(98, 58, 18, COLOR_CYAN);
    tft.setTextSize(1); tft.setTextColor(COLOR_AMBER, COLOR_BG); tft.setCursor(16, 64); tft.print("OPERATOR IDENT:");
    tft.setTextSize(1); tft.setTextColor(COLOR_ICE, COLOR_BG); tft.setCursor(20, 78); tft.print(bootCtx.displayBuffer);
    int cursorX = 20 + (BootContext::GREETING_LEN * 6) + 2; if ((now / 100) % 2 == 0) { tft.fillRect(cursorX, 78, 5, 8, COLOR_CYAN); } else { tft.fillRect(cursorX, 78, 5, 8, COLOR_BG); }
  }
  if (bootCtx.resolvedChars >= BootContext::GREETING_LEN) { bootCtx.displayBuffer[BootContext::GREETING_LEN] = '\0'; tft.setTextColor(COLOR_LIME, COLOR_BG); tft.setCursor(20, 78); tft.print(BootContext::TARGET_GREETING); setBootState(BOOT_STATE_SYS_LOCK); }
}

void initSysLock() {}
void tickSysLock(unsigned long now) {
  unsigned long elapsed = now - bootCtx.stateStartTime;
  if (elapsed > 250) { tft.drawFastHLine(18, 104, 92, COLOR_STEEL); tft.setTextSize(1); tft.setTextColor(COLOR_LIME, COLOR_BG); tft.setCursor(22, 112); tft.print("[ ACCESS GRANTED ]"); }
  int barWidth = map(constrain(elapsed, 0, 900), 0, 900, 0, 90); tft.fillRect(19, 126, barWidth, 3, COLOR_CYAN); tft.drawRect(18, 125, 92, 5, COLOR_STEEL);
  if (elapsed >= 1200) { setBootState(BOOT_STATE_IDLE); initHomeScreen(); }
}

void setBootState(BootState newState) {
  bootCtx.currentState = newState; bootCtx.stateStartTime = millis(); bootCtx.lastFrameTime = millis();
  switch (newState) {
    case BOOT_STATE_VERSION:     initVersionFrame(); break;
    case BOOT_STATE_LINUX_LOGS:   initLinuxBoot(); break;
    case BOOT_STATE_CRT_BEAM:     initCRTBeam(); break;
    case BOOT_STATE_RASTER_EXPAND:  initRasterExpand(); break;
    case BOOT_STATE_GRID_SCAN:    initGridScan(); break;
    case BOOT_STATE_DECRYPT_NAME:   initDecryptName(); break;
    case BOOT_STATE_SYS_LOCK:     initSysLock(); break;
    case BOOT_STATE_IDLE:         break;
  }
}

void updateBootAnimation() {
  unsigned long now = millis();
  switch (bootCtx.currentState) {
    case BOOT_STATE_VERSION:     tickVersionFrame(now); break;
    case BOOT_STATE_LINUX_LOGS:   tickLinuxBoot(now); break;
    case BOOT_STATE_CRT_BEAM:     tickCRTBeam(now); break;
    case BOOT_STATE_RASTER_EXPAND:  tickRasterExpand(now); break;
    case BOOT_STATE_GRID_SCAN:    tickGridScan(now); break;
    case BOOT_STATE_DECRYPT_NAME:   tickDecryptName(now); break;
    case BOOT_STATE_SYS_LOCK:     tickSysLock(now); break;
    case BOOT_STATE_IDLE:         break;
  }
}

// =============================================================================
// MAIN ENTRY & DISPATCH LOOP
// =============================================================================
void setup() {
  Serial.begin(115200);

#if ENABLE_PHYSICAL_BUTTONS
  pinMode(PIN_BTN_NAV, INPUT_PULLUP);
  pinMode(PIN_BTN_PREV, INPUT_PULLUP);
  pinMode(PIN_BTN_SEL, INPUT_PULLUP);
  pinMode(PIN_BTN_BACK, INPUT_PULLUP);
#endif

  if (!LittleFS.begin(true)) { Serial.println("[FS] Error mounting LittleFS"); }

  prefs.begin("retro_sys", true);
  currentThemeIdx = prefs.getUChar("theme_id", 0); if (currentThemeIdx >= TOTAL_THEMES) currentThemeIdx = 0;
  if (prefs.isKey("btn_map")) { prefs.getBytes("btn_map", buttonActionMap, 4); }
  prefs.end();

  loadTodos();
  loadSavedNetworks();
  
  tft.initR(INITR_BLACKTAB); 
  tft.setRotation(2); // Rotated 180 degrees

  Serial.println("\n=============================================");
  Serial.println("[SYSTEM] J.A.R.V.I.S. Core V.01 Initialized");
  Serial.println("[MODULES] 6 Arcade Games, E.D.I.T.H, and F.R.I.D.A.Y Loaded");
  Serial.println("=============================================\n");

  setBootState(BOOT_STATE_VERSION);
}

void loop() {
  InputAction input = pollInputs();

  switch (currentAppState) {
    case STATE_BOOTING: updateBootAnimation(); break;
    case STATE_HOMESCREEN: updateHomeScreen(); if (input == ACTION_SELECT) initRollMenuScreen(); break;
    case STATE_ROLL_MENU:
      if (input == ACTION_NAV) { activeRollIndex = (activeRollIndex + 1) % TOTAL_MENU_ITEMS; transitionRollMenu(1); }
      else if (input == ACTION_PREV) { activeRollIndex = (activeRollIndex - 1 + TOTAL_MENU_ITEMS) % TOTAL_MENU_ITEMS; transitionRollMenu(-1); }
      else if (input == ACTION_SELECT) { launchActiveApp(); } else if (input == ACTION_BACK) { initHomeScreen(); } break;
    case STATE_APP_ACTIVE: if (input == ACTION_BACK) initRollMenuScreen(); break;
    case STATE_APP_CLOCK: updateClockApp(input); if (input == ACTION_BACK && currentClockView == CLOCK_VIEW_MENU) initRollMenuScreen(); break;
    case STATE_APP_GAMES: updateGamesApp(input); if (input == ACTION_BACK && currentGamesView == GAMES_VIEW_SELECT_GAME) initRollMenuScreen(); break;
    case STATE_APP_SETTINGS: updateSettingsApp(input); if (input == ACTION_BACK && currentSettingsView == SETTINGS_VIEW_MENU) initRollMenuScreen(); break;
    case STATE_APP_FRIDAY: updateFridayApp(input); break;
    case STATE_APP_TODO: updateTodoApp(input); break;
    case STATE_APP_READER: updateReaderApp(input); break;
    case STATE_APP_KEYBOARD: updateKeyboardApp(input); break;
    case STATE_APP_EDITH: updateEdithApp(input); break;
  }
}
