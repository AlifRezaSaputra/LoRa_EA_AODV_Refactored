#include <Arduino.h>

#include "Config.h"
#include "Hardware.h"
#include "PacketFormat.h"
#include "Router.h"
#include "SeenEntry.h"

// Forward declaration diperlukan karena file .cpp tidak memakai auto-prototype Arduino.
int transmitFrame(const String& frame);

// ================================ STATE ===================================
unsigned long lastStatus = 0;
unsigned long lastRxActivity = 0;
unsigned long lastHelloAt = 0;

bool sosAlertActive = false;
unsigned long sosAlertStart = 0;
uint32_t countOk = 0;
uint32_t countCrcErr = 0;
uint32_t countIdleTicks = 0;
uint32_t countDuplicate = 0;
uint32_t countRreq = 0;
uint32_t countRrep = 0;
uint32_t countRerr = 0;
uint32_t countRouteCandidatesSeen = 0, countRouteCandidatesReplaced = 0;
uint32_t countRouteDiscoveryCompleted = 0, countRouteDiscoveryEmpty = 0;
uint32_t countDataBytes = 0;
uint32_t countRouteDiscoveryDurationMs = 0;
uint32_t countRouteFailures = 0;

// ============================ ROUTING TABLE ===============================
struct RouteEntry {
  bool used = false;
  String destination;
  String nextHop;
  String path;
  uint8_t minEnergy = 0;
  uint8_t hops = 0;
  unsigned long updatedAt = 0;
  unsigned long lifetimeMs = MY_ROUTE_TIMEOUT_MS;
};
RouteEntry routeTable[MAX_ROUTE_ENTRIES];

SeenEntry seenRreq[MAX_SEEN_RREQ];
SeenEntry seenData[MAX_SEEN_DATA];
SeenEntry seenRerr[MAX_SEEN_RERR];
SeenEntry completedRreq[16];
struct NeighborEntry { bool used=false; String id; unsigned long lastSeen=0; };
NeighborEntry neighbors[16];

struct RouteCandidate {
  bool used = false;
  String path;
  uint8_t minEnergy = 0;
  uint8_t hops = 0;
};
struct DiscoverySlot {
  bool used = false;
  String origin;
  String requestId;
  unsigned long startedAt = 0;
  RouteCandidate candidates[MAX_CANDIDATES];
};
DiscoverySlot discoverySlots[MAX_DISCOVERY_SLOTS];

void updateNeighbor(const String& id) {
  if (!id.length() || id == String(NODE_ID)) return;
  int freeSlot=-1, oldest=0;
  for (uint8_t i=0;i<16;i++) {
    if (neighbors[i].used && neighbors[i].id==id) { neighbors[i].lastSeen=millis(); return; }
    if (!neighbors[i].used && freeSlot<0) freeSlot=i;
    if (neighbors[i].lastSeen < neighbors[oldest].lastSeen) oldest=i;
  }
  int slot=freeSlot>=0?freeSlot:oldest; neighbors[slot].used=true; neighbors[slot].id=id; neighbors[slot].lastSeen=millis();
}
bool neighborAlive(const String& id) {
  for (uint8_t i=0;i<16;i++) if (neighbors[i].used && neighbors[i].id==id) return millis()-neighbors[i].lastSeen<=NEIGHBOR_TIMEOUT;
  return false;
}
void processNeighborMaintenance() {
  unsigned long now=millis();
  if (now-lastHelloAt>=HELLO_INTERVAL) { lastHelloAt=now; transmitFrame(String("HELLO|")+BASE_ID); }
  for (uint8_t i=0;i<MAX_ROUTE_ENTRIES;i++) {
    if (!routeTable[i].used) continue;
    if (now - routeTable[i].updatedAt > routeTable[i].lifetimeMs) {
      routeTable[i].used = false;
      continue;
    }
    if (routeTable[i].nextHop.length() && !neighborAlive(routeTable[i].nextHop)) {
      Serial.print("[LINK] Route ke "); Serial.print(routeTable[i].destination); Serial.println(" invalid: neighbor timeout");
      routeTable[i].used=false; countRouteFailures++;
    }
  }
}


