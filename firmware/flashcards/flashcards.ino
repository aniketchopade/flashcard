// Flashcards for the Elecrow CrowPanel ESP32 5.79" e-paper (792x272).
//
// Downloads index.json + section files from github.com/aniketchopade/flashcard.
// Copy Elecrow's driver files (EPD.h, EPD.cpp, EPD_Init.h, EPD_Init.cpp,
// EPDfont.h, spi.h, spi.cpp) from any 5.79 example into this folder.
// Needs the ArduinoJson library (Benoit Blanchon, v7).
//
// Controls
//   Menus : dial UP/DOWN = move    dial PRESS = select    EXIT = back
//   Study : MENU = show answer     EXIT = next card       dial PRESS = menu (+update)

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "EPD.h"

const char* WIFI_SSID = "YourWiFiName";
const char* WIFI_PASS = "YourPassword";
// GitHub API always returns the latest file (raw.githubusercontent is cached).
const char* API_BASE =
  "https://api.github.com/repos/aniketchopade/flashcard/contents/";

// Buttons (from Elecrow's 5.79_key example)
const int BTN_MENU = 2;   // reveal answer
const int BTN_EXIT = 1;   // next card / back
const int BTN_UP   = 6;   // dial up
const int BTN_DOWN = 4;   // dial down
const int BTN_OK   = 5;   // dial press

const int FULL_REFRESH_EVERY = 10;
const int MENU_ROWS = 9;
const int MAX_ITEMS = 60;

// ---------- State ----------
enum Mode { SUBJECTS, SECTIONS, STUDY };
Mode mode = SUBJECTS;

uint8_t ImageBW[27200];   // frame we draw into
uint8_t OldBW[27200];     // what is on screen now
int drawsSinceFull = 0;

JsonDocument indexDoc;    // index.json
JsonDocument deck;        // {"cards":[{"q","a","label"}]}
int cardCount = 0;
int current = 0;
bool showingAnswer = false;

int subjSel = 0;          // highlighted subject
int secSel = 0;           // highlighted section (0 = All)
String items[MAX_ITEMS];
int itemCount = 0;

Preferences prefs;        // remembers the last selection across reboots

// ================= Display =================

// Write the "old image" RAM so partial refresh knows what to erase.
void writeOldFrame(const uint8_t* img) {
  uint32_t i, col = 0, line = 0;
  EPD_SetRAMMP();
  EPD_SetRAMMA();
  EPD_WR_REG(0x26);
  for (i = 0; i < ALLSCREEN_BYTES; i++) {
    EPD_WR_DATA8(img[line * Source_BYTES * 2 + col]);
    if (++line >= Gate_BITS) { col++; line = 0; }
  }
  EPD_SetRAMSP();
  EPD_SetRAMSA();
  EPD_WR_REG(0xA6);
  for (i = 0; i < ALLSCREEN_BYTES; i++) {
    EPD_WR_DATA8(img[line * Source_BYTES * 2 + col]);
    if (++line >= Gate_BITS) { col++; line = 0; }
  }
}

void fullClear() {
  EPD_FastMode1Init();
  EPD_Display_Clear();
  EPD_Update();
  EPD_DeepSleep();
  memset(OldBW, 0xFF, sizeof(OldBW));
  drawsSinceFull = 0;
}

void showFrame() {
  if (drawsSinceFull >= FULL_REFRESH_EVERY) fullClear();
  EPD_FastMode1Init();
  writeOldFrame(OldBW);
  EPD_Display(ImageBW);
  EPD_PartUpdate();
  EPD_DeepSleep();
  memcpy(OldBW, ImageBW, sizeof(OldBW));
  drawsSinceFull++;
}

int drawWrapped(int x, int y, const char* text, int fontSize,
                int maxChars, int lineGap) {
  char line[120];
  const char* p = text;
  while (*p) {
    int len = strlen(p);
    int take = len;
    if (len > maxChars) {
      take = maxChars;
      while (take > 0 && p[take] != ' ') take--;
      if (take == 0) take = maxChars;
    }
    memcpy(line, p, take);
    line[take] = '\0';
    EPD_ShowString(x, y, line, fontSize, BLACK);
    y += fontSize + lineGap;
    p += take;
    while (*p == ' ') p++;
  }
  return y;
}

void drawMessage(const char* msg) {
  Paint_Clear(WHITE);
  EPD_ShowString(10, 20, msg, 24, BLACK);
  showFrame();
}

