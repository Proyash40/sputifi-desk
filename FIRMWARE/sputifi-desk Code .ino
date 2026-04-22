{\rtf1\ansi\ansicpg1252\cocoartf2869
\cocoatextscaling0\cocoaplatform0{\fonttbl\f0\fswiss\fcharset0 Helvetica;}
{\colortbl;\red255\green255\blue255;}
{\*\expandedcolortbl;;}
\paperw11900\paperh16840\margl1440\margr1440\vieww15020\viewh9940\viewkind0
\pard\tx720\tx1440\tx2160\tx2880\tx3600\tx4320\tx5040\tx5760\tx6480\tx7200\tx7920\tx8640\pardirnatural\partightenfactor0

\f0\fs24 \cf0 #include <Adafruit_GFX.h>\
#include <Adafruit_ILI9341.h>\
#include <SPI.h>\
\
#define TFT_CS   4\
#define TFT_DC   5\
#define TFT_RST  6\
#define TFT_MOSI 19\
#define TFT_CLK  18\
\
Adafruit_ILI9341 tft = Adafruit_ILI9341(TFT_CS, TFT_DC, TFT_RST);\
\
const int btnLeft  = 0;\
const int btnMid   = 1;\
const int btnRight = 10;\
\
const int encCLK = 7;\
const int encDT  = 2;\
const int encSW  = 3;\
\
int lastCLKState = HIGH;\
unsigned long lastEncTime = 0;\
\
int volumeLevel = 50;\
bool isMuted = false;\
\
unsigned long pressStart[3] = \{0, 0, 0\};\
bool pressed[3] = \{false, false, false\};\
const unsigned long longPressTime = 500;\
\
int currentPage = 0;\
bool isPlaying = true;\
unsigned long lastProgressUpdate = 0;\
unsigned long lastScreenRefresh = 0;\
int progressPercent = 0;\
\
String wifiStatus = "Connected";\
String apiStatus  = "Test Data";\
String lastAction = "System Ready";\
\
struct Track \{\
  String title;\
  String artist;\
  String album;\
  int durationSec;\
\};\
\
Track tracks[] = \{\
  \{"Blinding Lights", "The Weeknd", "After Hours", 200\},\
  \{"As It Was", "Harry Styles", "Harry's House", 167\},\
  \{"Starboy", "The Weeknd", "Starboy", 230\},\
  \{"Believer", "Imagine Dragons", "Evolve", 204\}\
\};\
\
const int trackCount = sizeof(tracks) / sizeof(tracks[0]);\
int currentTrack = 0;\
\
#define BG_COLOR    ILI9341_BLACK\
#define TEXT_COLOR  ILI9341_WHITE\
#define ACCENT      ILI9341_GREEN\
#define SUBTEXT     ILI9341_CYAN\
#define WARN        ILI9341_YELLOW\
#define BAR_BG      ILI9341_DARKGREY\
\
void logAction(const String& msg) \{\
  lastAction = msg;\
  Serial.println(msg);\
\}\
\
void volumeUp() \{\
  if (!isMuted && volumeLevel < 100) \{\
    volumeLevel += 5;\
    if (volumeLevel > 100) volumeLevel = 100;\
    Serial.println("Volume Up");\
    refreshDisplay();\
  \}\
\}\
\
void volumeDown() \{\
  if (!isMuted && volumeLevel > 0) \{\
    volumeLevel -= 5;\
    if (volumeLevel < 0) volumeLevel = 0;\
    Serial.println("Volume Down");\
    refreshDisplay();\
  \}\
\}\
\
void toggleMute() \{\
  isMuted = !isMuted;\
  Serial.println(isMuted ? "Muted" : "Unmuted");\
  refreshDisplay();\
\}\
\
void drawHeader(const String& title) \{\
  tft.fillScreen(BG_COLOR);\
  tft.setTextSize(2);\
  tft.setTextColor(ACCENT);\
  tft.setCursor(10, 10);\
  tft.println(title);\
  tft.drawLine(10, 30, 150, 30, ACCENT);\
\}\
\
void drawProgressBar(int x, int y, int w, int h, int percent) \{\
  tft.drawRect(x, y, w, h, TEXT_COLOR);\
  tft.fillRect(x + 1, y + 1, w - 2, h - 2, BAR_BG);\
  int fillWidth = map(percent, 0, 100, 0, w - 2);\
  tft.fillRect(x + 1, y + 1, fillWidth, h - 2, ACCENT);\
\}\
\
void drawNowPlayingPage() \{\
  drawHeader("Now Playing");\
  tft.setTextSize(2);\
  tft.setTextColor(TEXT_COLOR);\
  tft.setCursor(10, 45);\
  tft.println(tracks[currentTrack].title);\
  tft.setTextSize(2);\
  tft.setTextColor(SUBTEXT);\
  tft.setCursor(10, 70);\
  tft.println(tracks[currentTrack].artist);\
  tft.setTextSize(1);\
  tft.setTextColor(WARN);\
  tft.setCursor(10, 95);\
  tft.print("Status: ");\
  tft.println(isPlaying ? "Playing" : "Paused");\
  tft.setCursor(10, 110);\
  tft.print("Album: ");\
  tft.println(tracks[currentTrack].album);\
  drawProgressBar(10, 135, 140, 12, progressPercent);\
  tft.setTextColor(TEXT_COLOR);\
  tft.setCursor(10, 155);\
  tft.print(progressPercent);\
  tft.println("%");\
  tft.setTextSize(1);\
  tft.setTextColor(ILI9341_LIGHTGREY);\
  tft.setCursor(5, 175);\
  tft.println("Short: Prev / Play / Next");\
  tft.setCursor(5, 187);\
  tft.println("Long: Page- / Refresh / Page+");\
  tft.setCursor(10, 145);\
  tft.setTextColor(ILI9341_WHITE);\
  tft.print("Volume: ");\
  if (isMuted) \{\
    tft.println("Muted");\
  \} else \{\
    tft.print(volumeLevel);\
    tft.println("%");\
  \}\
\}\
\
void drawTrackInfoPage() \{\
  drawHeader("Track Info");\
  tft.setTextSize(2);\
  tft.setTextColor(TEXT_COLOR);\
  tft.setCursor(10, 45);\
  tft.println(tracks[currentTrack].title);\
  tft.setTextSize(1);\
  tft.setTextColor(SUBTEXT);\
  tft.setCursor(10, 75);\
  tft.print("Artist: ");\
  tft.println(tracks[currentTrack].artist);\
  tft.setCursor(10, 90);\
  tft.print("Album: ");\
  tft.println(tracks[currentTrack].album);\
  tft.setCursor(10, 105);\
  tft.print("Duration: ");\
  tft.print(tracks[currentTrack].durationSec);\
  tft.println(" sec");\
  tft.setCursor(10, 120);\
  tft.print("Track #: ");\
  tft.print(currentTrack + 1);\
  tft.print(" / ");\
  tft.println(trackCount);\
  tft.setCursor(10, 150);\
  tft.setTextColor(WARN);\
  tft.print("Last action:");\
  tft.setCursor(10, 165);\
  tft.setTextColor(TEXT_COLOR);\
  tft.println(lastAction);\
\}\
\
void drawSystemStatusPage() \{\
  drawHeader("System Status");\
  tft.setTextSize(1);\
  tft.setTextColor(TEXT_COLOR);\
  tft.setCursor(10, 45);\
  tft.print("WiFi: ");\
  tft.println(wifiStatus);\
  tft.setCursor(10, 60);\
  tft.print("API: ");\
  tft.println(apiStatus);\
  tft.setCursor(10, 75);\
  tft.print("Track Loaded: ");\
  tft.println(currentTrack + 1);\
  tft.setCursor(10, 90);\
  tft.print("Playback: ");\
  tft.println(isPlaying ? "Playing" : "Paused");\
  tft.setCursor(10, 115);\
  tft.setTextColor(WARN);\
  tft.println("This is a testing build that uses");\
  tft.setCursor(10, 127);\
  tft.println("imaginary track data to test");\
  tft.setCursor(10, 139);\
  tft.println("UI and input logic.");\
  tft.setCursor(10, 165);\
  tft.setTextColor(TEXT_COLOR);\
  tft.print("Page ");\
  tft.print(currentPage + 1);\
  tft.print(" of 3");\
\}\
\
void refreshDisplay() \{\
  if (currentPage == 0) \{\
    drawNowPlayingPage();\
  \} else if (currentPage == 1) \{\
    drawTrackInfoPage();\
  \} else \{\
    drawSystemStatusPage();\
  \}\
\}\
\
void previousTrack() \{\
  currentTrack--;\
  if (currentTrack < 0) currentTrack = trackCount - 1;\
  progressPercent = 0;\
  logAction("Previous Track");\
\}\
\
void nextTrack() \{\
  currentTrack++;\
  if (currentTrack >= trackCount) currentTrack = 0;\
  progressPercent = 0;\
  logAction("Next Track");\
\}\
\
void togglePlayback() \{\
  isPlaying = !isPlaying;\
  logAction(isPlaying ? "Playing" : "Paused");\
\}\
\
void pageLeft() \{\
  currentPage--;\
  if (currentPage < 0) currentPage = 2;\
  logAction("Page Left");\
\}\
\
void pageRight() \{\
  currentPage++;\
  if (currentPage > 2) currentPage = 0;\
  logAction("Page Right");\
\}\
\
void refreshSystem() \{\
  wifiStatus = "Connected";\
  apiStatus = "Refreshed";\
  logAction("Refreshing...");\
\}\
\
void shortPress(int id) \{\
  if (id == 0) previousTrack();\
  if (id == 1) togglePlayback();\
  if (id == 2) nextTrack();\
  refreshDisplay();\
\}\
\
void longPress(int id) \{\
  if (id == 0) pageLeft();\
  if (id == 1) refreshSystem();\
  if (id == 2) pageRight();\
  refreshDisplay();\
\}\
\
void handleButton(int pin, int id) \{\
  bool state = digitalRead(pin);\
  if (state == LOW && !pressed[id]) \{\
    pressed[id] = true;\
    pressStart[id] = millis();\
  \}\
  if (state == HIGH && pressed[id]) \{\
    pressed[id] = false;\
    unsigned long duration = millis() - pressStart[id];\
    if (duration < longPressTime) \{\
      shortPress(id);\
    \} else \{\
      longPress(id);\
    \}\
    delay(150);\
  \}\
\}\
\
void setup() \{\
  Serial.begin(115200);\
  pinMode(btnLeft, INPUT_PULLUP);\
  pinMode(btnMid, INPUT_PULLUP);\
  pinMode(btnRight, INPUT_PULLUP);\
  SPI.begin(TFT_CLK, -1, TFT_MOSI, TFT_CS);\
  tft.begin();\
  tft.setRotation(1);\
  refreshDisplay();\
  Serial.println("Spotify Display System Ready");\
  pinMode(encCLK, INPUT);\
  pinMode(encDT, INPUT);\
  pinMode(encSW, INPUT_PULLUP);\
  lastCLKState = digitalRead(encCLK);\
\}\
\
void loop() \{\
  handleButton(btnLeft, 0);\
  handleButton(btnMid, 1);\
  handleButton(btnRight, 2);\
  if (isPlaying && millis() - lastProgressUpdate > 1000) \{\
    lastProgressUpdate = millis();\
    progressPercent++;\
    if (progressPercent > 100) progressPercent = 0;\
  \}\
  int currentCLKState = digitalRead(encCLK);\
  if (currentCLKState != lastCLKState) \{\
    if (millis() - lastEncTime > 5) \{\
      lastEncTime = millis();\
      if (lastCLKState == HIGH && currentCLKState == LOW) \{\
        int dtState = digitalRead(encDT);\
        if (dtState == HIGH) \{\
          volumeUp();\
        \} else \{\
          volumeDown();\
        \}\
      \}\
    \}\
  \}\
  lastCLKState = currentCLKState;\
  if (digitalRead(encSW) == LOW) \{\
    toggleMute();\
    delay(200);\
  \}\
\}}