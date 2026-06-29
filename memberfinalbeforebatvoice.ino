// ============================================================
//  MEMBER NODE — Soldier Safety System
//  Change NODE_ID to "ID1" or "ID2" per device
//  ID1 uses STATUS_INTERVAL 5000, ID2 uses 8000
// ============================================================

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── Node identity ──────────────────────────────────────────
#define NODE_ID          "ID1"    // ← Change to "ID1" for node 1
#define STATUS_INTERVAL   5000    // ID1 = 5000, ID2 = 8000 (ms)

// ── LoRa pins ──────────────────────────────────────────────
#define LORA_SCK   18
#define LORA_MISO  19
#define LORA_MOSI  23
#define LORA_SS     5
#define LORA_RST   14
#define LORA_DIO0  26

// ── Peripherals ────────────────────────────────────────────
#define BTN_HELP      13
#define BTN_INJURED   12
#define BTN_CRITICAL  27
#define BUZZER        25
#define LED           33

// ── Timing ─────────────────────────────────────────────────
#define ACK_TIMEOUT      1500    // ms wait for direct ACK
#define DIRECT_RETRIES      2    // direct TX attempts before mesh
#define MESH_RETRIES        2    // mesh attempts before local alert
#define RELAY_WAIT       2500    // ms wait for relay ACK
#define BTN_DEBOUNCE      400    // ms between button reads

// ── Display (textSize 2, 128x64 OLED) ──────────────────────
// textSize 2: 12px wide × 16px tall per char
// 128px / 12px = 10 chars visible, 3 lines at y=0,21,42
#define MAX_VISIBLE   10
#define LINE_H        21
#define SCROLL_DELAY  130
#define SCROLL_PAUSE  900

Adafruit_SSD1306 display(128, 64, &Wire, -1);

struct Scroller {
  String        text;
  int           offset;
  bool          scrolling;
  bool          paused;
  unsigned long lastStep;
  unsigned long pauseStart;
};
Scroller lines[3];

// ── App state ──────────────────────────────────────────────
unsigned long lastPressTime  = 0;
unsigned long lastStatusTime = 0;
int           leaderRSSI     = -120;

// ── Forward declarations ───────────────────────────────────
bool sendWithACK(String payload);
bool sendViaMesh(String payload);
void handleIncoming(String msg);
void setDisplay(String l1, String l2, String l3);
void tickDisplay();
void drawLine(int i, int y);
void alertLocal(int times, int onMs, int offMs);
void triggerSOS(String msg);
void listenForAlerts(unsigned long durationMs);

// =============================================================
void setup() {
  Serial.begin(115200);

  pinMode(BTN_HELP,     INPUT_PULLUP);
  pinMode(BTN_INJURED,  INPUT_PULLUP);
  pinMode(BTN_CRITICAL, INPUT_PULLUP);
  pinMode(BUZZER, OUTPUT);
  pinMode(LED,    OUTPUT);
  digitalWrite(BUZZER, HIGH);   // active-low: HIGH = off
  digitalWrite(LED,    LOW);

  Wire.begin(21, 22);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(WHITE);
  display.setTextSize(2);
  setDisplay(NODE_ID, "READY", "");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_SS);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  LoRa.begin(433E6);

  randomSeed(analogRead(0));
  delay(random(200, 1000));
}