// Generic list menu: title, items[], highlighted row, hint at the bottom.
void drawMenu(const char* title, int sel, const char* hint) {
  Paint_Clear(WHITE);
  EPD_ShowString(10, 4, title, 16, BLACK);
  EPD_DrawLine(0, 24, 792, 24, BLACK);

  int first = (sel < MENU_ROWS) ? 0 : sel - MENU_ROWS + 1;
  for (int r = 0; r < MENU_ROWS && first + r < itemCount; r++) {
    int i = first + r;
    int y = 30 + r * 24;
    if (i == sel) EPD_ShowString(10, y, ">", 16, BLACK);
    EPD_ShowString(30, y, items[i].c_str(), 16, BLACK);
  }
  if (first > 0) EPD_ShowString(770, 30, "^", 16, BLACK);
  if (first + MENU_ROWS < itemCount) EPD_ShowString(770, 222, "v", 16, BLACK);

  EPD_ShowString(10, 256, hint, 12, BLACK);
  showFrame();
}

void drawCard() {
  JsonObject c = deck["cards"][current];
  const char* label = c["label"] | "";
  const char* q = c["q"] | "";
  const char* a = c["a"] | "";

  Paint_Clear(WHITE);
  char header[90];
  snprintf(header, sizeof(header), "%s   (%d/%d)", label, current + 1, cardCount);
  EPD_ShowString(10, 6, header, 12, BLACK);
  EPD_DrawLine(0, 24, 792, 24, BLACK);

  int y = drawWrapped(10, 36, q, 24, 60, 6);
  if (showingAnswer) {
    EPD_DrawLine(0, y + 4, 792, y + 4, BLACK);
    drawWrapped(10, y + 14, a, 16, 90, 4);
  } else {
    EPD_ShowString(10, 256, "MENU = answer   EXIT = next   DIAL press = sections",
                   12, BLACK);
  }
  showFrame();
}

// ================= Network =================

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  for (int i = 0; i < 30 && WiFi.status() != WL_CONNECTED; i++) delay(500);
  return WiFi.status() == WL_CONNECTED;
}

void wifiOff() {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
}

// GET <API_BASE><path> and parse JSON into `out`.
bool fetchJson(const char* path, JsonDocument& out) {
  WiFiClientSecure client;
  client.setInsecure();                    // learning only: skips cert check
  HTTPClient http;
  http.begin(client, String(API_BASE) + path);
  http.addHeader("Accept", "application/vnd.github.raw");
  http.addHeader("User-Agent", "crowpanel-flashcards");
  int code = http.GET();
  if (code != 200) {
    Serial.printf("GET %s -> HTTP %d\n", path, code);
    http.end();
    return false;
  }
  DeserializationError err = deserializeJson(out, http.getStream());
  http.end();
  if (err) Serial.printf("GET %s -> %s\n", path, err.c_str());
  return !err;
}

bool fetchIndex() {
  JsonDocument fresh;
  if (!connectWiFi() || !fetchJson("index.json", fresh)) return false;
  if (fresh["subjects"].size() == 0) return false;
  indexDoc = fresh;
  return true;
}

// Load one section (secChoice >= 1) or all sections (secChoice == 0) of a subject.
bool loadCards(int subj, int secChoice) {
  JsonArray secs = indexDoc["subjects"][subj]["sections"];
  JsonDocument fresh;
  JsonArray out = fresh["cards"].to<JsonArray>();

  int from = (secChoice == 0) ? 0 : secChoice - 1;
  int to   = (secChoice == 0) ? secs.size() : secChoice;
  for (int s = from; s < to; s++) {
    JsonDocument file;
    if (!fetchJson(secs[s]["file"].as<const char*>(), file)) return false;
    String label = String((const char*)file["section"]) + " " +
                   (const char*)file["title"];
    for (JsonObject c : file["cards"].as<JsonArray>()) {
      JsonObject n = out.add<JsonObject>();
      n["q"] = c["q"].as<String>();
      n["a"] = c["a"].as<String>();
      n["label"] = label;
    }
  }
  if (out.size() == 0) return false;
  deck = fresh;
  cardCount = deck["cards"].size();
  return true;
}

// ================= Screens =================

void showSubjects() {
  mode = SUBJECTS;
  JsonArray subs = indexDoc["subjects"];
  itemCount = 0;
  for (JsonObject s : subs) {
    if (itemCount >= MAX_ITEMS) break;
    int total = 0;
    for (JsonObject sec : s["sections"].as<JsonArray>()) total += sec["count"].as<int>();
    items[itemCount++] = String((const char*)s["name"]) + "  (" +
                         s["sections"].size() + " sections, " + total + " cards)";
  }
  if (subjSel >= itemCount) subjSel = 0;
  drawMenu("Choose subject", subjSel, "DIAL up/down = move   DIAL press = open");
}