// EA-AODV: higher bottleneck energy wins, then fewer hops.
// AODV mode: fewer hops wins; energy is deliberately ignored.
bool isBetterRouteCandidate(uint8_t candidateEnergy, uint8_t candidateHops,
                            const String& candidatePath, uint8_t currentEnergy,
                            uint8_t currentHops, const String& currentPath) {
  if (USE_EA_AODV && candidateEnergy != currentEnergy) {
    return candidateEnergy > currentEnergy;
  }
  if (candidateHops != currentHops) {
    return candidateHops < currentHops;
  }
  return candidatePath < currentPath;
}

float getBatteryVoltage() {
  return 3.30;
}

uint8_t calculateResidualEnergyPercent(float voltage) {
  (void)voltage;
  return 100;
}

uint8_t getResidualEnergyPercent() {
  return calculateResidualEnergyPercent(getBatteryVoltage());
}


// Radio facade: routing hanya mengetahui API frame, bukan objek RadioLib.
int transmitFrame(const String& frame) {
  return rxTransmitFrame(frame);
}

int findRouteIndex(const String& destination, bool refresh = true) {
  const unsigned long now = millis();
  for (uint8_t i = 0; i < MAX_ROUTE_ENTRIES; ++i) {
    RouteEntry& route = routeTable[i];
    if (!route.used || route.destination != destination) continue;
    const unsigned long elapsed = now - route.updatedAt;
    if (elapsed > route.lifetimeMs) {
      route.used = false;
      Serial.print("[ROUTE] Rute kedaluwarsa ke ");
      Serial.println(destination);
      continue;
    }
    // AODV memperpanjang lifetime ke max(sisa lifetime, ActiveRouteTimeout).
    if (refresh && (route.lifetimeMs - elapsed) < ACTIVE_ROUTE_TIMEOUT_MS) {
      route.updatedAt = now;
      route.lifetimeMs = ACTIVE_ROUTE_TIMEOUT_MS;
    }
    return i;
  }
  return -1;
}

bool getRoute(const String& destination, String& nextHop, String& path, uint8_t* energy = nullptr, uint8_t* hops = nullptr) {
  int i = findRouteIndex(destination, true);
  if (i < 0) return false;
  nextHop = routeTable[i].nextHop;
  path = routeTable[i].path;
  if (energy) *energy = routeTable[i].minEnergy;
  if (hops) *hops = routeTable[i].hops;
  return nextHop.length() > 0;
}

void installRoute(const String& destination, const String& nextHop, const String& path,
                  uint8_t minEnergy, uint8_t hops) {
  if (!destination.length() || !nextHop.length() || destination == NODE_ID) return;
  int slot = findRouteIndex(destination, false);
  if (slot < 0) {
    for (uint8_t i = 0; i < MAX_ROUTE_ENTRIES; ++i) {
      if (!routeTable[i].used) { slot = i; break; }
    }
  }
  if (slot < 0) {
    slot = 0;
    for (uint8_t i = 1; i < MAX_ROUTE_ENTRIES; ++i) {
      if ((long)(routeTable[i].updatedAt - routeTable[slot].updatedAt) < 0) slot = i;
    }
  }
  routeTable[slot].used = true;
  routeTable[slot].destination = destination;
  routeTable[slot].nextHop = nextHop;
  routeTable[slot].path = path;
  routeTable[slot].minEnergy = minEnergy;
  routeTable[slot].hops = hops;
  routeTable[slot].updatedAt = millis();
  routeTable[slot].lifetimeMs = MY_ROUTE_TIMEOUT_MS;
  Serial.print("[ROUTE] "); Serial.print(NODE_ID);
  Serial.print(" -> "); Serial.print(destination);
  Serial.print(" via "); Serial.print(nextHop);
  Serial.print(" | E_min="); Serial.print(minEnergy);
  Serial.print("% | hops="); Serial.println(hops);
}

void invalidateRoutesByPath(const String& path) {
  for (uint8_t i = 0; i < MAX_ROUTE_ENTRIES; ++i) {
    if (routeTable[i].used && routeTable[i].path == path) routeTable[i].used = false;
  }
}

