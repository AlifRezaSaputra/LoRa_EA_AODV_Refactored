#include <Arduino.h>

#include "Config.h"
#include "Hardware.h"
#include "PacketFormat.h"
#include "Router.h"
#include "SeenEntry.h"

// ================================ STATE ===================================
uint32_t seqNumber = 0;                 // Application DATA sequence (unchanged)
uint32_t originSequenceNumber = 0;       // AODV originator sequence number
bool originSequenceAllocatedForDiscovery = false;
uint32_t baseSequenceNumber = 0;         // Latest known BASE destination sequence
uint32_t rreqId = 0;
unsigned long lastSend = 0;
unsigned long lastRreqAt = 0;
unsigned long lastBatteryCheck = 0;
unsigned long lastHelloAt = 0;
unsigned long lastRreqForwardAt = 0;
unsigned long nextRreqRetryAt = 0;
unsigned long nextRreqAllowedAt = 0;
unsigned long nextRerrAllowedAt = 0;
uint8_t currentRreqTtl = TTL_START;
uint8_t rreqMaxTtlCount = 0;
bool routeDiscoveryExhausted = false;
uint32_t countRreqForwarded = 0, countDataForwarded = 0, countRerrSent = 0;
uint32_t countRouteDiscoveries = 0, countRouteFailures = 0;
uint32_t countAckSuccess = 0; uint64_t totalAckRttMs = 0;
bool batteryLowWarning = false;

bool sosActive = false;
bool waitingAck = false;
bool waitingSOSAck = false;
unsigned long ackWaitStart = 0;
uint32_t ackExpectedSeq = 0;
String lastSentPath = "";
bool routeDiscoveryInProgress = false;
bool pendingDataValid = false;
String pendingDataPayload = "";

// Forward declarations for functions used before their definitions.
void sendRouteError(const String& path, const String& destination, uint32_t seq);
void sendDataPayload(const String& payload);
void sendDataPacket();

uint32_t countAckTimeout = 0;
uint32_t countRouteDiscoveryCompleted = 0;
uint64_t totalRouteDiscoveryMs = 0;
unsigned long routeDiscoveryStartedAt = 0;
unsigned long currentAckTimeoutMs = DATA_ACK_TIMEOUT_MS;
String lastSentPayload = "";

// ============================ ROUTING TABLE ===============================
struct RouteEntry {
  bool used = false;
  String destination;
  String nextHop;
  String path;
  uint8_t minEnergy = 0;
  uint8_t hops = 0;
  uint32_t destinationSequence = 0;
  bool sequenceValid = false;
  String precursors; // Comma-separated AODV precursor neighbours.
  unsigned long updatedAt = 0;
  unsigned long lifetimeMs = MY_ROUTE_TIMEOUT_MS;
};
RouteEntry routeTable[MAX_ROUTE_ENTRIES];

// SeenEntry is declared in SeenEntry.h to avoid Arduino auto-prototype errors.
SeenEntry seenRreq[MAX_SEEN_RREQ];
SeenEntry seenRerr[MAX_SEEN_RERR];
struct BestRreqMetric { bool used=false; String key; String path; uint8_t energy=0; uint8_t hops=255; unsigned long at=0; };
BestRreqMetric bestRreq[MAX_SEEN_RREQ];

struct TxQueueEntry {
  bool used = false;
  String payload;
  unsigned long dueAt = 0;
  uint8_t priority = TX_QUEUE_PRIORITY_NORMAL; // nilai lebih kecil = prioritas lebih tinggi
};
struct NeighborEntry { bool used = false; String id; unsigned long lastSeen = 0; };
struct BlacklistEntry { bool used = false; String id; unsigned long until = 0; };
struct PendingRrepAck { bool used = false; String neighbor; String requestId; unsigned long sentAt = 0; };
struct PendingRoutePacket {
  bool used = false;
  String frame;
  String key;
  unsigned long enqueuedAt = 0;
};
NeighborEntry neighbors[MAX_NEIGHBORS];
BlacklistEntry blacklist[MAX_BLACKLIST_ENTRIES];
PendingRrepAck pendingRrepAcks[MAX_PENDING_RREP_ACKS];
TxQueueEntry txQueue[MAX_TX_QUEUE];
PendingRoutePacket pendingRouteQueue[MAX_PENDING_ROUTE_PACKETS];


// Radio facade: routing hanya mengetahui API frame, bukan objek RadioLib.
int transmitFrame(const String& frame) {
  return txTransmitFrame(frame);
}

uint8_t framePriority(const String& frame) {
  if (frame.startsWith("RREP|") || frame.startsWith("RREP_ACK|") ||
      frame.startsWith("RERR|") || frame.indexOf("|ACK|") >= 0) return TX_QUEUE_PRIORITY_CONTROL;
  if (frame.startsWith("RREQ|")) return TX_QUEUE_PRIORITY_RREQ;
  if (frame.startsWith("HELLO|")) return TX_QUEUE_PRIORITY_NORMAL;
  String f[MAX_FIELDS]; uint8_t n = splitFields(frame, f, MAX_FIELDS);
  if (n >= 8 && f[7] == "1") return TX_QUEUE_PRIORITY_SOS; // SOS telemetry
  return TX_QUEUE_PRIORITY_NORMAL;
}

bool enqueueFrame(const String& frame, unsigned long delayMs = 0) {
  uint8_t priority = framePriority(frame);
  for (uint8_t i = 0; i < MAX_TX_QUEUE; ++i) {
    if (!txQueue[i].used) {
      txQueue[i].used = true; txQueue[i].payload = frame;
      txQueue[i].dueAt = millis() + delayMs; txQueue[i].priority = priority;
      return true;
    }
  }
  // When full, preserve control/SOS traffic by evicting the lowest-priority newest item.
  int victim = -1;
  for (uint8_t i = 0; i < MAX_TX_QUEUE; ++i)
    if (txQueue[i].priority > priority && (victim < 0 || txQueue[i].priority > txQueue[victim].priority)) victim = i;
  if (victim >= 0) {
    txQueue[victim].payload = frame; txQueue[victim].dueAt = millis() + delayMs; txQueue[victim].priority = priority;
    Serial.println("[QUEUE] Paket prioritas tinggi menggantikan paket prioritas rendah.");
    return true;
  }
  Serial.println("[QUEUE] Penuh; paket dibuang sesuai kebijakan prioritas.");
  return false;
}