void showSections() {
  mode = SECTIONS;
  JsonObject s = indexDoc["subjects"][subjSel];
  JsonArray secs = s["sections"];
  int total = 0;
  for (JsonObject sec : secs) total += sec["count"].as<int>();

  itemCount = 0;
  items[itemCount++] = String("All sections  (") + total + " cards)";
  for (JsonObject sec : secs) {
    if (itemCount >= MAX_ITEMS) break;
    items[itemCount++] = String((const char*)sec["section"]) + "  " +
                         (const char*)sec["title"] + "  (" + sec["count"].as<int>() + ")";
  }
  if (secSel >= itemCount) secSel = 0;
  String title = String((const char*)s["name"]) + " - choose section";
  drawMenu(title.c_str(), secSel, "DIAL up/down = move   DIAL press = study   EXIT = subjects");
}

void startStudy() {
  drawMessage("Loading cards...");
  if (!loadCards(subjSel, secSel)) {
    drawMessage("Could not load cards - try again");
    delay(1500);
    showSections();
    return;
  }
  wifiOff();

  // Remember the choice by name, so it survives new sections being added.
  JsonObject s = indexDoc["subjects"][subjSel];
  prefs.putString("subject", (const char*)s["name"]);
  prefs.putString("section",
                  secSel == 0 ? "all" : (const char*)s["sections"][secSel - 1]["section"]);

  mode = STUDY;
  showingAnswer = false;
  current = esp_random() % cardCount;
  drawCard();
}

// Find the saved subject/section in the current index. Returns false if gone.
bool restoreSelection() {
  String subj = prefs.getString("subject", "");
  String sec  = prefs.getString("section", "");
  if (subj == "") return false;
  JsonArray subs = indexDoc["subjects"];
  for (int i = 0; i < (int)subs.size(); i++) {
    if (subj != (const char*)subs[i]["name"]) continue;
    subjSel = i;
    if (sec == "all") { secSel = 0; return true; }
    JsonArray secs = subs[i]["sections"];
    for (int j = 0; j < (int)secs.size(); j++) {
      if (sec == (const char*)secs[j]["section"]) { secSel = j + 1; return true; }
    }
  }
  return false;
}

// ================= Buttons =================

bool pressed(int pin) {
  if (digitalRead(pin) != LOW) return false;
  delay(30);
  while (digitalRead(pin) == LOW) delay(10);
  delay(30);
  return true;
}

void setup() {
  Serial.begin(115200);
  for (int pin : {BTN_MENU, BTN_EXIT, BTN_UP, BTN_DOWN, BTN_OK}) pinMode(pin, INPUT_PULLUP);
  prefs.begin("flashcards", false);

  pinMode(7, OUTPUT);
  digitalWrite(7, HIGH);
  EPD_GPIOInit();
  Paint_NewImage(ImageBW, EPD_W, EPD_H, Rotation, WHITE);
  Paint_Clear(WHITE);
  fullClear();

  drawMessage("Connecting...");
  if (!fetchIndex()) {
    drawMessage("Could not reach GitHub - press RST to retry");
    return;
  }
  if (restoreSelection()) startStudy();   // pick up where we left off
  else showSubjects();
}

void loop() {
  if (indexDoc["subjects"].size() == 0) { delay(50); return; }

  if (mode == SUBJECTS || mode == SECTIONS) {
    int& sel = (mode == SUBJECTS) ? subjSel : secSel;
    if (pressed(BTN_DOWN) && sel < itemCount - 1) { sel++; mode == SUBJECTS ? showSubjects() : showSections(); }
    if (pressed(BTN_UP) && sel > 0)               { sel--; mode == SUBJECTS ? showSubjects() : showSections(); }
    if (pressed(BTN_OK)) {
      if (mode == SUBJECTS) { secSel = 0; showSections(); }
      else startStudy();
    }
    if (pressed(BTN_EXIT)) {
      if (mode == SECTIONS) showSubjects();
      else if (cardCount > 0) { wifiOff(); mode = STUDY; drawCard(); }
    }
  } else {  // STUDY
    if (pressed(BTN_MENU) && !showingAnswer) {
      showingAnswer = true;
      drawCard();
    }
    if (pressed(BTN_EXIT)) {
      int next = current;
      if (cardCount > 1) while (next == current) next = esp_random() % cardCount;
      current = next;
      showingAnswer = false;
      drawCard();
    }
    if (pressed(BTN_OK)) {                 // back to sections, with a fresh index
      drawMessage("Checking for new sections...");
      if (!fetchIndex()) {
        drawMessage("Update failed - showing saved list");
        delay(1200);
      }
      restoreSelection();
      showSections();
    }
  }
  delay(20);
}