// ============================ DEDUPLICATION ===============================
bool rememberKey(SeenEntry entries[], uint8_t cap, const String& key, unsigned long lifetime) {
  unsigned long now = millis();
  int freeSlot = -1;
  int oldestSlot = 0;
  for (uint8_t i = 0; i < cap; ++i) {
    if (entries[i].used && now - entries[i].at > lifetime) entries[i].used = false;
    if (entries[i].used && entries[i].key == key) return false;
    if (!entries[i].used && freeSlot < 0) freeSlot = i;
    if (entries[i].at < entries[oldestSlot].at) oldestSlot = i;
  }
  int slot = (freeSlot >= 0) ? freeSlot : oldestSlot;
  entries[slot].used = true;
  entries[slot].key = key;
  entries[slot].at = now;
  return true;
}


bool hasFreshKey(SeenEntry entries[], uint8_t cap, const String& key, unsigned long lifetime) {
  unsigned long now = millis();
  for (uint8_t i = 0; i < cap; ++i) {
    if (entries[i].used && now - entries[i].at > lifetime) entries[i].used = false;
    if (entries[i].used && entries[i].key == key) return true;
  }
  return false;
}

// ============================== DISCOVERY ================================
int findDiscoverySlot(const String& origin, const String& requestId) {
  for (uint8_t i = 0; i < MAX_DISCOVERY_SLOTS; ++i) {
    if (discoverySlots[i].used && discoverySlots[i].origin == origin &&
        discoverySlots[i].requestId == requestId) return i;
  }
  return -1;
}

int allocateDiscoverySlot(const String& origin, const String& requestId) {
  for (uint8_t i = 0; i < MAX_DISCOVERY_SLOTS; ++i) {
    if (!discoverySlots[i].used) {
      discoverySlots[i].used = true;
      discoverySlots[i].origin = origin;
      discoverySlots[i].requestId = requestId;
      discoverySlots[i].startedAt = millis();
      for (uint8_t j = 0; j < MAX_CANDIDATES; ++j) discoverySlots[i].candidates[j].used = false;
      return i;
    }
  }
  int oldest = 0;
  for (uint8_t i = 1; i < MAX_DISCOVERY_SLOTS; ++i) {
    if ((long)(discoverySlots[i].startedAt - discoverySlots[oldest].startedAt) < 0) oldest = i;
  }
  discoverySlots[oldest].used = true;
  discoverySlots[oldest].origin = origin;
  discoverySlots[oldest].requestId = requestId;
  discoverySlots[oldest].startedAt = millis();
  for (uint8_t j = 0; j < MAX_CANDIDATES; ++j) discoverySlots[oldest].candidates[j].used = false;
  return oldest;
}

void addRouteCandidate(const String& origin, const String& requestId, const String& candidatePath,
                       uint8_t minEnergy, uint8_t hops) {
  int slot = findDiscoverySlot(origin, requestId);
  if (slot < 0) slot = allocateDiscoverySlot(origin, requestId);
  DiscoverySlot& d = discoverySlots[slot]; countRouteCandidatesSeen++;
  int cslot = -1;
  for (uint8_t i=0;i<MAX_CANDIDATES;i++) {
    if (d.candidates[i].used && d.candidates[i].path == candidatePath) { cslot=i; break; }
  }
  if (cslot >= 0) {
    if (!isBetterRouteCandidate(minEnergy,hops,candidatePath,d.candidates[cslot].minEnergy,d.candidates[cslot].hops,d.candidates[cslot].path)) return;
  } else {
    for (uint8_t i=0;i<MAX_CANDIDATES;i++) if (!d.candidates[i].used) { cslot=i; break; }
    if (cslot < 0) {
      int worst=0;
      for (uint8_t i=1;i<MAX_CANDIDATES;i++)
        if (isBetterRouteCandidate(d.candidates[worst].minEnergy,d.candidates[worst].hops,d.candidates[worst].path,
                                    d.candidates[i].minEnergy,d.candidates[i].hops,d.candidates[i].path)) worst=i;
      if (!isBetterRouteCandidate(minEnergy,hops,candidatePath,d.candidates[worst].minEnergy,d.candidates[worst].hops,d.candidates[worst].path)) return;
      cslot=worst; countRouteCandidatesReplaced++;
    }
  }
  d.candidates[cslot].used=true; d.candidates[cslot].path=candidatePath;
  d.candidates[cslot].minEnergy=minEnergy; d.candidates[cslot].hops=hops;
  Serial.print("[RREQ] Kandidat Top-K: "); Serial.print(candidatePath);
  if (USE_EA_AODV) { Serial.print(" | E_min="); Serial.print(minEnergy); Serial.print("% | hops="); Serial.println(hops); }
  else { Serial.print(" | metric=AODV hop-count | hops="); Serial.println(hops); }
}