void processTxQueue() {
  unsigned long now = millis(); int best = -1;
  for (uint8_t i = 0; i < MAX_TX_QUEUE; ++i) {
    if (!txQueue[i].used || (long)(now - txQueue[i].dueAt) < 0) continue;
    if (best < 0 || txQueue[i].priority < txQueue[best].priority ||
        (txQueue[i].priority == txQueue[best].priority && (long)(txQueue[i].dueAt - txQueue[best].dueAt) < 0)) best = i;
  }
  if (best >= 0) {
    String frame = txQueue[best].payload; txQueue[best].used = false; txQueue[best].payload = "";
    transmitFrame(frame);
  }
}

void updateNeighbor(const String& id) {
  if (!id.length() || id == String(NODE_ID)) return;
  int freeSlot = -1, oldest = 0;
  for (uint8_t i=0;i<MAX_NEIGHBORS;i++) {
    if (neighbors[i].used && neighbors[i].id == id) { neighbors[i].lastSeen = millis(); return; }
    if (!neighbors[i].used && freeSlot < 0) freeSlot = i;
    if (neighbors[i].lastSeen < neighbors[oldest].lastSeen) oldest = i;
  }
  int slot = freeSlot >= 0 ? freeSlot : oldest;
  neighbors[slot].used = true; neighbors[slot].id = id; neighbors[slot].lastSeen = millis();
}
bool neighborAlive(const String& id) {
  for (uint8_t i=0;i<MAX_NEIGHBORS;i++) if (neighbors[i].used && neighbors[i].id == id)
    return millis() - neighbors[i].lastSeen <= NEIGHBOR_TIMEOUT;
  return false;
}

bool isNeighborBlacklisted(const String& id) {
  unsigned long now = millis();
  for (uint8_t i = 0; i < MAX_BLACKLIST_ENTRIES; ++i) {
    if (blacklist[i].used && (long)(now - blacklist[i].until) >= 0)
      blacklist[i].used = false;
    if (blacklist[i].used && blacklist[i].id == id) return true;
  }
  return false;
}

void blacklistNeighbor(const String& id) {
  if (!id.length()) return;
  int slot = -1;
  for (uint8_t i = 0; i < MAX_BLACKLIST_ENTRIES; ++i) {
    if (blacklist[i].used && blacklist[i].id == id) { slot = i; break; }
    if (!blacklist[i].used && slot < 0) slot = i;
  }
  if (slot < 0) slot = 0;
  blacklist[slot].used = true;
  blacklist[slot].id = id;
  blacklist[slot].until = millis() + BLACKLIST_TIMEOUT_MS;
  Serial.print("[AODV] Neighbor masuk blacklist: "); Serial.println(id);
}

void trackRrepAck(const String& neighbor, const String& requestId) {
  int slot = -1;
  for (uint8_t i = 0; i < MAX_PENDING_RREP_ACKS; ++i) {
    if (pendingRrepAcks[i].used && pendingRrepAcks[i].neighbor == neighbor &&
        pendingRrepAcks[i].requestId == requestId) { slot = i; break; }
    if (!pendingRrepAcks[i].used && slot < 0) slot = i;
  }
  if (slot < 0) slot = 0;
  pendingRrepAcks[slot].used = true;
  pendingRrepAcks[slot].neighbor = neighbor;
  pendingRrepAcks[slot].requestId = requestId;
  pendingRrepAcks[slot].sentAt = millis();
}

bool clearRrepAck(const String& neighbor, const String& requestId) {
  for (uint8_t i = 0; i < MAX_PENDING_RREP_ACKS; ++i) {
    if (pendingRrepAcks[i].used && pendingRrepAcks[i].neighbor == neighbor &&
        pendingRrepAcks[i].requestId == requestId) {
      pendingRrepAcks[i].used = false;
      updateNeighbor(neighbor);
      return true;
    }
  }
  return false;
}

// ============================= ROUTE TABLE ================================
// RFC-style 32-bit serial-number comparison (wrap-around safe).
bool sequenceIsNewer(uint32_t candidate, uint32_t current) {
  return static_cast<int32_t>(candidate - current) > 0;
}