// =============================================================
//  loop — FIXED STRUCTURE
//
//  Key fix: the debounce check NO LONGER uses return.
//  Previously "if(debounce) return" was blocking LoRa.parsePacket()
//  from running whenever the debounce timer was active.
//  Now packet receive and button read are fully independent.
// =============================================================
void loop() {

  // ── 1. Always tick display (non-blocking) ─────────────────
  tickDisplay();

  // ── 2. Always check for incoming packets ──────────────────
  //  This MUST run every loop with no conditions blocking it.
  //  Previously the debounce return was cutting this off.
  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String msg = "";
    while (LoRa.available()) msg += (char)LoRa.read();
    handleIncoming(msg);
  }

  // ── 3. Periodic STATUS heartbeat ──────────────────────────
  if (millis() - lastStatusTime > STATUS_INTERVAL &&
      millis() - lastPressTime  > 2000) {
    sendWithACK("STATUS OK");
    lastStatusTime = millis();
  }

  // ── 4. Button read — debounce check does NOT use return ───
  //  Using a plain if block so execution always continues
  //  to the top of loop() next iteration regardless
  if (millis() - lastPressTime >= BTN_DEBOUNCE) {
    if      (digitalRead(BTN_HELP)     == LOW) triggerSOS("NEED HELP");
    else if (digitalRead(BTN_INJURED)  == LOW) triggerSOS("PERSON INJURED");
    else if (digitalRead(BTN_CRITICAL) == LOW) triggerSOS("CRITICAL CONDITION");
  }
}

// =============================================================
//  listenForAlerts — actively listens for incoming packets
//  for a set duration. Called after SOS is sent so we don't
//  miss ALERT broadcasts that arrive while we were transmitting.
//  This is the core fix for "other member misses the alert".
// =============================================================
void listenForAlerts(unsigned long durationMs) {
  unsigned long start = millis();
  while (millis() - start < durationMs) {
    tickDisplay();
    int ps = LoRa.parsePacket();
    if (ps) {
      String msg = "";
      while (LoRa.available()) msg += (char)LoRa.read();
      handleIncoming(msg);
    }
  }
}

// =============================================================
//  triggerSOS
// =============================================================
void triggerSOS(String msg) {
  lastPressTime  = millis();
  lastStatusTime = millis();

  setDisplay(NODE_ID, msg, "SENDING..");
  Serial.println("[SOS] " + msg);

  bool ok = sendWithACK(msg);

  if (!ok) {
    setDisplay(NODE_ID, msg, "MESH...");
    ok = sendViaMesh(msg);
  }

  if (!ok) {
    setDisplay(NODE_ID, msg, "NO LINK!");
    alertLocal(8, 100, 100);
  }

  // After SOS completes (however it ended), listen for 4 seconds.
  // The leader broadcasts ALERT twice (3s apart).
  // This window catches the second broadcast in case we missed first.
  // Also catches alerts from OTHER soldiers that fired during our TX.
  listenForAlerts(4000);
}

// =============================================================
//  sendWithACK
// =============================================================
bool sendWithACK(String payload) {
  String packet = String(NODE_ID) + ":" + payload;

  for (int attempt = 0; attempt < DIRECT_RETRIES; attempt++) {
    delay(random(50 + attempt * 100, 150 + attempt * 200));

    Serial.println("[TX] Attempt " + String(attempt+1) + ": " + packet);
    LoRa.beginPacket();
    LoRa.print(packet);
    LoRa.endPacket();

    unsigned long start = millis();
    while (millis() - start < ACK_TIMEOUT) {
      tickDisplay();
      int ps = LoRa.parsePacket();
      if (ps) {
        String resp = "";
        while (LoRa.available()) resp += (char)LoRa.read();

        if (resp == "ACK:" + String(NODE_ID)) {
          leaderRSSI = LoRa.packetRssi();
          Serial.println("[ACK] OK. RSSI: " + String(leaderRSSI));
          if (payload != "STATUS OK") {
            setDisplay(NODE_ID, payload, "DELIVRD");
            // Beep AFTER display — never block radio with buzzer mid-flight
            alertLocal(2, 80, 80);
          }
          return true;
        }
        // Any other packet (ALERT from leader) — handle it immediately
        // This is how the sending node catches its own alert echo
        handleIncoming(resp);
      }
    }
    Serial.println("[ACK] Timeout attempt " + String(attempt+1));
  }
  return false;
}