void sendRouteReply(const String& origin, const String& requestId, const String& path,
                    uint8_t energy, uint8_t hops) {
  uint8_t n = pathCount(path);
  if (n < 2) return;
  String previousHop = pathNodeAt(path, n - 2);
  installRoute(origin, previousHop, path, energy, hops);
  updateNeighbor(previousHop);
  String frame = "RREP|" + origin + "|" + String(BASE_ID) + "|" + requestId + "|" +
                 path + "|" + String(n - 2) + "|" + String(energy) + "|" + String(MAX_ROUTE_TTL);
  countRrep++;
  Serial.print("[RREP] Jalur terpilih: "); Serial.print(path);
  if (USE_EA_AODV) {
    Serial.print(" | E_min="); Serial.print(energy);
    Serial.print("% | hops="); Serial.println(hops);
  } else {
    Serial.print(" | metric=AODV hop-count | hops="); Serial.println(hops);
  }
  transmitFrame(frame);
}

void processDiscoveryWindows() {
  unsigned long now = millis();
  for (uint8_t i = 0; i < MAX_DISCOVERY_SLOTS; ++i) {
    DiscoverySlot& d = discoverySlots[i];
    if (!d.used || now - d.startedAt < ROUTE_DISCOVERY_WINDOW) continue;

    int best = -1;
    for (uint8_t c = 0; c < MAX_CANDIDATES; ++c) {
      if (!d.candidates[c].used) continue;
      if (best < 0 || isBetterRouteCandidate(
              d.candidates[c].minEnergy, d.candidates[c].hops, d.candidates[c].path,
              d.candidates[best].minEnergy, d.candidates[best].hops, d.candidates[best].path)) {
        best = c;
      }
    }
    if (best >= 0) {
      countRouteDiscoveryCompleted++;
      countRouteDiscoveryDurationMs += (uint32_t)(now - d.startedAt);
      // Transmit RREP before diagnostic Serial output to minimize control-plane delay.
      sendRouteReply(d.origin, d.requestId, d.candidates[best].path,
                     d.candidates[best].minEnergy, d.candidates[best].hops);
      Serial.print("[METRIC,DISCOVERY] origin="); Serial.print(d.origin);
      Serial.print(",request="); Serial.print(d.requestId);
      Serial.print(",duration_ms="); Serial.println(now - d.startedAt);
    }
    if (best < 0) countRouteDiscoveryEmpty++;
    rememberKey(completedRreq, 16, d.origin + "|" + d.requestId, RREQ_SEEN_LIFETIME);
    d.used = false;
  }
}

void handleRreq(String f[], uint8_t n) {
  // RREQ|origin|destination|requestId|previousHop|hops|minEnergy|path|ttl
  if (n < 9) return;
  String origin = f[1], destination = f[2], requestId = f[3];
  uint8_t hops = (uint8_t)constrain(f[5].toInt(), 0, 255);
  uint8_t energy = (uint8_t)constrain(f[6].toInt(), 0, 100);
  String path = f[7];

  if (origin == NODE_ID || !origin.length() || destination != String(NODE_ID)) return;
  updateNeighbor(f[4]);
  if (pathContains(path, String(NODE_ID))) return;
  if (hasFreshKey(completedRreq, 16, origin + "|" + requestId, RREQ_SEEN_LIFETIME)) return;

  String seenKey = origin + "|" + requestId + "|" + path;
  // Exact duplicate paths are filtered; distinct paths remain eligible for Top-K ranking.
  if (!rememberKey(seenRreq, MAX_SEEN_RREQ, seenKey, RREQ_SEEN_LIFETIME)) return;

  uint8_t pathEnergy = USE_EA_AODV ? min8(energy, getResidualEnergyPercent()) : 100;
  String finalPath = appendPath(path, String(NODE_ID));
  uint8_t finalHops = (uint8_t)min(255, (int)hops + 1);
  countRreq++;
  addRouteCandidate(origin, requestId, finalPath, pathEnergy, finalHops);
}


// ============================ DATA HANDLING ==============================
bool rememberData(const String& src, uint32_t seq) {
  return rememberKey(seenData, MAX_SEEN_DATA, src + "|" + String(seq), 120000UL);
}

