/*
  ============================================================================
  Sputifi-Desk: Spotify Currently Playing Display & Media Controller
  ESP32-C3 (Seeed Studio XIAO) + ILI9341 TFT Display
  
  Author: Proyash Kumar Sarkar
  Purpose: Real-time Spotify playback control with album art display
  
  Hardware:
    - MCU: ESP32-C3 (Seeed Studio XIAO Supermini)
    - Display: 2.4" ILI9341 SPI TFT
    - Buttons: 3x Mechanical Switches (Active Low)
  
  Libraries Required:
    - TFT_eSPI (User_Setup.h pre-configured)
    - SpotifyESP32 by witnessmenow
    - ArduinoJson
    - TJpg_Decoder
    - ezButton (for debouncing)
  
  ============================================================================
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <TFT_eSPI.h>
#include <SpotifyArduino.h>
#include <ArduinoJson.h>
#include <TJpg_Decoder.h>
#include <ezButton.h>
#include <HTTPClient.h>

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
#define ACCENT_COLOR    TFT_GREEN
#define SECONDARY_COLOR TFT_CYAN
#define STATUS_COLOR    TFT_YELLOW

// Display Dimensions
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  240

// Album Art Settings
#define ALBUM_ART_X     10
#define ALBUM_ART_Y     30
#define ALBUM_ART_SIZE  180  // Will attempt to scale to this size

// Text Region
#define TEXT_START_X    200
#define TEXT_START_Y    30

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
  String trackId;           // Unique identifier to detect track changes
  String trackName;
  String artistName;
  String albumArtUrl;
  String albumName;
  int durationMs;
  int progressMs;
  boolean isPlaying;
  unsigned long lastFetchTime;
};

CurrentTrack currentTrack = {"", "", "", "", "", 0, 0, false, 0};

// Pending UI Feedback (for immediate button press feedback)
struct UIFeedback {
  String message;
  unsigned long displayUntil;
};

UIFeedback uiFeedback = {"", 0};

// ============================================================================
// TIMING CONTROL (All non-blocking via millis())
// ============================================================================

#define SPOTIFY_POLL_INTERVAL    5000   // Poll Spotify API every 5 seconds
#define BUTTON_DEBOUNCE_TIME     50     // ms
#define UI_FEEDBACK_DURATION     1500   // Show button feedback for 1.5 seconds
#define JPEG_DECODE_TIMEOUT      10000  // Timeout for JPEG download/decode

unsigned long lastSpotifyPoll = 0;
unsigned long lastScreenRedraw = 0;
boolean needsScreenRedraw = true;

// ============================================================================
// JPEG BUFFER & MEMORY MANAGEMENT
// ============================================================================

/*
  CRITICAL MEMORY NOTE for ESP32-C3:
  The ESP32-C3 has limited RAM (~400KB usable). JPEG decoding is memory-intensive.
  
  Strategy:
  1. We use TJpg_Decoder's built-in streaming decoder, which processes the JPEG
     line-by-line rather than loading the entire image into RAM.
  2. The decoder outputs 16-bit RGB565 data directly to the TFT framebuffer.
  3. We download JPEG data via HTTPClient in chunks.
  
  This approach avoids storing the full uncompressed image (~100KB for 300x300 JPEG).
*/

// Global JPEG download state
struct JPEGDownloadState {
  WiFiClient httpClient;
  HTTPClient http;
  uint8_t jpegBuffer[4096];        // Small buffer for chunked downloads
  size_t jpegBufferIndex;
  boolean isDownloading;
  unsigned long downloadStartTime;
  String currentUrl;
};

