/*
  ============================================================================
  Sputifi-Desk: Spotify Currently Playing Display & Media Controller
  ESP32-C3 (Seeed Studio XIAO) + ILI9341 TFT Display
  
  Author: Proyash Kumar Sarkar
  Purpose: Real-time Spotify playback control with centered UI
  
  Hardware:
    - MCU: ESP32-C3 (Seeed Studio XIAO Supermini)
    - Display: 2.4" ILI9341 SPI TFT
    - Buttons: 3x Mechanical Switches (Active Low)
  
  Libraries Required:
    - TFT_eSPI (User_Setup.h pre-configured)
    - SpotifyESP32 by witnessmenow
    - ArduinoJson
    - ezButton (for debouncing)
  
  ============================================================================
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <TFT_eSPI.h>
#include <SpotifyArduino.h>
#include <ArduinoJson.h>
#include <ezButton.h>
#include <HTTPClient.h>
#include <driver/adc.h>

// ============================================================================
// CONFIGURATION SECTION
// ============================================================================

// WiFi Credentials
const char* WIFI_SSID     = "YOUR_SSID";
const char* WIFI_PASSWORD = "YOUR_PASSWORD";

// Spotify API Credentials
// Get these from: https://developer.spotify.com/dashboard
const char* SPOTIFY_CLIENT_ID     = "YOUR_CLIENT_ID";
const char* SPOTIFY_CLIENT_SECRET = "YOUR_CLIENT_SECRET";
const char* SPOTIFY_REFRESH_TOKEN = "YOUR_REFRESH_TOKEN";

// Battery ADC Pin (if available on your board)
#define BATTERY_ADC_PIN 26  // Adjust based on your board

// ============================================================================
// PIN DEFINITIONS (STRICT - As per hardware spec)
// ============================================================================

#define TFT_CS    7
#define TFT_RST   3
#define TFT_DC    5
#define TFT_MOSI  6
#define TFT_SCLK  4
#define TFT_MISO  -1

#define BTN_PREV        0
#define BTN_PLAY_PAUSE  1
#define BTN_NEXT        10

// ============================================================================
// DISPLAY & GRAPHICS CONSTANTS
// ============================================================================

TFT_eSPI tft = TFT_eSPI();

// Color Palette
#define BG_COLOR        TFT_BLACK
#define TEXT_COLOR      TFT_WHITE
#define ACCENT_COLOR    0x1DB954  // Spotify Green
#define SECONDARY_COLOR TFT_CYAN
#define STATUS_COLOR    TFT_YELLOW

// Display Dimensions
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  240

// ============================================================================
// BUTTON OBJECTS (Non-blocking debouncing via ezButton)
// ============================================================================

ezButton btnPrev(BTN_PREV);
ezButton btnPlayPause(BTN_PLAY_PAUSE);
ezButton btnNext(BTN_NEXT);

// ============================================================================
// SPOTIFY & API STATE MANAGEMENT
// ============================================================================

WiFiClientSecure client;
SpotifyArduino spotify(client, SPOTIFY_CLIENT_ID, SPOTIFY_CLIENT_SECRET, SPOTIFY_REFRESH_TOKEN);

// Current Track State
struct CurrentTrack {
  String trackId;
  String trackName;
  String artistName;
  int durationMs;
  int progressMs;
  boolean isPlaying;
  unsigned long lastFetchTime;
};

CurrentTrack currentTrack = {"", "", "", 0, 0, false, 0};

// ============================================================================
// TIMING CONTROL (All non-blocking via millis())
// ============================================================================

#define SPOTIFY_POLL_INTERVAL    5000   // Poll Spotify API every 5 seconds
#define BUTTON_DEBOUNCE_TIME     50     // ms
#define BATTERY_UPDATE_INTERVAL  5000   // Update battery every 5 seconds

unsigned long lastSpotifyPoll = 0;
unsigned long lastScreenRedraw = 0;
unsigned long lastBatteryUpdate = 0;
boolean needsScreenRedraw = true;
int batteryPercent = 100;

// ============================================================================
// FUNCTION DECLARATIONS
// ============================================================================

void initializeWiFi();
void initializeSpotify();
void initializeDisplay();
void initializeButtons();
void handleButtonPresses();
void pollSpotifyAPI();
void updateDisplay();
void drawCenteredUI();
void drawSpotifyLogo();
void drawMediaControls();
void drawTrackInfo();
void drawStatusBar();
void drawBatteryIndicator();
void drawProgressBar();
int getBatteryPercentage();
boolean sendPauseCommand();
boolean sendPlayCommand();
boolean sendNextCommand();
boolean sendPreviousCommand();
void handleSpotifyError(const String& context);
void drawFilledCircle(int x, int y, int radius, uint16_t color);
String formatTime(int seconds);

// ============================================================================
// SETUP
// ============================================================================

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n");
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   Sputifi-Desk: Initializing...        ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  initializeDisplay();
  initializeButtons();
  initializeWiFi();
  initializeSpotify();
  
  Serial.println("\n✓ System Ready!");
  Serial.println("Entering main loop...\n");
  
  // Force initial poll and screen redraw
  lastSpotifyPoll = millis() - SPOTIFY_POLL_INTERVAL;
  lastBatteryUpdate = millis() - BATTERY_UPDATE_INTERVAL;
  needsScreenRedraw = true;
}

// ============================================================================
// MAIN LOOP
// ============================================================================

void loop() {
  // Non-blocking button state updates
  handleButtonPresses();
  
  // Non-blocking Spotify polling
  unsigned long currentTime = millis();
  if (currentTime - lastSpotifyPoll >= SPOTIFY_POLL_INTERVAL) {
    lastSpotifyPoll = currentTime;
    pollSpotifyAPI();
  }
  
  // Non-blocking battery update
  if (currentTime - lastBatteryUpdate >= BATTERY_UPDATE_INTERVAL) {
    lastBatteryUpdate = currentTime;
    batteryPercent = getBatteryPercentage();
    needsScreenRedraw = true;
  }
  
  // Non-blocking display updates
  if (needsScreenRedraw || currentTime - lastScreenRedraw >= 500) {
    updateDisplay();
    needsScreenRedraw = false;
    lastScreenRedraw = currentTime;
  }
}

// ============================================================================
// INITIALIZATION FUNCTIONS
// ============================================================================

void initializeDisplay() {
  Serial.println("\n[INIT] TFT Display...");
  
  tft.init();
  tft.setRotation(1);  // Landscape mode
  tft.fillScreen(BG_COLOR);
  
  // Display splash screen
  tft.setTextColor(ACCENT_COLOR);
  tft.setTextSize(3);
  tft.setCursor(50, 80);
  tft.println("Sputifi");
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(1);
  tft.setCursor(80, 130);
  tft.println("Initializing...");
  
  Serial.println("✓ TFT Display initialized (ILI9341)");
}

void initializeButtons() {
  Serial.println("[INIT] Button inputs...");
  
  btnPrev.setDebounceTime(BUTTON_DEBOUNCE_TIME);
  btnPlayPause.setDebounceTime(BUTTON_DEBOUNCE_TIME);
  btnNext.setDebounceTime(BUTTON_DEBOUNCE_TIME);
  
  Serial.println("✓ Buttons initialized with debouncing");
}

void initializeWiFi() {
  Serial.println("[INIT] WiFi connection...");
  Serial.print("  SSID: ");
  Serial.println(WIFI_SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi connected!");
    Serial.print("  IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n✗ WiFi connection failed!");
  }
  
  client.setInsecure();
}

void initializeSpotify() {
  Serial.println("[INIT] Spotify API client...");
  
  if (spotify.refreshAccessToken()) {
    Serial.println("✓ Spotify authentication successful!");
  } else {
    Serial.println("✗ Spotify authentication failed!");
    Serial.println("  Check your Client ID, Secret, and Refresh Token");
  }
}

// ============================================================================
// BUTTON HANDLING (Non-blocking)
// ============================================================================

void handleButtonPresses() {
  btnPrev.loop();
  btnPlayPause.loop();
  btnNext.loop();
  
  // Previous Track
  if (btnPrev.isPressed()) {
    Serial.println("[BTN] Previous pressed");
    if (sendPreviousCommand()) {
      Serial.println("✓ Previous command sent");
    } else {
      Serial.println("✗ Previous command failed");
    }
    delay(500);
  }
  
  // Play/Pause Toggle
  if (btnPlayPause.isPressed()) {
    Serial.println("[BTN] Play/Pause pressed");
    
    if (currentTrack.isPlaying) {
      if (sendPauseCommand()) {
        Serial.println("✓ Pause command sent");
        currentTrack.isPlaying = false;
      } else {
        Serial.println("✗ Pause command failed");
      }
    } else {
      if (sendPlayCommand()) {
        Serial.println("✓ Play command sent");
        currentTrack.isPlaying = true;
      } else {
        Serial.println("✗ Play command failed");
      }
    }
    delay(500);
    needsScreenRedraw = true;
  }
  
  // Next Track
  if (btnNext.isPressed()) {
    Serial.println("[BTN] Next pressed");
    if (sendNextCommand()) {
      Serial.println("✓ Next command sent");
    } else {
      Serial.println("✗ Next command failed");
    }
    delay(500);
  }
}

// ============================================================================
// SPOTIFY API POLLING (Non-blocking)
// ============================================================================

void pollSpotifyAPI() {
  Serial.println("\n[API] Polling currently playing...");
  
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("✗ WiFi not connected, skipping poll");
    handleSpotifyError("WiFi disconnected");
    return;
  }
  
  CurrentlyPlaying currentlyPlaying = spotify.getCurrentlyPlaying();
  
  if (!currentlyPlaying.error) {
    String newTrackId = currentlyPlaying.trackName;
    boolean trackChanged = (newTrackId != currentTrack.trackId);
    
    if (trackChanged) {
      Serial.println("\n→ New track detected, updating display");
    }
    
    currentTrack.trackId = newTrackId;
    currentTrack.trackName = currentlyPlaying.trackName;
    currentTrack.artistName = currentlyPlaying.artistNames[0];
    currentTrack.durationMs = currentlyPlaying.durationMs;
    currentTrack.progressMs = currentlyPlaying.progressMs;
    currentTrack.isPlaying = currentlyPlaying.isPlaying;
    currentTrack.lastFetchTime = millis();
    
    if (trackChanged) {
      needsScreenRedraw = true;
    }
    
    Serial.println("✓ Poll successful");
    Serial.print("  Track: ");
    Serial.print(currentTrack.trackName);
    Serial.print(" - ");
    Serial.println(currentTrack.artistName);
    Serial.print("  Status: ");
    Serial.println(currentlyPlaying.isPlaying ? "Playing" : "Paused");
    
  } else {
    Serial.println("✗ Spotify API error");
    handleSpotifyError(currentlyPlaying.errorMessage);
  }
}

// ============================================================================
// SPOTIFY CONTROL COMMANDS
// ============================================================================

boolean sendPauseCommand() {
  if (spotify.pause()) {
    currentTrack.isPlaying = false;
    needsScreenRedraw = true;
    return true;
  }
  return false;
}

boolean sendPlayCommand() {
  if (spotify.play()) {
    currentTrack.isPlaying = true;
    needsScreenRedraw = true;
    return true;
  }
  return false;
}

boolean sendNextCommand() {
  if (spotify.next()) {
    delay(500);
    pollSpotifyAPI();
    return true;
  }
  return false;
}

boolean sendPreviousCommand() {
  if (spotify.previous()) {
    delay(500);
    pollSpotifyAPI();
    return true;
  }
  return false;
}

// ============================================================================
// DISPLAY RENDERING
// ============================================================================

void updateDisplay() {
  tft.fillScreen(BG_COLOR);
  drawStatusBar();
  drawCenteredUI();
  drawBatteryIndicator();
}

void drawStatusBar() {
  // Top bar
  tft.fillRect(0, 0, DISPLAY_WIDTH, 25, ACCENT_COLOR);
  tft.setTextColor(BG_COLOR);
  tft.setTextSize(1);
  tft.setCursor(10, 7);
  tft.print("SPOTIFY");
  
  // Status indicator
  tft.setCursor(280, 7);
  if (WiFi.status() == WL_CONNECTED) {
    tft.print("WiFi");
  } else {
    tft.print("---");
  }
}

void drawCenteredUI() {
  // Spotify Logo (centered, large)
  drawSpotifyLogo();
  
  // Track Info (below logo)
  drawTrackInfo();
  
  // Media Controls (at bottom)
  drawMediaControls();
  
  // Progress Bar
  drawProgressBar();
}

void drawSpotifyLogo() {
  // Draw a stylized Spotify circle logo in the center top area
  int centerX = DISPLAY_WIDTH / 2;
  int logoY = 60;
  int logoRadius = 20;
  
  // Draw circle outline
  tft.drawCircle(centerX, logoY, logoRadius, ACCENT_COLOR);
  tft.drawCircle(centerX, logoY, logoRadius - 2, ACCENT_COLOR);
  
  // Draw stylized dots for Spotify logo
  drawFilledCircle(centerX - 8, logoY - 5, 3, ACCENT_COLOR);
  drawFilledCircle(centerX, logoY + 8, 3, ACCENT_COLOR);
  drawFilledCircle(centerX + 8, logoY - 5, 3, ACCENT_COLOR);
}

void drawFilledCircle(int x, int y, int radius, uint16_t color) {
  for (int i = -radius; i <= radius; i++) {
    for (int j = -radius; j <= radius; j++) {
      if (i*i + j*j <= radius*radius) {
        tft.drawPixel(x + i, y + j, color);
      }
    }
  }
}

void drawTrackInfo() {
  int centerX = DISPLAY_WIDTH / 2;
  int trackY = 110;
  
  // Track Name (Large, centered)
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);  // Middle center
  
  String trackName = currentTrack.trackName;
  if (trackName.length() > 18) {
    trackName = trackName.substring(0, 15) + "...";
  }
  tft.drawString(trackName, centerX, trackY, 2);
  
  // Artist Name (Medium, centered)
  tft.setTextColor(SECONDARY_COLOR);
  tft.setTextSize(1);
  String artistName = currentTrack.artistName;
  if (artistName.length() > 25) {
    artistName = artistName.substring(0, 22) + "...";
  }
  tft.drawString(artistName, centerX, trackY + 30, 1);
}

void drawMediaControls() {
  int centerX = DISPLAY_WIDTH / 2;
  int controlsY = 190;
  int buttonSpacing = 80;
  
  // Previous Button
  tft.setTextColor(ACCENT_COLOR);
  tft.setTextSize(2);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("|<", centerX - buttonSpacing, controlsY, 2);
  
  // Play/Pause Button (larger)
  if (currentTrack.isPlaying) {
    tft.drawString("||", centerX, controlsY, 2);
  } else {
    tft.drawString(">", centerX, controlsY, 2);
  }
  
  // Next Button
  tft.drawString(">|", centerX + buttonSpacing, controlsY, 2);
}

void drawProgressBar() {
  int barX = 20;
  int barY = 150;
  int barWidth = DISPLAY_WIDTH - 40;
  int barHeight = 8;
  
  int fillWidth = 0;
  if (currentTrack.durationMs > 0) {
    fillWidth = (currentTrack.progressMs * barWidth) / currentTrack.durationMs;
  }
  
  // Draw background bar
  tft.fillRect(barX, barY, barWidth, barHeight, TFT_DARKGREY);
  
  // Draw filled portion
  if (fillWidth > 0) {
    tft.fillRect(barX, barY, fillWidth, barHeight, ACCENT_COLOR);
  }
  
  // Time display
  int progressSec = currentTrack.progressMs / 1000;
  int durationSec = currentTrack.durationMs / 1000;
  
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);
  tft.drawString(formatTime(progressSec), barX, barY + 12, 1);
  
  tft.setTextDatum(TR_DATUM);
  tft.drawString(formatTime(durationSec), barX + barWidth, barY + 12, 1);
}

void drawBatteryIndicator() {
  // Battery indicator at top right
  int batX = DISPLAY_WIDTH - 50;
  int batY = 8;
  
  tft.setTextColor(BG_COLOR);
  tft.setTextSize(1);
  tft.setTextDatum(TR_DATUM);
  tft.drawString(String(batteryPercent) + "%", batX - 5, batY, 1);
  
  // Battery bar
  int barWidth = 35;
  int barHeight = 10;
  tft.drawRect(batX - barWidth, batY - 2, barWidth, barHeight, BG_COLOR);
  
  // Fill based on percentage
  int fillWidth = (batteryPercent * (barWidth - 2)) / 100;
  uint16_t batteryColor = ACCENT_COLOR;
  if (batteryPercent < 20) batteryColor = TFT_RED;
  
  tft.fillRect(batX - barWidth + 1, batY - 1, fillWidth, barHeight - 2, batteryColor);
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

String formatTime(int seconds) {
  int minutes = seconds / 60;
  int secs = seconds % 60;
  
  String result = "";
  if (minutes < 10) result += "0";
  result += minutes;
  result += ":";
  if (secs < 10) result += "0";
  result += secs;
  
  return result;
}

int getBatteryPercentage() {
  // Read battery voltage from ADC
  // This assumes you have a battery connected to BATTERY_ADC_PIN
  // Adjust these values based on your battery setup
  
  adc1_channel_t channel = ADC1_CHANNEL_0;  // GPIO26
  int rawValue = adc1_get_raw(channel);
  
  // Convert ADC reading to percentage (adjust these values)
  // Typically: 4095 = 4.2V (100%), 2550 = 2.5V (0%)
  int batteryVoltage = (rawValue * 4200) / 4095;  // Convert to mV
  
  // Map voltage to percentage (3.0V = 0%, 4.2V = 100%)
  int percent = ((batteryVoltage - 3000) * 100) / 1200;
  
  if (percent < 0) percent = 0;
  if (percent > 100) percent = 100;
  
  return percent;
}

void handleSpotifyError(const String& context) {
  Serial.print("[ERROR] ");
  Serial.println(context);
  
  tft.fillScreen(BG_COLOR);
  tft.setTextColor(TFT_RED);
  tft.setTextSize(1);
  tft.setCursor(50, 100);
  tft.println("Spotify Error:");
  tft.setCursor(50, 120);
  tft.println(context.c_str());
}

// ============================================================================
// END OF FILE
// ============================================================================