void logToServer(const String& nodeId, uint32_t seq, float lat, float lng,
                 float battV, float battPct, bool sos, const String& hops, const String& path) {
  String json = "{";
  json += "\"node_id\":\"" + nodeId + "\",";
  json += "\"seq\":" + String(seq) + ",";
  json += "\"lat\":" + String(lat, 6) + ",";
  json += "\"lng\":" + String(lng, 6) + ",";
  json += "\"battery_voltage\":" + String(battV, 2) + ",";
  json += "\"battery_percent\":" + String(battPct, 0) + ",";
  json += "\"sos\":" + String(sos ? "true" : "false") + ",";
  json += "\"hops\":\"" + hops + "\",";
  json += "\"path\":\"" + path + "\"}";
  Serial.print("[SERVER] "); Serial.println(json);
  Serial.print("[METRIC,RX]"); Serial.print(",node="); Serial.print(nodeId);
  Serial.print(",seq="); Serial.print(seq); Serial.print(",sos="); Serial.print(sos ? 1 : 0);
  Serial.print(",hops="); Serial.print(hops); Serial.print(",path="); Serial.print(path);
  Serial.print(",battery_pct="); Serial.println(battPct, 1);
}

void sendAck(const String& destination, uint32_t seq) {
  String nextHop, path;
  if (!getRoute(destination, nextHop, path)) nextHop = destination; // direct fallback
  String ack = String(BASE_ID) + "|" + destination + "|ACK|" + String(seq) + "|" +
               nextHop + "|" + String(MAX_ROUTE_TTL);
  transmitFrame(ack);
  Serial.print("[ACK] -> "); Serial.print(destination); Serial.print(" via ");
  Serial.print(nextHop); Serial.print(" seq#"); Serial.println(seq);
}

void handleData(String f[], uint8_t n) {
  // Telemetry payload: src|dst|seq|lat|lng|battV|battPct|sos|hops|path|nextHop|ttl
  if (n < 10) return;
  String src = f[0], dst = f[1];
  uint32_t seq = (uint32_t)f[2].toInt();
  if (!src.length() || dst != String(NODE_ID)) return;
  uint8_t receivedPathCount = pathCount(f[9]);
  if (receivedPathCount > 0) updateNeighbor(pathNodeAt(f[9], receivedPathCount - 1));
  if (n >= 12 && f[10].length() && f[10] != String(NODE_ID)) return;

  bool firstSeen = rememberData(src, seq);
  if (!firstSeen) {
    countDuplicate++;
    sendAck(src, seq); // ACK duplicate immediately so a lost ACK does not stall the sender.
    Serial.print("[DATA] Duplikat "); Serial.print(src); Serial.print(" seq#");
    Serial.println(seq);
    return;
  }

  countOk++;
  countDataBytes += joinFields(f, n).length();
  String path = f[9];
  if (!pathContains(path, String(NODE_ID))) path = appendPath(path, String(NODE_ID));
  uint8_t hops = (uint8_t)constrain(f[8].toInt(), 0, 255);
  if (hops < 255 && !pathContains(f[9], String(NODE_ID))) hops++;
  float lat = f[3].toFloat();
  float lng = f[4].toFloat();
  float battV = f[5].toFloat();
  float battPct = f[6].toFloat();
  bool sos = f[7] == "1";

  // ACK first: don't make the sender wait for Serial/JSON output or buzzer work.
  sendAck(src, seq);
  Serial.println(); Serial.println("=== PAKET DITERIMA ===");
  Serial.print("Dari: "); Serial.println(src);
  Serial.print("Isi paket: "); Serial.println(joinFields(f, n));
  logToServer(src, seq, lat, lng, battV, battPct, sos, String(hops), path);

  if (sos) {
    Serial.println("[SOS] DARURAT! Mengaktifkan buzzer.");
    rxActivateBuzzer();
    sosAlertActive = true;
    sosAlertStart = millis();
  } else if (!sosAlertActive) {
    rxLedPulse();
  }
  // ACK was already transmitted immediately after packet validation.
}

