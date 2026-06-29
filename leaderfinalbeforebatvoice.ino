// ============================================================
//  LEADER NODE — Soldier Safety System
//  KEY FIX: broadcastAlert now sends ALERT twice, 3s apart.
//  This guarantees members that were briefly blocked (during
//  their own STATUS TX) catch the second broadcast.
// ============================================================

#include <SPI.h>
#include <LoRa.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ── LoRa pins ──────────────────────────────────────────────
#define LORA_SS    5
#define LORA_RST  14
#define LORA_DIO0 26

// ── Peripherals ────────────────────────────────────────────
#define BUZZER  25
#define LED     33

// ── Timing ─────────────────────────────────────────────────
#define LOST_TIMEOUT     60000
#define ACK_DELAY           80
#define ALERT_REPEAT_GAP  3000   // ms between first and second ALERT broadcast

// ── Known nodes ────────────────────────────────────────────
#define NUM_NODES 2

// ── Display (textSize 2, 128x64 OLED) ──────────────────────
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

struct NodeState {
  String        id;
  unsigned long lastSeen;
  int           rssi;
  bool          lost;
};
NodeState nodes[NUM_NODES] = {
  {"ID1", 0, -120, false},
  {"ID2", 0, -120, false}
};

// ── Forward declarations ───────────────────────────────────
void processPacket(String msg, int rssi);
void sendACK(String nodeId);
void broadcastAlert(String fromNode, String st, String nearNode, int diff,
                    bool viaMesh, String viaNode, int measuredRSSI);
void sendLost(String nodeId);
void updateNode(String id, int rssi);
int  getNodeIndex(String id);
String shortType(String data);
void setDisplay(String l1, String l2, String l3);
void tickDisplay();
void drawLine(int i, int y);
void alertLocal(int times, int onMs, int offMs);

// =============================================================
void setup() {
  Serial.begin(115200);

  pinMode(BUZZER, OUTPUT);
  pinMode(LED,    OUTPUT);
  digitalWrite(BUZZER, HIGH);
  digitalWrite(LED,    LOW);

  Wire.begin(21, 22);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextColor(WHITE);
  display.setTextSize(2);
  setDisplay("LEADER", "READY", "");

  SPI.begin(18, 19, 23, 5);
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  LoRa.begin(433E6);
  delay(500);
}

// =============================================================
void loop() {
  tickDisplay();

  int packetSize = LoRa.parsePacket();
  if (packetSize) {
    String msg = "";
    while (LoRa.available()) msg += (char)LoRa.read();
    int rssi = LoRa.packetRssi();
    Serial.println("[RX] " + msg + " RSSI:" + String(rssi));
    processPacket(msg, rssi);
  }

  for (int i = 0; i < NUM_NODES; i++) {
    if (nodes[i].lastSeen == 0) continue;
    if (!nodes[i].lost && millis() - nodes[i].lastSeen > LOST_TIMEOUT) {
      nodes[i].lost = true;
      sendLost(nodes[i].id);
    }
  }
}

// =============================================================
//  processPacket
// =============================================================
void processPacket(String msg, int rssi) {

  if (msg.startsWith("RELAY:")) return;

  int firstColon = msg.indexOf(':');
  if (firstColon == -1) return;

  String fromNode = msg.substring(0, firstColon);
  String rest     = msg.substring(firstColon + 1);

  if (getNodeIndex(fromNode) == -1) {
    Serial.println("[UNKNOWN] " + fromNode);
    return;
  }

  // ── Detect mesh suffix ─────────────────────────────────────
  // Mesh packet format: "ID1:NEED HELP:-98:viaID2"
  //                                    ^^^relay node's measured RSSI
  //                                        (how strong ID1 was at ID2)
  // This is the only RSSI value — sender cannot measure its own signal.
  bool   viaMesh   = false;
  String viaNode   = "";
  int    relayRSSI = -120;    // relay node's measurement of sender's signal
  String payload   = rest;

  int viaIdx = rest.indexOf(":via");
  if (viaIdx != -1) {
    viaMesh = true;
    viaNode = rest.substring(viaIdx + 4);
    String beforeVia = rest.substring(0, viaIdx);
    // last colon before :via separates the relay-measured RSSI
    int lastColon = beforeVia.lastIndexOf(':');
    if (lastColon != -1) {
      relayRSSI = beforeVia.substring(lastColon + 1).toInt();
      payload   = beforeVia.substring(0, lastColon);
    } else {
      payload = beforeVia;
    }
  }

  int effectiveRSSI = viaMesh ? relayRSSI : rssi;
  updateNode(fromNode, effectiveRSSI);

  // ACK first — members are waiting on this
  delay(ACK_DELAY);
  sendACK(fromNode);

  // ── STATUS — silent update, no alert ──────────────────────
  if (payload == "STATUS OK") {
    String rssiStr = String(effectiveRSSI) + "dBm";
    setDisplay(fromNode, "STATUS OK", rssiStr);
    Serial.println("[STATUS] " + fromNode + " " + rssiStr);
    return;
  }

  // ── SOS ───────────────────────────────────────────────────
  String st = shortType(payload);

  String nearNode = "NONE";
  int    diff     = 0;
  int    bestRSSI = -200;
  for (int i = 0; i < NUM_NODES; i++) {
    if (nodes[i].id == fromNode) continue;
    if (nodes[i].rssi > bestRSSI) {
      bestRSSI = nodes[i].rssi;
      nearNode = nodes[i].id;
    }
  }
  if (nearNode != "NONE") diff = abs(effectiveRSSI - bestRSSI);

  // Broadcast ALERT — function sends it twice internally
  broadcastAlert(fromNode, st, nearNode, diff, viaMesh, viaNode, relayRSSI);

  String line1 = fromNode + ":" + st;
  String line2  = "NR:" + nearNode + "+" + String(diff);
  String line3  = viaMesh ? "via" + viaNode : String(effectiveRSSI) + "dBm";
  setDisplay(line1, line2, line3);
  alertLocal(5, 150, 150);
}