// =============================================================
//  sendViaMesh
// =============================================================
bool sendViaMesh(String payload) {
  // No RSSI embedded here — sender cannot measure its own signal strength.
  // Only the relay node (receiver) can measure RSSI, done in handleIncoming.
  String relayReq = "RELAY:" + String(NODE_ID) + ":" + payload;

  for (int attempt = 0; attempt < MESH_RETRIES; attempt++) {
    delay(random(100, 300));

    Serial.println("[MESH] Broadcasting: " + relayReq);
    LoRa.beginPacket();
    LoRa.print(relayReq);
    LoRa.endPacket();

    unsigned long start = millis();
    while (millis() - start < RELAY_WAIT) {
      tickDisplay();
      int ps = LoRa.parsePacket();
      if (ps) {
        String resp = "";
        while (LoRa.available()) resp += (char)LoRa.read();

        if (resp == "ACK:" + String(NODE_ID)) {
          leaderRSSI = LoRa.packetRssi();
          Serial.println("[MESH ACK] Delivered via relay.");
          setDisplay(NODE_ID, payload, "RELAY OK");
          alertLocal(2, 80, 80);
          return true;
        }
        handleIncoming(resp);
      }
    }
    Serial.println("[MESH] Attempt " + String(attempt+1) + " timed out.");
  }
  return false;
}

// =============================================================
//  handleIncoming
// =============================================================
void handleIncoming(String msg) {
  Serial.println("[RX] " + msg);

  // ── ALERT from leader ─────────────────────────────────────
  // Packet format: "ALERT:ID1:HLP:ID2:15"
  //                        ^    ^   ^   ^
  //                       node type near diff
  if (msg.startsWith("ALERT:")) {
    int p1 = msg.indexOf(':');
    int p2 = msg.indexOf(':', p1+1);
    int p3 = msg.indexOf(':', p2+1);
    int p4 = msg.indexOf(':', p3+1);
    if (p1==-1 || p2==-1 || p3==-1) return;

    String node     = msg.substring(p1+1, p2);
    String type     = msg.substring(p2+1, p3);
    String nearNode = (p4 != -1) ? msg.substring(p3+1, p4) : "";
    String diff     = (p4 != -1) ? msg.substring(p4+1)     : "";
    // strip any trailing mesh suffix from diff (e.g. ":viaID2:...")
    int extraColon  = diff.indexOf(':');
    if (extraColon != -1) diff = diff.substring(0, extraColon);

    if (node == String(NODE_ID)) return;  // ignore echo of our own SOS

    String label = type;
    if      (type=="HLP")  label = "HELP";
    else if (type=="INJ")  label = "INJURED";
    else if (type=="CRT")  label = "CRITICAL";
    else if (type=="LOST") label = "LOST!";

    // Line 1: ALERT
    // Line 2: ID1:HLP  (node + alert type — scrolls if needed)
    // Line 3: NR:ID2+15 (nearest node + dB difference)
    String line2 = node + ":" + label;
    String line3 = "";
    if (nearNode != "" && nearNode != "NONE" && diff != "" && diff != "0") {
      line3 = "NR:" + nearNode + "+" + diff;
    } else if (nearNode != "" && nearNode != "NONE") {
      line3 = "NR:" + nearNode;
    }

    setDisplay("ALERT", line2, line3);
    alertLocal(5, 150, 150);
    return;
  }

  // ── RELAY request from peer ───────────────────────────────
  // Incoming format: "RELAY:ID1:NEED HELP"
  // We measure RSSI of this packet — that IS the signal strength
  // of the ID1→ID2 link. This is the only valid RSSI measurement.
  // Forwarded format: "ID1:NEED HELP:-98:viaID2"
  //                                   ^^^relay's measured RSSI
  if (msg.startsWith("RELAY:")) {
    int p1 = msg.indexOf(':');
    int p2 = msg.indexOf(':', p1+1);
    if (p1==-1 || p2==-1) return;

    String origNode    = msg.substring(p1+1, p2);
    String origPayload = msg.substring(p2+1);   // everything after "RELAY:IDx:"

    // Measure RSSI right now — this is how strong ID1's signal was
    // when WE (the relay node) received it. Most accurate measurement possible.
    int measuredRSSI = LoRa.packetRssi();

    if (origNode == String(NODE_ID)) return;

    // Build forwarded packet with relay-measured RSSI embedded
    String forwarded = origNode + ":" + origPayload + ":" + String(measuredRSSI) + ":via" + String(NODE_ID);

    Serial.println("[RELAY] Forwarding: " + forwarded + " (measured RSSI: " + String(measuredRSSI) + ")");
    setDisplay("RELAY", origNode, origPayload);

    delay(random(200, 500));
    LoRa.beginPacket();
    LoRa.print(forwarded);
    LoRa.endPacket();

    unsigned long start = millis();
    while (millis() - start < ACK_TIMEOUT + 500) {
      tickDisplay();
      int ps = LoRa.parsePacket();
      if (ps) {
        String resp = "";
        while (LoRa.available()) resp += (char)LoRa.read();

        if (resp == "ACK:" + origNode) {
          Serial.println("[RELAY] ACK for " + origNode + " — passing back.");
          delay(random(50, 150));
          LoRa.beginPacket();
          LoRa.print(resp);
          LoRa.endPacket();
          setDisplay("RELAY OK", origNode, "");
          alertLocal(3, 100, 100);
          return;
        }
        handleIncoming(resp);
      }
    }
    Serial.println("[RELAY] No leader ACK for " + origNode);
    return;
  }
}