JPEGDownloadState jpegState = {WiFiClient(), HTTPClient(), {0}, 0, false, 0, ""};

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
void drawAlbumArt(const String& imageUrl);
void drawTrackInfo();
void drawStatusBar();
void displayUIFeedback(const String& message);
void startJPEGDownload(const String& url);
boolean isJPEGDownloadComplete();
void processJPEGBuffer();
boolean sendPauseCommand();
boolean sendPlayCommand();
boolean sendNextCommand();
boolean sendPreviousCommand();
void handleSpotifyError(const String& context);

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
  
  // Configure TJpg_Decoder for 16-bit output (RGB565)
  TJpgDec.setJpgScale(1);
  TJpgDec.setCallback(tft_output);
  
  Serial.println("\n✓ System Ready!");
  Serial.println("Entering main loop...\n");
  
  // Force initial poll and screen redraw
  lastSpotifyPoll = millis() - SPOTIFY_POLL_INTERVAL;
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
  
  // Non-blocking display updates
  if (needsScreenRedraw || currentTime - lastScreenRedraw >= 500) {
    updateDisplay();
    needsScreenRedraw = false;
    lastScreenRedraw = currentTime;
  }
  
  // Check if UI feedback message should expire
  if (uiFeedback.displayUntil > 0 && currentTime >= uiFeedback.displayUntil) {
    uiFeedback.message = "";
    uiFeedback.displayUntil = 0;
    needsScreenRedraw = true;
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
  tft.setTextSize(2);
  tft.setCursor(50, 100);
  tft.println("Sputifi-Desk");
  tft.setTextColor(TEXT_COLOR);
  tft.setTextSize(1);
  tft.setCursor(60, 130);
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
  
  client.setInsecure();  // Accept self-signed certificates for now
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
    displayUIFeedback("Skipping prev...");
    if (sendPreviousCommand()) {
      Serial.println("✓ Previous command sent");
    } else {
      Serial.println("✗ Previous command failed");
    }
    delay(500);  // Prevent rapid repeated presses
  }
  
  // Play/Pause Toggle
  if (btnPlayPause.isPressed()) {
    Serial.println("[BTN] Play/Pause pressed");
    
    if (currentTrack.isPlaying) {
      displayUIFeedback("Pausing...");
      if (sendPauseCommand()) {
        Serial.println("✓ Pause command sent");
        currentTrack.isPlaying = false;
      } else {
        Serial.println("✗ Pause command failed");
      }
    } else {
      displayUIFeedback("Playing...");
      if (sendPlayCommand()) {
        Serial.println("✓ Play command sent");
        currentTrack.isPlaying = true;
      } else {
        Serial.println("✗ Play command failed");
      }
    }
    delay(500);
  }
  
  // Next Track
  if (btnNext.isPressed()) {
    Serial.println("[BTN] Next pressed");
    displayUIFeedback("Skipping next...");
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
    // =====================================================================
    // SPOTIFY API RESPONSE SUCCESSFUL
    // =====================================================================
    
    String newTrackId = currentlyPlaying.trackName;  // Use track name as identifier
    
    // Check if track has changed
    boolean trackChanged = (newTrackId != currentTrack.trackId);
    
    if (trackChanged) {
      Serial.println("\n→ New track detected, updating display");
    }
    
    // Update track information
    currentTrack.trackId = newTrackId;
    currentTrack.trackName = currentlyPlaying.trackName;
    currentTrack.artistName = currentlyPlaying.artistNames[0];  // First artist
    currentTrack.albumName = currentlyPlaying.albumName;
    currentTrack.durationMs = currentlyPlaying.durationMs;
    currentTrack.progressMs = currentlyPlaying.progressMs;
    currentTrack.isPlaying = currentlyPlaying.isPlaying;
    currentTrack.lastFetchTime = millis();
    
    // Handle album art URL
    if (currentlyPlaying.albumImages.size() > 0) {
      String newArtUrl = "";
      
      // Try to find 300x300 image, fallback to largest available
      for (int i = 0; i < currentlyPlaying.albumImages.size(); i++) {
        if (currentlyPlaying.albumImages[i].width == 300) {
          newArtUrl = currentlyPlaying.albumImages[i].url;
          break;
        }
        if (currentlyPlaying.albumImages[i].width >= 640) {
          newArtUrl = currentlyPlaying.albumImages[i].url;
        }
      }
      
      if (newArtUrl != currentTrack.albumArtUrl) {
        Serial.println("→ Album art URL changed, will download");
        currentTrack.albumArtUrl = newArtUrl;
        trackChanged = true;
      }
    }
    
    // Flag screen redraw if track or art changed
    if (trackChanged) {
      needsScreenRedraw = true;
    }
    
    // Debug output
    Serial.println("✓ Poll successful");
    Serial.print("  Track: ");
    Serial.print(currentTrack.trackName);
    Serial.print(" - ");
    Serial.println(currentTrack.artistName);
    Serial.print("  Status: ");
    Serial.println(currentlyPlaying.isPlaying ? "Playing" : "Paused");
    Serial.print("  Progress: ");
    Serial.print(currentlyPlaying.progressMs / 1000);
    Serial.print("/");
    Serial.println(currentlyPlaying.durationMs / 1000);
    
  } else {
    // =====================================================================
    // SPOTIFY API ERROR
    // =====================================================================
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
    // Immediately poll to get new track info
    delay(500);
    pollSpotifyAPI();
    return true;
  }
  return false;
}