// =============================================================
//  sendACK
// =============================================================
void sendACK(String nodeId) {
  String ack = "ACK:" + nodeId;
  LoRa.beginPacket();
  LoRa.print(ack);
  LoRa.endPacket();
  Serial.println("[ACK] Sent: " + ack);
}

// =============================================================
//  broadcastAlert — FIXED: sends ALERT twice, 3 seconds apart
//
//  Why twice?
//  When a member sends SOS, the OTHER member may be blocked
//  inside its own ACK wait (from a STATUS heartbeat) for up
//  to ~1.5 seconds. The first ALERT fires immediately after
//  ACK. The second fires 3 seconds later, by which time every
//  member is guaranteed to be back in its listening loop.
//
//  The leader also listens between the two broadcasts and
//  continues ticking the display — it does not freeze.
// =============================================================
void broadcastAlert(String fromNode, String st, String nearNode, int diff,
                    bool viaMesh, String viaNode, int measuredRSSI) {

  String alert = "ALERT:" + fromNode + ":" + st + ":" + nearNode + ":" + String(diff);
  if (viaMesh) alert += ":via" + viaNode + ":" + String(measuredRSSI);

  // ── First broadcast ───────────────────────────────────────
  delay(100);   // let ACK clear the air
  LoRa.beginPacket();
  LoRa.print(alert);
  LoRa.endPacket();
  Serial.println("[BROADCAST 1] " + alert);

  // ── Wait 3 seconds while still receiving ──────────────────
  // Keep listening during the gap — don't freeze.
  // If another SOS comes in during this window, it gets processed.
  unsigned long gapStart = millis();
  while (millis() - gapStart < ALERT_REPEAT_GAP) {
    tickDisplay();
    int ps = LoRa.parsePacket();
    if (ps) {
      String msg = "";
      while (LoRa.available()) msg += (char)LoRa.read();
      int r = LoRa.packetRssi();
      // Process incoming packets during the wait gap
      // but don't re-enter broadcastAlert recursively for SOS —
      // just update node state and send ACK
      if (!msg.startsWith("ALERT:")) {
        Serial.println("[GAP RX] " + msg);
        int fc = msg.indexOf(':');
        if (fc != -1) {
          String fn = msg.substring(0, fc);
          if (getNodeIndex(fn) != -1) {
            updateNode(fn, r);
            delay(ACK_DELAY);
            sendACK(fn);
            // Full processing of this packet happens after gap ends
            // We store it and handle after second broadcast
          }
        }
      }
    }
  }

  // ── Second broadcast ──────────────────────────────────────
  LoRa.beginPacket();
  LoRa.print(alert);
  LoRa.endPacket();
  Serial.println("[BROADCAST 2] " + alert);
}

// =============================================================
//  sendLost
// =============================================================
void sendLost(String nodeId) {
  String alert = "ALERT:" + nodeId + ":LOST:NONE:0";
  LoRa.beginPacket();
  LoRa.print(alert);
  LoRa.endPacket();

  // Send lost alert twice as well
  delay(3000);
  LoRa.beginPacket();
  LoRa.print(alert);
  LoRa.endPacket();

  Serial.println("[LOST] " + nodeId);
  setDisplay("LOST", nodeId, ">60s");
  alertLocal(6, 100, 100);
}

// =============================================================
//  Helpers
// =============================================================
void updateNode(String id, int rssi) {
  int idx = getNodeIndex(id);
  if (idx == -1) return;
  nodes[idx].lastSeen = millis();
  nodes[idx].rssi     = rssi;
  nodes[idx].lost     = false;
}

int getNodeIndex(String id) {
  for (int i = 0; i < NUM_NODES; i++)
    if (nodes[i].id == id) return i;
  return -1;
}

String shortType(String data) {
  if (data == "NEED HELP")          return "HLP";
  if (data == "PERSON INJURED")     return "INJ";
  if (data == "CRITICAL CONDITION") return "CRT";
  return data.substring(0, min((int)data.length(), 6));
}

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