// ================================= RERR ===================================
void handleRerr(String f[], uint8_t n) {
  // RERR|reporter|destination|brokenPath|seq|ttl
  if (n < 6) return;
  String reporter = f[1], destination = f[2], brokenPath = f[3];
  uint32_t seq = (uint32_t)f[4].toInt();
  String key = reporter + "|" + destination + "|" + brokenPath + "|" + String(seq);
  if (!rememberKey(seenRerr, MAX_SEEN_RERR, key, RREQ_SEEN_LIFETIME)) return;
  if (pathContains(brokenPath, String(NODE_ID))) {
    invalidateRoutesByPath(brokenPath);
    countRerr++;
    Serial.print("[RERR] Rute terdampak dihapus: ");
    Serial.println(brokenPath);
  }
}

// ============================ PACKET DISPATCH =============================
void handlePacket(const String& packet) {
  if (!packet.length()) return;
  String f[MAX_FIELDS];
  uint8_t n = splitFields(packet, f, MAX_FIELDS);
  if (n == 0) return;

  if (f[0] == "HELLO" && n >= 2) { updateNeighbor(f[1]); lastRxActivity = millis(); return; }
  if (f[0] == "RREQ") { handleRreq(f, n); return; }
  if (f[0] == "RERR") { handleRerr(f, n); return; }
  if (f[0] == "RREP") return;  
  if (n >= 4 && f[2] == "ACK") return;  
  handleData(f, n);
}

void pollRadio() {
  String packet;
  int state = HW_RADIO_OK;
  if (!rxPollRadio(packet, state)) return;

  lastRxActivity = millis();
  if (state == HW_RADIO_OK && packet.length()) {
    handlePacket(packet);
  } else if (state == HW_RADIO_CRC_MISMATCH) {
    countCrcErr++;
  }
}

// ================================ APP API =================================
void routerBegin() {
  Serial.begin(115200);
  randomSeed((uint32_t)micros() ^ (uint32_t)analogRead(34));
  delay(200);
  Serial.println();
  Serial.println("=== LoRa EA-AODV Node RX (BASECAMP) ===");
  Serial.print("ID: "); Serial.println(NODE_ID);
  Serial.print("Base ID: "); Serial.println(BASE_ID);
  Serial.print("[ROUTING] Mode: ");
  Serial.println(USE_EA_AODV
                   ? "EA-AODV (max-min energy, hop tie-break)"
                   : "AODV standard (minimum hop-count)");

  if (!rxHardwareBegin()) {
    // Sama seperti sketch asal: LED tetap ON saat inisialisasi LoRa gagal.
    while (true) delay(10);
  }

  lastRxActivity = millis();
  Serial.println("[INIT] LoRa siap; basecamp menunggu RREQ dan DATA.");
}

void routerUpdate() {
  unsigned long now = millis();
  pollRadio();
  processDiscoveryWindows();
  processNeighborMaintenance();
  rxLedUpdate();
  now = millis();

  if (sosAlertActive && now - sosAlertStart > SOS_ALERT_DURATION) {
    rxDeactivateBuzzer();
    sosAlertActive = false;
    Serial.println("[SOS] Alert berakhir.");
  }

  if (now - lastStatus >= STATUS_INTERVAL) {
    lastStatus = now;
    Serial.print("[STATUS] OK:"); Serial.print(countOk);
    Serial.print(" | CRC:"); Serial.print(countCrcErr);
    Serial.print(" | IdleTicks:"); Serial.print(countIdleTicks);
    Serial.print(" | Duplicate:"); Serial.print(countDuplicate);
    Serial.print(" | RREQ:"); Serial.print(countRreq);
    Serial.print(" | RREP:"); Serial.print(countRrep);
    Serial.print(" | RERR:"); Serial.print(countRerr);
    Serial.print(" | Candidates:"); Serial.print(countRouteCandidatesSeen);
    Serial.print(" | Replaced:"); Serial.print(countRouteCandidatesReplaced);
    Serial.print(" | DiscoveryOK:"); Serial.print(countRouteDiscoveryCompleted);
    Serial.print(" | DiscoveryEmpty:"); Serial.print(countRouteDiscoveryEmpty);
    Serial.print(" | RXBytes:"); Serial.print(countDataBytes);
    Serial.print(" | AvgDiscoveryMs:"); Serial.println(countRouteDiscoveryCompleted ? countRouteDiscoveryDurationMs / countRouteDiscoveryCompleted : 0);
  }

  if ((long)(now - lastRxActivity) >= (long)RX_TIMEOUT) {
    countIdleTicks++;
    lastRxActivity = now;
  }
}