// =============================================================
//  Display helpers
// =============================================================
void setDisplay(String l1, String l2, String l3) {
  String t[3] = {l1, l2, l3};
  for (int i = 0; i < 3; i++) {
    lines[i].text       = t[i];
    lines[i].offset     = 0;
    lines[i].paused     = true;
    lines[i].pauseStart = millis();
    lines[i].lastStep   = millis();
    lines[i].scrolling  = (t[i].length() > MAX_VISIBLE);
  }
  display.clearDisplay();
  for (int i = 0; i < 3; i++) drawLine(i, i * LINE_H);
  display.display();
}

void tickDisplay() {
  bool changed = false;
  for (int i = 0; i < 3; i++) {
    if (!lines[i].scrolling) continue;
    if (lines[i].paused) {
      if (millis() - lines[i].pauseStart >= SCROLL_PAUSE) {
        lines[i].paused   = false;
        lines[i].lastStep = millis();
      }
      continue;
    }
    if (millis() - lines[i].lastStep >= SCROLL_DELAY) {
      lines[i].offset++;
      int cycle = lines[i].text.length() + 3;
      if (lines[i].offset >= cycle) {
        lines[i].offset     = 0;
        lines[i].paused     = true;
        lines[i].pauseStart = millis();
      }
      lines[i].lastStep = millis();
      changed = true;
    }
  }
  if (changed) {
    display.clearDisplay();
    for (int i = 0; i < 3; i++) drawLine(i, i * LINE_H);
    display.display();
  }
}

void drawLine(int i, int y) {
  if (lines[i].text == "") return;
  display.setTextSize(2);
  display.setCursor(0, y);
  if (!lines[i].scrolling) {
    display.print(lines[i].text);
    return;
  }
  String t     = lines[i].text;
  int    tlen  = t.length();
  int    cycle = tlen + 3;
  String visible = "";
  for (int c = 0; c < MAX_VISIBLE; c++) {
    int idx = (lines[i].offset + c) % cycle;
    visible += (idx < tlen) ? t[idx] : ' ';
  }
  display.print(visible);
}

void alertLocal(int times, int onMs, int offMs) {
  for (int i = 0; i < times; i++) {
    digitalWrite(BUZZER, LOW);
    digitalWrite(LED,    HIGH);
    delay(onMs);
    digitalWrite(BUZZER, HIGH);
    digitalWrite(LED,    LOW);
    delay(offMs);
  }
}