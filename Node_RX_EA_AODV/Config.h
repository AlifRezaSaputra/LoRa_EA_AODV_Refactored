#ifndef RX_CONFIG_H
#define RX_CONFIG_H

#include <Arduino.h>

// ======================== PER-DEVICE CONFIGURATION ========================
static const char NODE_ID[] = "BASE";
static const char BASE_ID[] = "BASE";
static const bool USE_EA_AODV = true;

// ============================== PIN CONFIG ================================
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define LORA_DIO0 26
#define LORA_RST 14
#define LORA_DIO1 33
#define BUZZER_PIN 25
#define LED_BASE 2

// ============================= RADIO CONFIG ===============================
static const float LORA_FREQ = 923.0;
static const float LORA_BW = 125.0;
static const uint8_t LORA_SF = 7;
static const uint8_t LORA_CR = 5;       // RadioLib: 5 = coding rate 4/5
static const uint8_t LORA_SYNCWORD = 0x12;
static const int8_t LORA_POWER = 10;
static const uint16_t LORA_PREAMBLE = 8;

// ======================= AODV ORIGINAL DEFAULT TIMERS =====================
// Nilai mengikuti default ns-3 AODV (aodv-routing-protocol.cc).
static const uint32_t NODE_TRAVERSAL_TIME_MS = 40UL;
static const uint8_t NET_DIAMETER = 35;
static const uint8_t TTL_START = 1;
static const uint8_t TTL_INCREMENT = 2;
static const uint8_t TTL_THRESHOLD = 7;
static const uint8_t TIMEOUT_BUFFER = 2;
static const uint8_t RREQ_RETRIES = 2;
static const uint16_t RREQ_RATE_LIMIT = 10;
static const uint16_t RERR_RATE_LIMIT = 10;
static const uint32_t ACTIVE_ROUTE_TIMEOUT_MS = 3000UL;
static const uint32_t HELLO_INTERVAL_MS = 1000UL;
static const uint8_t ALLOWED_HELLO_LOSS = 2;
static const uint32_t NET_TRAVERSAL_TIME_MS = 2UL * NODE_TRAVERSAL_TIME_MS * NET_DIAMETER;
static const uint32_t PATH_DISCOVERY_TIME_MS = 2UL * NET_TRAVERSAL_TIME_MS;
static const uint32_t MY_ROUTE_TIMEOUT_MS = 2UL * ((PATH_DISCOVERY_TIME_MS > ACTIVE_ROUTE_TIMEOUT_MS)
                                                        ? PATH_DISCOVERY_TIME_MS
                                                        : ACTIVE_ROUTE_TIMEOUT_MS);
static const uint32_t NEXT_HOP_WAIT_MS = NODE_TRAVERSAL_TIME_MS + 10UL;
static const uint32_t BLACKLIST_TIMEOUT_MS = RREQ_RETRIES * NET_TRAVERSAL_TIME_MS;
static const uint32_t DELETE_PERIOD_MS = 5UL * ((ACTIVE_ROUTE_TIMEOUT_MS > HELLO_INTERVAL_MS)
                                                     ? ACTIVE_ROUTE_TIMEOUT_MS
                                                     : HELLO_INTERVAL_MS);
static const uint32_t MAX_QUEUE_TIME_MS = 30000UL;
static const uint32_t NEIGHBOR_TIMEOUT_MS = ALLOWED_HELLO_LOSS * HELLO_INTERVAL_MS;

static const uint8_t RREQ_INITIAL_TTL = TTL_START;
static const uint8_t RREQ_RING_MAX_TTL = NET_DIAMETER;
static const uint32_t RREQ_SEEN_LIFETIME = PATH_DISCOVERY_TIME_MS;
static const uint32_t HELLO_INTERVAL = HELLO_INTERVAL_MS;
static const uint32_t NEIGHBOR_TIMEOUT = NEIGHBOR_TIMEOUT_MS;
static const uint8_t MAX_ROUTE_TTL = NET_DIAMETER;

// Timer layanan Basecamp, bukan timer AODV. Dipakai untuk mengumpulkan
// kandidat jalur Top-K sebelum Basecamp mengirim RREP.
static const unsigned long ROUTE_DISCOVERY_WINDOW = 900UL;
static const unsigned long RX_TIMEOUT = 100UL;
static const unsigned long STATUS_INTERVAL = 5000UL;
static const unsigned long SOS_ALERT_DURATION = 5000UL;

// ================================= LIMITS ==================================
static const uint8_t MAX_ROUTE_ENTRIES = 4;
static const uint8_t MAX_SEEN_RREQ = 48;
static const uint8_t MAX_SEEN_DATA = 32;
static const uint8_t MAX_SEEN_RERR = 16;
static const uint8_t MAX_DISCOVERY_SLOTS = 4;
static const uint8_t MAX_CANDIDATES = 6;
static const uint8_t MAX_FIELDS = 16;

#endif // RX_CONFIG_H