bool routeMetricIsBetter(uint8_t energy, uint8_t hops,
                         uint8_t currentEnergy, uint8_t currentHops) {
  if (USE_EA_AODV && energy != currentEnergy) return energy > currentEnergy;
  return hops < currentHops;
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

bool getRoute(const String& destination, String& nextHop, String& path) {
  int i = findRouteIndex(destination, true);
  if (i < 0) return false;
  nextHop = routeTable[i].nextHop;
  path = routeTable[i].path;
  return nextHop.length() > 0;
}

void installRoute(const String& destination, const String& nextHop, const String& path,
                  uint8_t minEnergy, uint8_t hops,
                  uint32_t destinationSequence = 0, bool sequenceValid = true) {
  if (!destination.length() || !nextHop.length() || destination == NODE_ID) return;

  int slot = findRouteIndex(destination, false);
  if (slot >= 0) {
    RouteEntry& old = routeTable[slot];
    bool replace = false;
    if (sequenceValid && old.sequenceValid) {
      if (sequenceIsNewer(destinationSequence, old.destinationSequence)) {
        replace = true;
      } else if (destinationSequence == old.destinationSequence) {
        // Equal destination sequence: use EA metric; standard mode uses hop count.
        replace = routeMetricIsBetter(minEnergy, hops, old.minEnergy, old.hops);
      }
      // Older sequence numbers never overwrite a fresh route.
    } else if (sequenceValid && !old.sequenceValid) {
      replace = true;
    } else if (!old.sequenceValid) {
      replace = routeMetricIsBetter(minEnergy, hops, old.minEnergy, old.hops);
    }
    if (!replace) return;
  } else {
    for (uint8_t i = 0; i < MAX_ROUTE_ENTRIES; ++i) {
      if (!routeTable[i].used) { slot = i; break; }
    }
  }

  if (slot < 0) {
    // Keep bounded memory: evict the oldest route only when no free slot exists.
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
  routeTable[slot].destinationSequence = destinationSequence;
  routeTable[slot].sequenceValid = sequenceValid;
  routeTable[slot].precursors = "";
  routeTable[slot].updatedAt = millis();
  routeTable[slot].lifetimeMs = MY_ROUTE_TIMEOUT_MS;
  Serial.print("[ROUTE] "); Serial.print(NODE_ID);
  Serial.print(" -> "); Serial.print(destination);
  Serial.print(" via "); Serial.print(nextHop);
  Serial.print(" | seq="); Serial.print(destinationSequence);
  Serial.print(" | E_min="); Serial.print(minEnergy);
  Serial.print("% | hops="); Serial.println(hops);
}

bool precursorContains(const String& list, const String& id) {
  int start = 0;
  while (start < (int)list.length()) {
    int end = list.indexOf(',', start);
    if (end < 0) end = list.length();
    if (list.substring(start, end) == id) return true;
    start = end + 1;
  }
  return false;
}

void addPrecursor(const String& destination, const String& precursor) {
  if (!precursor.length() || precursor == String(NODE_ID)) return;
  int slot = findRouteIndex(destination, false);
  if (slot < 0 || precursorContains(routeTable[slot].precursors, precursor)) return;
  if (routeTable[slot].precursors.length()) routeTable[slot].precursors += ",";
  routeTable[slot].precursors += precursor;
}

String getPrecursorsForPath(const String& path, const String& destination) {
  for (uint8_t i = 0; i < MAX_ROUTE_ENTRIES; ++i)
    if (routeTable[i].used && routeTable[i].path == path &&
        routeTable[i].destination == destination) return routeTable[i].precursors;
  return "";
}

void invalidateRoute(const String& destination, uint32_t errorSequence = 0, bool sequenceValid = false) {
  for (uint8_t i = 0; i < MAX_ROUTE_ENTRIES; ++i) {
    RouteEntry& route = routeTable[i];
    if (!route.used || route.destination != destination) continue;
    // Ignore stale RERRs when a newer valid destination sequence is already known.
    if (sequenceValid && route.sequenceValid &&
        sequenceIsNewer(route.destinationSequence, errorSequence)) continue;
    route.used = false;
  }
}

void invalidateRoutesByPath(const String& path, const String& unreachableDestination,
                            uint32_t errorSequence) {
  for (uint8_t i = 0; i < MAX_ROUTE_ENTRIES; ++i) {
    RouteEntry& route = routeTable[i];
    if (!route.used || route.path != path) continue;
    // Sequence freshness applies to the unreachable destination itself. Other
    // entries sharing the broken physical path are invalidated as link-dependent routes.
    if (route.destination == unreachableDestination && route.sequenceValid &&
        sequenceIsNewer(route.destinationSequence, errorSequence)) continue;
    route.used = false;
  }
}

void processNeighborMaintenance() {
  unsigned long now = millis();
  for (uint8_t p = 0; p < MAX_PENDING_RREP_ACKS; ++p) {
    if (pendingRrepAcks[p].used && now - pendingRrepAcks[p].sentAt >= NEXT_HOP_WAIT_MS) {
      String failedNeighbor = pendingRrepAcks[p].neighbor;
      pendingRrepAcks[p].used = false;
      blacklistNeighbor(failedNeighbor);
      for (uint8_t r = 0; r < MAX_ROUTE_ENTRIES; ++r) {
        if (routeTable[r].used && routeTable[r].nextHop == failedNeighbor) {
          String broken = routeTable[r].path;
          uint32_t errorSeq = routeTable[r].sequenceValid ? routeTable[r].destinationSequence + 1 : 1;
          routeTable[r].destinationSequence = errorSeq;
          routeTable[r].sequenceValid = true;
          sendRouteError(broken, routeTable[r].destination, errorSeq);
          String failedDestination = routeTable[r].destination;
          routeTable[r].used = false;
          ++countRouteFailures;
          if (failedDestination == String(BASE_ID)) {
            routeDiscoveryInProgress = false;
            originSequenceAllocatedForDiscovery = false;
            currentRreqTtl = TTL_START;
            rreqMaxTtlCount = 0;
            routeDiscoveryExhausted = false;
            nextRreqRetryAt = now;
          }
        }
      }
    }
  }
  if (now - lastHelloAt >= HELLO_INTERVAL) {
    lastHelloAt = now; enqueueFrame(String("HELLO|") + NODE_ID, random(0, (long)HELLO_JITTER_MAX_MS + 1L));
  }
  for (uint8_t i=0;i<MAX_ROUTE_ENTRIES;i++) {
    if (!routeTable[i].used) continue;
    if (now - routeTable[i].updatedAt > routeTable[i].lifetimeMs) {
      routeTable[i].used = false;
      continue;
    }
    if (routeTable[i].nextHop.length() && !neighborAlive(routeTable[i].nextHop)) {
      Serial.print("[LINK] Next-hop tidak aktif: "); Serial.println(routeTable[i].nextHop);
      String broken = routeTable[i].path; String dest = routeTable[i].destination;
      uint32_t routeSeq = routeTable[i].sequenceValid ? routeTable[i].destinationSequence : 0;
      ++routeSeq; // AODV increments the unreachable destination sequence before RERR.
      routeTable[i].destinationSequence = routeSeq;
      routeTable[i].sequenceValid = true;
      if (dest == String(BASE_ID)) baseSequenceNumber = routeSeq;
      sendRouteError(broken, dest, routeSeq);
      routeTable[i].used = false;
      countRouteFailures++;
      if (dest == String(BASE_ID)) {
        routeDiscoveryInProgress = false;
        originSequenceAllocatedForDiscovery = false;
        currentRreqTtl = TTL_START;
        rreqMaxTtlCount = 0;
        routeDiscoveryExhausted = false;
        nextRreqRetryAt = now;
      }
    }
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

bool acceptBetterRreq(const String& key, const String& path, uint8_t energy, uint8_t hops) {
  unsigned long now=millis(); int freeSlot=-1, oldest=0;
  for (uint8_t i=0;i<MAX_SEEN_RREQ;i++) {
    if (bestRreq[i].used && now-bestRreq[i].at > RREQ_SEEN_LIFETIME) bestRreq[i].used=false;
    if (bestRreq[i].used && bestRreq[i].key==key) {
      bool better = USE_EA_AODV ? (energy>bestRreq[i].energy || (energy==bestRreq[i].energy && (hops<bestRreq[i].hops || (hops==bestRreq[i].hops && path<bestRreq[i].path)))) : (hops<bestRreq[i].hops || (hops==bestRreq[i].hops && path<bestRreq[i].path));
      if (!better) return false;
      bestRreq[i].energy=energy; bestRreq[i].hops=hops; bestRreq[i].path=path; bestRreq[i].at=now; return true;
    }
    if (!bestRreq[i].used && freeSlot<0) freeSlot=i;
    if (bestRreq[i].at < bestRreq[oldest].at) oldest=i;
  }
  int slot=freeSlot>=0?freeSlot:oldest; bestRreq[slot].used=true; bestRreq[slot].key=key;
  bestRreq[slot].energy=energy; bestRreq[slot].hops=hops; bestRreq[slot].path=path; bestRreq[slot].at=now; return true;
}

// ============================== DISCOVERY ================================
unsigned long calculateRreqRetryDelayMs(uint8_t ttl) {
  // Sama dengan ScheduleRreqRetry() pada AODV original.
  if (ttl < NET_DIAMETER) {
    return 2UL * NODE_TRAVERSAL_TIME_MS * (ttl + TIMEOUT_BUFFER);
  }
  const uint8_t backoffExponent = (rreqMaxTtlCount > 0) ? (rreqMaxTtlCount - 1) : 0;
  return NET_TRAVERSAL_TIME_MS * (1UL << backoffExponent);
}

unsigned long reserveRateLimitedSlot(unsigned long& nextAllowedAt, uint16_t limitPerSecond) {
  const unsigned long now = millis();
  if (nextAllowedAt < now) nextAllowedAt = now;
  const unsigned long delayMs = nextAllowedAt - now;
  const unsigned long spacing = (limitPerSecond > 0) ? (1000UL / limitPerSecond) : 1000UL;
  nextAllowedAt += (spacing ? spacing : 1UL);
  return delayMs;
}

void sendRouteRequest() {
  // Jika discovery sebelumnya habis RreqRetries, permintaan baru mulai dari TtlStart.
  if (routeDiscoveryExhausted) {
    currentRreqTtl = TTL_START;
    rreqMaxTtlCount = 0;
    routeDiscoveryExhausted = false;
  }

  const uint8_t ttlToSend = currentRreqTtl;
  if (ttlToSend == NET_DIAMETER) ++rreqMaxTtlCount;

  const uint8_t energy = USE_EA_AODV ? txGetResidualEnergyPercent() : 100;
  ++rreqId;
  if (!originSequenceAllocatedForDiscovery) {
    ++originSequenceNumber;
    originSequenceAllocatedForDiscovery = true;
  }
  ++countRouteDiscoveries;
  // Extended RREQ keeps the original nine fields and appends origin/destination
  // sequence numbers, allowing old diagnostic parsers to read the base fields.
  String frame = "RREQ|" + String(NODE_ID) + "|" + String(BASE_ID) + "|" +
                 String(rreqId) + "|" + String(NODE_ID) + "|0|" + String(energy) + "|" +
                 String(NODE_ID) + "|" + String(ttlToSend) + "|" +
                 String(originSequenceNumber) + "|" + String(baseSequenceNumber);

  routeDiscoveryInProgress = true;
  lastRreqAt = millis();
  const unsigned long rateDelay = reserveRateLimitedSlot(nextRreqAllowedAt, RREQ_RATE_LIMIT);
  routeDiscoveryStartedAt = lastRreqAt + rateDelay;
  const unsigned long retryDelay = calculateRreqRetryDelayMs(ttlToSend);
  nextRreqRetryAt = lastRreqAt + rateDelay + retryDelay;

  Serial.print("[RREQ] TTL="); Serial.print(ttlToSend);
  Serial.print(" retry_timer_ms="); Serial.print(retryDelay);
  Serial.print(" maxTTL_attempt="); Serial.print(rreqMaxTtlCount);
  Serial.print(" request#"); Serial.println(rreqId);
  enqueueFrame(frame, rateDelay);
}

bool isSimplePath(const String& path, uint8_t maxNodes) {
  uint8_t count = pathCount(path);
  if (count == 0 || count > maxNodes) return false;
  for (uint8_t i = 0; i < count; ++i) {
    String a = pathNodeAt(path, i);
    if (!a.length()) return false;
    for (uint8_t j = 0; j < i; ++j)
      if (a == pathNodeAt(path, j)) return false;
  }
  return true;
}

void handleRreq(String f[], uint8_t n) {
  // RREQ|origin|destination|requestId|previousHop|hops|minEnergy|path|ttl|originSeq|destSeq
  if (n < 9) return;
  String origin = f[1], destination = f[2], requestId = f[3];
  String previousHop = f[4];
  uint8_t hops = (uint8_t)constrain(f[5].toInt(), 0, 255);
  uint8_t energy = (uint8_t)constrain(f[6].toInt(), 0, 100);
  String path = f[7];
  int ttl = f[8].toInt();
  uint32_t originSeq = (n >= 10) ? (uint32_t)f[9].toInt() : 0;
  uint32_t requestedDestSeq = (n >= 11) ? (uint32_t)f[10].toInt() : 0;

  if (origin == NODE_ID || !origin.length() || destination != String(BASE_ID)) return;
  if (isNeighborBlacklisted(previousHop)) return;
  if (!isSimplePath(path, MAX_ROUTE_HOPS + 1) ||
      pathCount(path) != (uint8_t)(hops + 1) ||
      pathNodeAt(path, pathCount(path) - 1) != previousHop) return;
  if (pathContains(path, String(NODE_ID))) return;

  String metricKey = origin + "|" + requestId;
  uint8_t pathEnergy = USE_EA_AODV ? min8(energy, txGetResidualEnergyPercent()) : 100;
  uint8_t nextHops = (uint8_t)min(255, (int)hops + 1);
  if (!acceptBetterRreq(metricKey, path, pathEnergy, nextHops)) return;

  // AODV reverse route: learn how to reach the RREQ origin through previousHop.
  // The path string is retained as a route snapshot for the LoRa source-route format.
  if (previousHop.length() && originSeq) {
    String reverseSnapshot = appendPath(path, String(NODE_ID));
    installRoute(origin, previousHop, reverseSnapshot, pathEnergy, nextHops,
                 originSeq, true);
  }

  if (ttl <= 0 || hops >= MAX_ROUTE_HOPS) return;
  String forwardedPath = appendPath(path, String(NODE_ID));
  String frame = "RREQ|" + origin + "|" + destination + "|" + requestId + "|" +
                 String(NODE_ID) + "|" + String((uint8_t)(hops + 1)) + "|" +
                 String(pathEnergy) + "|" + forwardedPath + "|" + String(ttl - 1) + "|" +
                 String(originSeq) + "|" + String(max(requestedDestSeq, baseSequenceNumber));
  unsigned long now = millis();
  unsigned long jitter = (unsigned long)random(0, FORWARD_JITTER_MAX_MS + 1);
  unsigned long earliestForwardAt = lastRreqForwardAt + RREQ_FORWARD_MIN_INTERVAL;
  unsigned long minimumDelay = (lastRreqForwardAt && (long)(earliestForwardAt - now) > 0)
                                 ? (earliestForwardAt - now) : 0;
  unsigned long rateDelay = reserveRateLimitedSlot(nextRreqAllowedAt, RREQ_RATE_LIMIT);
  if (minimumDelay > jitter) jitter = minimumDelay;
  if (rateDelay > jitter) jitter = rateDelay;
  if (enqueueFrame(frame, jitter)) { countRreqForwarded++; lastRreqForwardAt = now + jitter; }
}

void handleRrep(String f[], uint8_t n) {
  // RREP|origin|destination|requestId|selectedPath|index|energy|ttl|destSeq|originSeq
  if (n < 8) return;
  String origin = f[1], destination = f[2], requestId = f[3], path = f[4];
  int index = f[5].toInt();
  uint8_t energy = (uint8_t)constrain(f[6].toInt(), 0, 100);
  int ttl = f[7].toInt();
  uint32_t destinationSeq = (n >= 9) ? (uint32_t)f[8].toInt() : 0;
  uint32_t originSeq = (n >= 10) ? (uint32_t)f[9].toInt() : 0;
  bool ackRequired = (n >= 11 && f[10] == "1");
  uint8_t nNodes = pathCount(path);
  if (nNodes < 2 || nNodes > MAX_ROUTE_HOPS + 1 || index < 0 || index >= nNodes) return;
  if (!isSimplePath(path, MAX_ROUTE_HOPS + 1)) return;
  if (pathNodeAt(path, 0) != origin || pathNodeAt(path, nNodes - 1) != destination) return;
  if (pathNodeAt(path, (uint8_t)index) != String(NODE_ID)) return;
  if (origin == String(NODE_ID) && n >= 10 && originSeq != originSequenceNumber) return;
  if (index > 0 && originSeq != 0) {
    int reverseIndex = findRouteIndex(origin, false);
    if (reverseIndex < 0) return; // No valid reverse route for this RREQ origin.
    if (routeTable[reverseIndex].sequenceValid &&
        sequenceIsNewer(routeTable[reverseIndex].destinationSequence, originSeq)) return;
  }
  if (destination == String(BASE_ID) && n >= 9 && baseSequenceNumber &&
      sequenceIsNewer(baseSequenceNumber, destinationSeq)) return;

  // AODV RREP-ACK is conditional: send it only when the previous hop requested it.
  if (index < nNodes - 1) {
    String replySender = pathNodeAt(path, (uint8_t)(index + 1));
    if (isNeighborBlacklisted(replySender)) return;
    if (ackRequired) {
      String ack = "RREP_ACK|" + String(NODE_ID) + "|" + replySender + "|" + requestId;
      transmitFrame(ack);
    }
    updateNeighbor(replySender);
  }

  uint8_t hops = nNodes - 1;
  if (index < nNodes - 1) {
    String nextTowardDestination = pathNodeAt(path, (uint8_t)(index + 1));
    updateNeighbor(nextTowardDestination);
    installRoute(destination, nextTowardDestination, path, energy, hops,
                 destinationSeq, n >= 9);
    if (index > 0) addPrecursor(destination, pathNodeAt(path, (uint8_t)(index - 1)));
  }
  if (index > 0) {
    String nextTowardOrigin = pathNodeAt(path, (uint8_t)(index - 1));
    installRoute(origin, nextTowardOrigin, path, energy, hops, originSeq, n >= 10);
    if (index < nNodes - 1) addPrecursor(origin, pathNodeAt(path, (uint8_t)(index + 1)));
  }
  if (destination == String(BASE_ID) && (baseSequenceNumber == 0 ||
      sequenceIsNewer(destinationSeq, baseSequenceNumber) || destinationSeq == baseSequenceNumber)) {
    baseSequenceNumber = destinationSeq;
  }

  if (String(NODE_ID) == origin) {
    routeDiscoveryInProgress = false;
    originSequenceAllocatedForDiscovery = false;
    currentRreqTtl = TTL_START; rreqMaxTtlCount = 0; routeDiscoveryExhausted = false;
    uint32_t discoveryMs = (uint32_t)(millis() - routeDiscoveryStartedAt);
    countRouteDiscoveryCompleted++; totalRouteDiscoveryMs += discoveryMs;
    Serial.print("[METRIC,DISCOVERY_TX] duration_ms="); Serial.println(discoveryMs);
    Serial.print("[RREP] Rute ke BASE siap melalui ");
    String nh, rp;
    if (getRoute(String(BASE_ID), nh, rp)) Serial.println(nh);
    if (pendingDataValid) {
      String payload = pendingDataPayload;
      pendingDataValid = false;
      pendingDataPayload = "";
      sendDataPayload(payload);
    }
    return;
  }

  if (ttl <= 0 || index <= 0) return;
  String upstreamNeighbor = pathNodeAt(path, (uint8_t)(index - 1));
  String forwarded = "RREP|" + origin + "|" + destination + "|" + requestId + "|" + path + "|" +
                     String(index - 1) + "|" + String(energy) + "|" + String(ttl - 1) + "|" +
                     String(destinationSeq) + "|" + String(originSeq) + "|1";
  if (transmitFrame(forwarded) == HW_RADIO_OK)
    trackRrepAck(upstreamNeighbor, requestId);
}

void checkSOS() {
  bool pressed = (txSosPressed());
  if (pressed != sosActive) {
    sosActive = pressed;
    Serial.println(sosActive ? "[SOS] TOMBOL SOS AKTIF! Mengirim DATA darurat segera." : "[SOS] Tombol SOS dilepas.");
    if (sosActive) sendDataPacket(); // Do not wait for the periodic 5-second telemetry timer.
  }
}

void checkBattery() {
  unsigned long now = millis();
  if (now - lastBatteryCheck < BATT_CHECK_INTERVAL) return;
  lastBatteryCheck = now;

  float v = txGetBatteryVoltage();
  float current_mA = txGetBatteryCurrentMilliAmps();
  float power_mW = txGetBatteryPowerMilliWatts();
  float pct = txCalculateResidualEnergyPercent(v);
  Serial.print("[BATT] V: "); Serial.print(v, 2);
  Serial.print(" V | I: "); Serial.print(current_mA, 1);
  Serial.print(" mA | P: "); Serial.print(power_mW, 1);
  Serial.print(" mW | SoC estimasi: "); Serial.print(pct, 0);
  Serial.println("%");

  bool low = pct < BATTERY_LOW_WARNING_PERCENT;
  if (low && !batteryLowWarning) {
    Serial.println("[BATT] Baterai < 20%! Menyalakan LED merah.");
    txLedRedPersistentOn();
    batteryLowWarning = true;
  } else if (!low && batteryLowWarning) {
    Serial.println("[BATT] Baterai pulih > 20%. Mematikan LED merah.");
    txLedRedOff();
    batteryLowWarning = false;
  }
}

unsigned long calculateAckTimeoutMs(const String& path) {
  // ACK end-to-end DATA merupakan ekstensi proyek, bukan AODV ACK.
  // Gunakan nilai NetTraversalTime original sebagai batas tunggu.
  (void)path;
  return DATA_ACK_TIMEOUT_MS;
}

void transmitDataFrame(const String& payload, bool isRetry = false) {
  // Keep one DATA transaction in flight. If another telemetry sample arrives,
  // retain the latest sample; never overwrite a pending SOS with normal telemetry.
  if (waitingAck && !isRetry) {
    String incoming[MAX_FIELDS]; String pending[MAX_FIELDS];
    uint8_t inN = splitFields(payload, incoming, MAX_FIELDS);
    uint8_t pn = pendingDataValid ? splitFields(pendingDataPayload, pending, MAX_FIELDS) : 0;
    bool pendingIsSos = (pn >= 8 && pending[7] == "1");
    bool incomingIsSos = (inN >= 8 && incoming[7] == "1");
    if (!pendingDataValid || incomingIsSos || !pendingIsSos) {
      pendingDataPayload = payload;
      pendingDataValid = true;
    }
    return;
  }
  String nextHop, path;
  if (!getRoute(String(BASE_ID), nextHop, path)) {
    pendingDataPayload = payload;
    pendingDataValid = true;
    if (!routeDiscoveryInProgress) sendRouteRequest();
    Serial.println("[DATA] Belum ada rute; data disimpan sementara menunggu RREP.");
    return;
  }
  String f[MAX_FIELDS];
  uint8_t n = splitFields(payload, f, MAX_FIELDS);
  if (n < 10) return;
  if (n < 12) {
    f[10] = nextHop;
    f[11] = String(MAX_ROUTE_TTL);
    n = 12;
  } else {
    f[10] = nextHop;
    f[11] = String(MAX_ROUTE_TTL);
  }
  String outgoing = joinFields(f, n);
  Serial.print(isRetry ? "[DATA-RETRY] " : "[DATA] ");
  Serial.print("Tx #"); Serial.print(f[2]); Serial.print(" -> ");
  Serial.print(BASE_ID); Serial.print(" via "); Serial.print(nextHop);
  Serial.print(": "); Serial.println(outgoing);

  int state = transmitFrame(outgoing);
  if (state == HW_RADIO_OK) {
    waitingAck = true;
    ackWaitStart = millis();
    ackExpectedSeq = (uint32_t)f[2].toInt();
    waitingSOSAck = f[7] == "1";
    lastSentPath = path;
    lastSentPayload = outgoing;
    currentAckTimeoutMs = calculateAckTimeoutMs(path);
    if (pendingDataValid && pendingDataPayload == payload) {
      pendingDataValid = false;
      pendingDataPayload = "";
    }
  } else {
  }
}

void sendDataPayload(const String& payload) { transmitDataFrame(payload, true); }

void sendDataPacket() {
  float latitude = 0.0f;
  float longitude = 0.0f;
  const bool gpsValid = txGetGpsLocation(latitude, longitude);
  String latStr = gpsValid ? String(latitude, 6) : "0.000000";
  String lngStr = gpsValid ? String(longitude, 6) : "0.000000";
  float batteryVoltage = txGetBatteryVoltage();
  uint8_t batteryPercent = txCalculateResidualEnergyPercent(batteryVoltage);
  String payload = String(NODE_ID) + "|" + String(BASE_ID) + "|" + String(seqNumber) + "|" +
                   latStr + "|" + lngStr + "|" + String(batteryVoltage, 2) + "|" +
                   String(batteryPercent) + "|" + String(sosActive ? 1 : 0) +
                   "|0|" + String(NODE_ID) + "||" + String(MAX_ROUTE_TTL);
  seqNumber++;
  transmitDataFrame(payload, false);
}

void handleData(String f[], uint8_t n);

bool queuePacketUntilRoute(const String& frame) {
  String fields[MAX_FIELDS];
  uint8_t n = splitFields(frame, fields, MAX_FIELDS);
  if (n < 3) return false;
  String key = fields[0] + "|" + fields[2];
  int freeSlot = -1;
  int oldestSlot = -1;
  for (uint8_t i = 0; i < MAX_PENDING_ROUTE_PACKETS; ++i) {
    if (pendingRouteQueue[i].used && pendingRouteQueue[i].key == key) return true;
    if (!pendingRouteQueue[i].used && freeSlot < 0) freeSlot = i;
    if (pendingRouteQueue[i].used &&
        (oldestSlot < 0 || (long)(pendingRouteQueue[i].enqueuedAt -
                                  pendingRouteQueue[oldestSlot].enqueuedAt) < 0))
      oldestSlot = i;
  }
  int slot = freeSlot;
  if (slot < 0) {
    // Bounded AODV request queue: evict the oldest packet when full.
    slot = oldestSlot;
    if (slot < 0) return false;
    Serial.println("[QUEUE] Pending-route queue penuh; paket tertua dibuang.");
  }
  pendingRouteQueue[slot].used = true;
  pendingRouteQueue[slot].frame = frame;
  pendingRouteQueue[slot].key = key;
  pendingRouteQueue[slot].enqueuedAt = millis();
  return true;
}

void processPendingRouteQueue() {
  const unsigned long now = millis();
  const bool routeReady = findRouteIndex(String(BASE_ID), false) >= 0;
  for (uint8_t i = 0; i < MAX_PENDING_ROUTE_PACKETS; ++i) {
    PendingRoutePacket& entry = pendingRouteQueue[i];
    if (!entry.used) continue;
    if (now - entry.enqueuedAt > MAX_QUEUE_TIME_MS) {
      entry.used = false;
      entry.frame = "";
      entry.key = "";
      Serial.println("[QUEUE] Paket tanpa rute kedaluwarsa dan dibuang.");
      continue;
    }
    if (!routeReady) continue;
    String frame = entry.frame;
    entry.used = false;
    entry.frame = "";
    entry.key = "";
    String fields[MAX_FIELDS];
    uint8_t n = splitFields(frame, fields, MAX_FIELDS);
    if (n >= 10) handleData(fields, n);
  }
}

void handleData(String f[], uint8_t n) {
  // Legacy compatible payload:
  // src|dst|seq|lat|lng|battV|battPct|sos|hops|path|nextHop|ttl
  if (n < 10) return;
  String src = f[0], dst = f[1];
  uint32_t seq = (uint32_t)f[2].toInt();
  if (!src.length() || !dst.length()) return;

  // Every hiker TX is also a relay: forward frames explicitly addressed to this node as next hop.
  if (dst == String(NODE_ID)) return;
  if (n >= 12 && f[10] != String(NODE_ID)) return;
  if (n >= 12 && f[11].toInt() <= 0) {
    Serial.println("[FORWARD] TTL DATA habis; paket dibuang.");
    return;
  }
  if (pathContains(f[9], String(NODE_ID))) {
    Serial.println("[FORWARD] Loop terdeteksi pada path DATA; paket dibuang.");
    return;
  }

  String nextHop, routePath;
  if (!getRoute(dst, nextHop, routePath)) {
    String waitingFrame = joinFields(f, n);
    if (queuePacketUntilRoute(waitingFrame)) {
      Serial.print("[QUEUE] DATA menunggu rute ke "); Serial.println(dst);
      if (!routeDiscoveryInProgress) sendRouteRequest();
    } else {
      Serial.println("[QUEUE] DATA gagal dimasukkan ke pending-route queue.");
    }
    return;
  }
  uint8_t previousCount = pathCount(f[9]);
  if (previousCount > 0) addPrecursor(dst, pathNodeAt(f[9], previousCount - 1));
  uint8_t ttl = (n >= 12) ? (uint8_t)constrain(f[11].toInt(), 0, 255) : MAX_ROUTE_TTL;
  if (ttl == 0) return;
  uint8_t hops = (uint8_t)constrain(f[8].toInt(), 0, 254);
  f[8] = String(hops + 1);
  f[9] = appendPath(f[9], String(NODE_ID));
  f[10] = nextHop;
  f[11] = String(ttl - 1);
  String forwarded = joinFields(f, 12);
  Serial.print("[FORWARD] "); Serial.print(src); Serial.print(" seq#");
  Serial.print(seq); Serial.print(" -> "); Serial.print(dst);
  Serial.print(" via "); Serial.println(nextHop);
  enqueueFrame(forwarded);
}

// ================================= ACK ====================================
void handleAck(String f[], uint8_t n) {
  // Old 4-field ACK remains accepted; routed ACK has nextHop and TTL appended.
  String src = f[0], dst = f[1];
  uint32_t seq = (uint32_t)f[3].toInt();
  if (dst == String(NODE_ID)) {
    if (src != String(BASE_ID)) return;
    if (waitingAck && seq == ackExpectedSeq) {
      Serial.print("[ACK] Diterima dari basecamp, seq#"); Serial.println(seq);
       if (waitingSOSAck) {
        txLedGreenTripleBlink();
        Serial.println("[ACK] SOS sampai di basecamp!");
      }
      totalAckRttMs += (uint32_t)(millis() - ackWaitStart); countAckSuccess++;
      waitingAck = false;
      waitingSOSAck = false;
      if (pendingDataValid) {
        String nextPayload = pendingDataPayload;
        pendingDataValid = false;
        pendingDataPayload = "";
        sendDataPayload(nextPayload);
      }
    }
    return;
  }

  if (n < 6 || f[4] != String(NODE_ID) || f[5].toInt() <= 0) return;
  String nextHop, path;
  if (!getRoute(dst, nextHop, path)) return;
  f[4] = nextHop;
  f[5] = String(f[5].toInt() - 1);
  enqueueFrame(joinFields(f, n));
}

void handleRrepAck(String f[], uint8_t n) {
  // RREP_ACK|sender|receiver|requestId (one-hop control confirmation).
  if (n < 4 || f[2] != String(NODE_ID)) return;
  clearRrepAck(f[1], f[3]);
}

void sendRouteError(const String& path, const String& destination, uint32_t seq) {
  if (!path.length() || !destination.length()) return;
  String recipients = getPrecursorsForPath(path, destination);
  // Legacy path fallback keeps older firmware able to process the error.
  if (!recipients.length()) recipients = path;
  String frame = "RERR|" + String(NODE_ID) + "|" + destination + "|" + path + "|" +
                 String(seq) + "|" + String(MAX_ROUTE_TTL) + "|" + recipients;
  unsigned long delayMs = reserveRateLimitedSlot(nextRerrAllowedAt, RERR_RATE_LIMIT);
  enqueueFrame(frame, delayMs);
}

bool rerrTargetsThisNode(const String& recipients) {
  if (recipients.indexOf('>') >= 0) return pathContains(recipients, String(NODE_ID));
  return precursorContains(recipients, String(NODE_ID));
}

void handleRerr(String f[], uint8_t n) {
  // RERR|reporter|destination|brokenPath|destinationSeq|ttl|precursorList
  if (n < 6) return;
  String reporter = f[1], destination = f[2], brokenPath = f[3];
  uint32_t seq = (uint32_t)f[4].toInt();
  int ttl = f[5].toInt();
  String recipients = (n >= 7) ? f[6] : brokenPath;
  String key = reporter + "|" + destination + "|" + brokenPath + "|" + String(seq);
  if (!rememberKey(seenRerr, MAX_SEEN_RERR, key, RREQ_SEEN_LIFETIME)) return;
  if (!rerrTargetsThisNode(recipients)) return;

  String upstream = getPrecursorsForPath(brokenPath, destination);
  invalidateRoutesByPath(brokenPath, destination, seq);
  invalidateRoute(destination, seq, true);
  if (destination == String(BASE_ID) &&
      (baseSequenceNumber == 0 || sequenceIsNewer(seq, baseSequenceNumber) || seq == baseSequenceNumber))
    baseSequenceNumber = seq;

  // Propagate only to this node's own precursors (AODV precursor semantics).
  if (ttl > 0 && upstream.length()) {
    String forwarded = "RERR|" + reporter + "|" + destination + "|" + brokenPath + "|" +
                       String(seq) + "|" + String(ttl - 1) + "|" + upstream;
    unsigned long delayMs = reserveRateLimitedSlot(nextRerrAllowedAt, RERR_RATE_LIMIT);
    enqueueFrame(forwarded, delayMs);
  }

  if (destination == String(BASE_ID)) {
    routeDiscoveryInProgress = false;
    originSequenceAllocatedForDiscovery = false;
    currentRreqTtl = TTL_START;
    rreqMaxTtlCount = 0;
    routeDiscoveryExhausted = false;
    nextRreqRetryAt = millis();
  }
}

// ============================ PACKET DISPATCH =============================
void handlePacket(const String& packet) {
  if (!packet.length()) return;
  String f[MAX_FIELDS];
  uint8_t n = splitFields(packet, f, MAX_FIELDS);
  if (n == 0) return;

  if (f[0] == "HELLO" && n >= 2) { updateNeighbor(f[1]); return; }
  if (f[0] == "RREQ" && n >= 5) updateNeighbor(f[4]);
  if (f[0] == "RREQ") { handleRreq(f, n); return; }
  if (f[0] == "RREP") { handleRrep(f, n); return; }
  if (f[0] == "RREP_ACK") { handleRrepAck(f, n); return; }
  if (f[0] == "RERR") { handleRerr(f, n); return; }
  if (n >= 4 && f[2] == "ACK") { updateNeighbor(f[0]); handleAck(f, n); return; }
  if (n >= 10) { uint8_t pc = pathCount(f[9]); if (pc > 0) updateNeighbor(pathNodeAt(f[9], pc - 1)); }
  handleData(f, n);
}

void pollRadio() {
  String packet;
  int state = HW_RADIO_OK;
  if (!txPollRadio(packet, state)) return;

  if (state == HW_RADIO_OK && packet.length()) {
    handlePacket(packet);
  } else if (state == HW_RADIO_CRC_MISMATCH) {
    Serial.println("[RADIO] CRC mismatch; paket diabaikan.");
  }
}

// ================================ APP API =================================
void routerBegin() {
  Serial.begin(115200);
  randomSeed((uint32_t)micros() ^ (uint32_t)analogRead(34));
  delay(500);
  Serial.println();
  Serial.println("=== LoRa EA-AODV Node TX (SOURCE + RELAY) ===");
  Serial.print("ID: "); Serial.println(NODE_ID);
  Serial.println("Role: SOURCE + RELAY");
  Serial.print("Base ID: "); Serial.println(BASE_ID);
  Serial.print("[ROUTING] Mode: ");
  Serial.println(USE_EA_AODV
                   ? "EA-AODV (max-min energy, hop tie-break)"
                   : "AODV standard (minimum hop-count)");

  if (!txHardwareBegin()) {
    // Sama seperti sketch asal: LED error dipertahankan dan node berhenti.
    while (true) {
      txLedUpdate();
      delay(10);
    }
  }

  // Discovery awal mengikuti expanding-ring AODV original (TTL 1, lalu 3, 5, 7, 35).
  sendRouteRequest();
  Serial.println("[INIT] LoRa siap.");
}

void routerUpdate() {
  unsigned long now = millis();
  pollRadio();
  processTxQueue();
  processNeighborMaintenance();
  processPendingRouteQueue();
  txLedUpdate();
  // Perbarui waktu setelah transmisi RadioLib yang sinkron.
  now = millis();

  txGpsUpdate();
  checkSOS();
  checkBattery();

  if (waitingAck && now - ackWaitStart >= currentAckTimeoutMs) {
    countAckTimeout++;
    Serial.print("[ACK] Timeout #"); Serial.print(countAckTimeout);
    Serial.print(" after "); Serial.print(currentAckTimeoutMs); Serial.println(" ms; route invalidated.");
    if (waitingSOSAck) txLedRedBlink();
    waitingAck = false;
    waitingSOSAck = false;

    // Pertahankan frame yang belum ACK untuk dikirim setelah rute baru tersedia.
    if (lastSentPayload.length()) {
      bool preservePending = false;
      String sentFields[MAX_FIELDS];
      uint8_t sentCount = splitFields(lastSentPayload, sentFields, MAX_FIELDS);
      bool lastSentWasSos = (sentCount >= 8 && sentFields[7] == "1");
      uint32_t lastSentSeq = (sentCount >= 3) ? (uint32_t)sentFields[2].toInt() : 0;
      if (pendingDataValid) {
        String pendingFields[MAX_FIELDS];
        uint8_t pendingCount = splitFields(pendingDataPayload, pendingFields, MAX_FIELDS);
        bool pendingIsSos = (pendingCount >= 8 && pendingFields[7] == "1");
        uint32_t pendingSeq = (pendingCount >= 3) ? (uint32_t)pendingFields[2].toInt() : 0;
        preservePending = (pendingIsSos && !lastSentWasSos) || (pendingSeq > lastSentSeq);
      }
      if (!preservePending) {
        pendingDataPayload = lastSentPayload;
        pendingDataValid = true;
      }
    }

    if (lastSentPath.length()) {
      int failedRoute = findRouteIndex(String(BASE_ID), false);
      uint32_t routeSeq = (failedRoute >= 0 && routeTable[failedRoute].sequenceValid)
                            ? routeTable[failedRoute].destinationSequence : baseSequenceNumber;
      ++routeSeq;
      baseSequenceNumber = routeSeq;
      if (failedRoute >= 0) {
        routeTable[failedRoute].destinationSequence = routeSeq;
        routeTable[failedRoute].sequenceValid = true;
      }
      sendRouteError(lastSentPath, String(BASE_ID), routeSeq);
    }
    invalidateRoute(String(BASE_ID));
    routeDiscoveryInProgress = false;
    originSequenceAllocatedForDiscovery = false;
    currentRreqTtl = TTL_START;
    rreqMaxTtlCount = 0;
    routeDiscoveryExhausted = false;
    nextRreqRetryAt = now;
  }

  if (findRouteIndex(String(BASE_ID), false) < 0 &&
      !routeDiscoveryExhausted && (long)(now - nextRreqRetryAt) >= 0) {
    if (routeDiscoveryInProgress) {
      routeDiscoveryInProgress = false;
      if (currentRreqTtl < NET_DIAMETER) {
        const uint16_t nextTtl = (uint16_t)currentRreqTtl + TTL_INCREMENT;
        currentRreqTtl = (nextTtl > TTL_THRESHOLD) ? NET_DIAMETER : (uint8_t)nextTtl;
        Serial.print("[RREQ] Ring timeout; next TTL="); Serial.println(currentRreqTtl);
      } else if (rreqMaxTtlCount >= RREQ_RETRIES) {
        routeDiscoveryExhausted = true;
        originSequenceAllocatedForDiscovery = false;
        ++countRouteFailures;
        Serial.println("[RREQ] Discovery failed after RreqRetries at NetDiameter.");
      }
    }
    if (!routeDiscoveryInProgress && !routeDiscoveryExhausted) sendRouteRequest();
  }

  if (pendingDataValid && !waitingAck && !routeDiscoveryInProgress &&
      findRouteIndex(String(BASE_ID), false) >= 0) {
    String nextPayload = pendingDataPayload;
    pendingDataValid = false;
    pendingDataPayload = "";
    sendDataPayload(nextPayload);
  }

  static unsigned long lastRouteStats = 0;
  if (now - lastRouteStats >= 30000UL) {
    lastRouteStats = now;
    Serial.print("[METRIC,TX]"); Serial.print(",node="); Serial.print(NODE_ID);
    Serial.print(",rreq_forwarded="); Serial.print(countRreqForwarded);
    Serial.print(",data_forwarded="); Serial.print(countDataForwarded);
    Serial.print(",ack_timeout="); Serial.print(countAckTimeout);
    Serial.print(",discoveries="); Serial.print(countRouteDiscoveries);
    Serial.print(",route_failures="); Serial.print(countRouteFailures);
    Serial.print(",ack_success="); Serial.print(countAckSuccess);
    Serial.print(",avg_ack_rtt_ms="); Serial.print(countAckSuccess ? (uint32_t)(totalAckRttMs / countAckSuccess) : 0);
    Serial.print(",avg_discovery_ms="); Serial.println(countRouteDiscoveryCompleted ? (uint32_t)(totalRouteDiscoveryMs / countRouteDiscoveryCompleted) : 0);
  }

  if (now - lastSend >= SEND_INTERVAL) {
    lastSend = now;
    sendDataPacket();
  }
}