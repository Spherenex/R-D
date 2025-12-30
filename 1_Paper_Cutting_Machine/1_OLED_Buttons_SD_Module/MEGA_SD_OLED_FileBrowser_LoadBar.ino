/*
  File: MEGA_SD_OLED_FileBrowser_LoadBar.ino

  Board: Arduino Mega 2560
  OLED : SSD1306 128x64 I2C (Addr 0x3C)
  SD   : SPI SD module (SD.h)

  Buttons (to GND, INPUT_PULLUP):
    D2 = Scroll (next)
    D3 = Enter (open folder / select file)
    D4 = Back  (go parent)

  UI:
    - 5 sec splash with loading bar
    - During splash: init SD + scan root directory
    - After 5 sec: show SD contents
    - Show only 5 items to avoid half-cut lines
    - Inverted selection bar
*/

#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------------- OLED ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------------- SD ----------------
const uint8_t SD_CS = 10;

// ---------------- Buttons ----------------
const uint8_t BTN_SCROLL = 2;
const uint8_t BTN_ENTER  = 3;
const uint8_t BTN_BACK   = 4;
const unsigned long DEBOUNCE_MS = 120;

// ---------------- Browser Settings ----------------
const int VISIBLE_ITEMS = 5;     // ✅ fully visible (no cut)
const int MAX_ENTRIES   = 50;
const bool SHOW_ONLY_STL = false; // optional filter

// ---------------- Data Structures ----------------
struct Entry {
  char name[32];
  bool isDir;
};

Entry entries[MAX_ENTRIES];
int entryCount = 0;

int selectionIndex = 0;
int scrollOffset   = 0;

char currentPath[80] = "/";

// ---------------- Button states ----------------
bool lastScrollState = HIGH;
bool lastEnterState  = HIGH;
bool lastBackState   = HIGH;

unsigned long lastScrollTime = 0;
unsigned long lastEnterTime  = 0;
unsigned long lastBackTime   = 0;

// ---------------- Helpers ----------------
bool endsWithSTL(const char* s) {
  int len = strlen(s);
  if (len < 4) return false;
  const char* ext = s + (len - 4);
  return ( (ext[0] == '.') &&
           (ext[1] == 's' || ext[1] == 'S') &&
           (ext[2] == 't' || ext[2] == 'T') &&
           (ext[3] == 'l' || ext[3] == 'L') );
}

bool buttonPressed(uint8_t pin, bool &lastState, unsigned long &lastTime) {
  bool now = digitalRead(pin); // INPUT_PULLUP: HIGH release, LOW press

  if (now != lastState) {
    lastTime = millis();
    lastState = now;
  }

  if (now == LOW && (millis() - lastTime) > DEBOUNCE_MS) {
    while (digitalRead(pin) == LOW) { delay(5); } // wait release
    lastState = HIGH;
    lastTime = millis();
    return true;
  }
  return false;
}

// ✅ Keeps selection fully visible and when going down pushes it to 5th line
void clampScroll() {
  if (entryCount <= 0) {
    selectionIndex = 0;
    scrollOffset = 0;
    return;
  }

  if (selectionIndex < 0) selectionIndex = 0;
  if (selectionIndex > entryCount - 1) selectionIndex = entryCount - 1;

  if (selectionIndex < scrollOffset) scrollOffset = selectionIndex;

  if (selectionIndex >= scrollOffset + VISIBLE_ITEMS) {
    scrollOffset = selectionIndex - (VISIBLE_ITEMS - 1);
  }

  if (scrollOffset < 0) scrollOffset = 0;

  int maxOffset = entryCount - VISIBLE_ITEMS;
  if (maxOffset < 0) maxOffset = 0;
  if (scrollOffset > maxOffset) scrollOffset = maxOffset;
}