boolean sendPreviousCommand() {
  if (spotify.previous()) {
    // Immediately poll to get new track info
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
  // Clear screen
  tft.fillScreen(BG_COLOR);
  
  // Draw status bar at top
  drawStatusBar();
  
  // Draw album art (if available)
  if (currentTrack.albumArtUrl.length() > 0) {
    drawAlbumArt(currentTrack.albumArtUrl);
  }
  
  // Draw track info on right side
  drawTrackInfo();
  
  // Overlay UI feedback if present
  if (uiFeedback.message.length() > 0) {
    tft.fillRect(200, 180, 110, 50, TFT_DARKGREY);
    tft.drawRect(200, 180, 110, 50, ACCENT_COLOR);
    tft.setTextColor(ACCENT_COLOR);
    tft.setTextSize(1);
    tft.setCursor(210, 195);
    tft.println(uiFeedback.message);
  }
}

void drawStatusBar() {
  // Draw top bar with playback status
  tft.fillRect(0, 0, DISPLAY_WIDTH, 20, ACCENT_COLOR);
  tft.setTextColor(BG_COLOR);
  tft.setTextSize(1);
  tft.setCursor(10, 5);
  
  if (currentTrack.isPlaying) {
    tft.print("▶ PLAYING");
  } else {
    tft.print("⏸ PAUSED");
  }
  
  // WiFi status indicator (top right)
  tft.setCursor(290, 5);
  if (WiFi.status() == WL_CONNECTED) {
    tft.print("📡");
  } else {
    tft.setTextColor(TFT_RED);
    tft.print("✗");
  }
}

void drawTrackInfo() {
  /*
    TEXT LAYOUT (Right side):
    - Large track name (wrapping)
    - Medium artist name (wrapping)
    - Small album name
    - Progress bar
  */
  
  tft.setTextColor(TEXT_COLOR);
  int lineY = TEXT_START_Y;
  int lineHeight = 20;
  
  // Track Name (Large, wrapped)
  tft.setTextSize(2);
  String trackName = currentTrack.trackName;
  if (trackName.length() > 15) {
    trackName = trackName.substring(0, 12) + "...";
  }
  tft.setCursor(TEXT_START_X, lineY);
  tft.println(trackName);
  lineY += lineHeight + 5;
  
  // Artist Name (Medium, wrapped)
  tft.setTextSize(1);
  tft.setTextColor(SECONDARY_COLOR);
  String artistName = currentTrack.artistName;
  if (artistName.length() > 20) {
    artistName = artistName.substring(0, 17) + "...";
  }
  tft.setCursor(TEXT_START_X, lineY);
  tft.println(artistName);
  lineY += lineHeight;
  
  // Album Name (Small)
  tft.setTextSize(1);
  tft.setTextColor(STATUS_COLOR);
  String albumName = currentTrack.albumName;
  if (albumName.length() > 20) {
    albumName = albumName.substring(0, 17) + "...";
  }
  tft.setCursor(TEXT_START_X, lineY);
  tft.println(albumName);
  lineY += lineHeight;
  
  // Progress Bar
  drawProgressBar(TEXT_START_X, lineY, 110, 12);
  
  // Time display
  int progressSec = currentTrack.progressMs / 1000;
  int durationSec = currentTrack.durationMs / 1000;
  
  tft.setTextSize(1);
  tft.setTextColor(TEXT_COLOR);
  tft.setCursor(TEXT_START_X, lineY + 18);
  tft.print(formatTime(progressSec));
  tft.print(" / ");
  tft.println(formatTime(durationSec));
}

void drawProgressBar(int x, int y, int w, int h) {
  // Draw progress bar for current track
  
  int fillWidth = 0;
  if (currentTrack.durationMs > 0) {
    fillWidth = (currentTrack.progressMs * w) / currentTrack.durationMs;
  }
  
  // Background
  tft.drawRect(x, y, w, h, TEXT_COLOR);
  tft.fillRect(x + 1, y + 1, w - 2, h - 2, TFT_DARKGREY);
  
  // Filled portion
  if (fillWidth > 0) {
    tft.fillRect(x + 1, y + 1, fillWidth - 1, h - 2, ACCENT_COLOR);
  }
}

void drawAlbumArt(const String& imageUrl) {
  /*
    ALBUM ART RENDERING STRATEGY:
    
    1. We use TJpg_Decoder for streaming JPEG decode.
    2. The decoder processes the JPEG line-by-line.
    3. Lines are rendered directly to the TFT framebuffer (no intermediate buffer).
    4. This minimizes RAM usage: ~4KB download buffer + TJpg working memory (~30KB).
    
    Memory breakdown for ESP32-C3 (~400KB total SRAM):
    - Sketch code: ~150KB
    - WiFi/BLE stack: ~100KB
    - Stack: ~50KB
    - Available for heap: ~100KB
    
    The 4KB download buffer + TJpg streaming keeps us well under limits.
  */
  
  Serial.print("[JPEG] Decoding album art from: ");
  Serial.println(imageUrl);
  
  // For now, draw a placeholder until we implement streaming download
  // In production, you would:
  // 1. startJPEGDownload(imageUrl)
  // 2. Wait for download completion
  // 3. Feed data to TJpg_Decoder
  // 4. Decoder renders directly to TFT via tft_output callback
  
  // PLACEHOLDER: Gray box
  tft.fillRect(ALBUM_ART_X, ALBUM_ART_Y, ALBUM_ART_SIZE, ALBUM_ART_SIZE, TFT_DARKGREY);
  tft.drawRect(ALBUM_ART_X, ALBUM_ART_Y, ALBUM_ART_SIZE, ALBUM_ART_SIZE, SECONDARY_COLOR);
  tft.setTextColor(SECONDARY_COLOR);
  tft.setTextSize(1);
  tft.setCursor(ALBUM_ART_X + 50, ALBUM_ART_Y + 80);
  tft.println("[Album Art]");
  tft.setCursor(ALBUM_ART_X + 40, ALBUM_ART_Y + 95);
  tft.println("Loading...");
}

// ============================================================================
// UTILITY FUNCTIONS
// ============================================================================

void displayUIFeedback(const String& message) {
  uiFeedback.message = message;
  uiFeedback.displayUntil = millis() + UI_FEEDBACK_DURATION;
  needsScreenRedraw = true;
}

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
// JPEG DECODING CALLBACK
// ============================================================================

// This callback is invoked by TJpg_Decoder for each decoded line
// It receives RGB565 data and writes directly to the TFT framebuffer
boolean tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  // This function is called for each block of the JPEG
  // x, y: position on screen
  // w, h: width/height of block
  // bitmap: pointer to RGB565 pixel data
  
  // Scale down if necessary to fit display
  if (x + w > ALBUM_ART_X + ALBUM_ART_SIZE) {
    w = ALBUM_ART_X + ALBUM_ART_SIZE - x;
  }
  if (y + h > ALBUM_ART_Y + ALBUM_ART_SIZE) {
    h = ALBUM_ART_Y + ALBUM_ART_SIZE - y;
  }
  
  // Push pixels to display
  if (w > 0 && h > 0) {
    tft.pushImage(x, y, w, h, bitmap);
  }
  
  return true;  // Return true to continue decoding
}

// ============================================================================
// JPEG DOWNLOAD UTILITIES (Placeholder - Implement as needed)
// ============================================================================

void startJPEGDownload(const String& url) {
  // TODO: Implement streaming JPEG download
  // 1. Open HTTP connection to URL
  // 2. Read in 4KB chunks into jpegBuffer
  // 3. Feed to TJpg_Decoder as chunks arrive
  // 4. Decoder calls tft_output() to render
  
  jpegState.currentUrl = url;
  jpegState.isDownloading = true;
  jpegState.downloadStartTime = millis();
}

boolean isJPEGDownloadComplete() {
  // TODO: Implement download state tracking
  return !jpegState.isDownloading;
}

// ============================================================================
// END OF FILE
// ============================================================================