void drawUI() {
  display.clearDisplay();

  // Header
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.setTextColor(SSD1306_WHITE);
  display.print("SD:");
  display.print(currentPath);

  // Separator
  display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

  // List (5 fully visible lines)
  int y = 12;
  for (int row = 0; row < VISIBLE_ITEMS; row++) {
    int idx = scrollOffset + row;
    if (idx >= entryCount) break;

    bool selected = (idx == selectionIndex);

    if (selected) {
      display.fillRect(0, y - 1, 128, 9, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }

    display.setCursor(2, y);

    if (entries[idx].isDir) {
      display.print("[");
      display.print(entries[idx].name);
      display.print("]");
    } else {
      display.print(entries[idx].name);
    }

    y += 8;
  }

  display.display();
}

bool loadDirectory(const char* path) {
  entryCount = 0;
  selectionIndex = 0;
  scrollOffset = 0;

  File dir = SD.open(path);
  if (!dir) return false;
  if (!dir.isDirectory()) {
    dir.close();
    return false;
  }

  File f = dir.openNextFile();
  while (f && entryCount < MAX_ENTRIES) {
    const char* nm = f.name();
    bool isDir = f.isDirectory();

    bool keep = true;
    if (!isDir && SHOW_ONLY_STL) keep = endsWithSTL(nm);

    if (keep) {
      strncpy(entries[entryCount].name, nm, sizeof(entries[entryCount].name) - 1);
      entries[entryCount].name[sizeof(entries[entryCount].name) - 1] = '\0';
      entries[entryCount].isDir = isDir;
      entryCount++;
    }

    f.close();
    f = dir.openNextFile();
  }

  dir.close();
  clampScroll();
  return true;
}

void pathJoin(char* out, size_t outSize, const char* base, const char* child) {
  if (strcmp(base, "/") == 0) snprintf(out, outSize, "/%s", child);
  else snprintf(out, outSize, "%s/%s", base, child);
}

void goParent(char* path) {
  if (strcmp(path, "/") == 0) return;

  int len = strlen(path);
  if (len > 1 && path[len - 1] == '/') {
    path[len - 1] = '\0';
    len--;
  }

  char* last = strrchr(path, '/');
  if (!last) { strcpy(path, "/"); return; }

  if (last == path) path[1] = '\0';
  else *last = '\0';
}

// ---------------- Actions ----------------
void onScroll() {
  if (entryCount <= 0) return;

  selectionIndex++;
  if (selectionIndex >= entryCount) selectionIndex = 0;
  clampScroll();
  drawUI();
}

void onEnter() {
  if (entryCount <= 0) return;

  if (entries[selectionIndex].isDir) {
    char newPath[80];
    pathJoin(newPath, sizeof(newPath), currentPath, entries[selectionIndex].name);

    if (loadDirectory(newPath)) {
      strncpy(currentPath, newPath, sizeof(currentPath) - 1);
      currentPath[sizeof(currentPath) - 1] = '\0';
      drawUI();
    } else {
      // brief error screen
      display.clearDisplay();
      display.setTextColor(SSD1306_WHITE);
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println("Open folder failed");
      display.setCursor(0, 16);
      display.println(entries[selectionIndex].name);
      display.display();
      delay(800);
      drawUI();
    }
  } else {
    // file selected
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Selected file:");
    display.setCursor(0, 16);
    display.println(entries[selectionIndex].name);
    display.display();
    delay(2000);
    drawUI();
  }
}

void onBack() {
  if (strcmp(currentPath, "/") == 0) return;

  char parent[80];
  strncpy(parent, currentPath, sizeof(parent) - 1);
  parent[sizeof(parent) - 1] = '\0';

  goParent(parent);

  if (loadDirectory(parent)) {
    strncpy(currentPath, parent, sizeof(currentPath) - 1);
    currentPath[sizeof(currentPath) - 1] = '\0';
    drawUI();
  } else {
    strcpy(currentPath, "/");
    loadDirectory("/");
    drawUI();
  }
}

// ---------------- Splash with Loading Bar ----------------
void splashWithLoadingBar() {
  const unsigned long TOTAL_MS = 5000;
  const unsigned long STEP_MS  = 50;   // 100 steps ~ 5 sec
  const int barX = 8;
  const int barY = 54;
  const int barW = 112;
  const int barH = 8;

  unsigned long start = millis();

  bool sdOk = false;
  bool scanned = false;

  while (millis() - start < TOTAL_MS) {
    unsigned long elapsed = millis() - start;
    float p = (float)elapsed / (float)TOTAL_MS;
    if (p > 1.0f) p = 1.0f;

    // Try SD init early
    if (!sdOk) {
      // Mega SPI stability
      pinMode(53, OUTPUT);
      pinMode(SD_CS, OUTPUT);
      SPI.begin();
      sdOk = SD.begin(SD_CS);
    }

    // Scan root once SD is OK
    if (sdOk && !scanned) {
      strcpy(currentPath, "/");
      loadDirectory(currentPath);
      scanned = true;
    }

    // Draw splash + bar
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(0, 10);
    display.println("Spherenex");

    display.setTextSize(1);
    display.setCursor(0, 36);
    display.println("Shape You Feature");

    // status text
    display.setCursor(0, 46);
    if (!sdOk) display.print("Loading SD...");
    else if (!scanned) display.print("Scanning...");
    else display.print("Ready");

    // progress bar outline
    display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);

    // progress fill
    int fillW = (int)((barW - 2) * p);
    if (fillW < 0) fillW = 0;
    if (fillW > barW - 2) fillW = barW - 2;
    display.fillRect(barX + 1, barY + 1, fillW, barH - 2, SSD1306_WHITE);

    display.display();

    delay(STEP_MS);
  }

  // After 5 sec, ensure SD + root scan really exists
  if (!SD.begin(SD_CS)) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("SD init FAILED!");
    display.setCursor(0, 16);
    display.println("Check wiring");
    display.display();
    while (1);
  }

  strcpy(currentPath, "/");
  if (!loadDirectory(currentPath)) {
    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println("Root open failed");
    display.display();
    while (1);
  }
}

// ---------------- Setup / Loop ----------------
void setup() {
  pinMode(BTN_SCROLL, INPUT_PULLUP);
  pinMode(BTN_ENTER,  INPUT_PULLUP);
  pinMode(BTN_BACK,   INPUT_PULLUP);

  Wire.begin();
  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (1);
  }

  // 5 sec splash + load bar + SD init + scan
  splashWithLoadingBar();

  // Show SD content immediately
  drawUI();
}

void loop() {
  if (buttonPressed(BTN_SCROLL, lastScrollState, lastScrollTime)) onScroll();
  if (buttonPressed(BTN_ENTER,  lastEnterState,  lastEnterTime))  onEnter();
  if (buttonPressed(BTN_BACK,   lastBackState,   lastBackTime))   onBack();
}
