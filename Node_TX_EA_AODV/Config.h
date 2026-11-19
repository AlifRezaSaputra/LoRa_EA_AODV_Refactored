#ifndef TX_CONFIG_H
#define TX_CONFIG_H

#include <Arduino.h>

// ======================== PER-DEVICE CONFIGURATION ========================
// Ganti NODE_ID agar unik pada setiap node SOURCE + RELAY.
static const char NODE_ID[] = "NODE01";
static const char BASE_ID[] = "BASE";

// true: EA-AODV (max-min residual energy, lalu hop terkecil)
// false: AODV (hop terkecil). Nilai harus sama di seluruh jaringan.
static const bool USE_EA_AODV = true;

// ============================== PIN CONFIG ================================
#define GPS_RX 16
#define GPS_TX 17
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define LORA_DIO0 26
#define LORA_RST 14
#define LORA_DIO1 33
#define SOS_BUTTON_PIN 27
#define LED_RED 12
#define LED_GREEN 15
#define I2C_SDA 21
#define I2C_SCL 22

// ============================= RADIO CONFIG ===============================
static const float LORA_FREQ = 923.0;
static const float LORA_BW = 125.0;
static const uint8_t LORA_SF = 7;
static const uint8_t LORA_CR = 5;       // RadioLib: 5 = coding rate 4/5
static const uint8_t LORA_SYNCWORD = 0x12;
static const int8_t LORA_POWER = 10;
static const uint16_t LORA_PREAMBLE = 8;

// ======================= AODV ORIGINAL DEFAULT TIMERS =====================
// Nilai mengikuti default ns-3 AODV. Perhitungan retry RREQ dilakukan
// berdasarkan TTL dan NetTraversalTime di Router.cpp.
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

// Nilai yang digunakan langsung oleh sketch.
static const uint8_t RREQ_INITIAL_TTL = TTL_START;
static const uint8_t RREQ_RING_MAX_TTL = NET_DIAMETER;
static const uint32_t RREQ_SEEN_LIFETIME = PATH_DISCOVERY_TIME_MS;
static const uint32_t HELLO_INTERVAL = HELLO_INTERVAL_MS;
static const uint32_t NEIGHBOR_TIMEOUT = NEIGHBOR_TIMEOUT_MS;
static const uint8_t MAX_ROUTE_TTL = NET_DIAMETER;

// Timer aplikasi/perangkat keras yang bukan timer AODV.
static const unsigned long SEND_INTERVAL = 5000UL;
static const unsigned long LED_DURATION = 3000UL;

// ACK DATA end-to-end adalah ekstensi aplikasi, bukan timer AODV.
// Dipakai NetTraversalTime sebagai batas tunggu konservatif tanpa angka baru.
static const uint32_t DATA_ACK_TIMEOUT_MS = NET_TRAVERSAL_TIME_MS;
static const unsigned long FORWARD_JITTER_MAX_MS = 10UL; // AODV: random 0..10 ms
static const unsigned long RREQ_FORWARD_MIN_INTERVAL = 0UL;
static const float BATT_LOW_THRESHOLD = 20.0;
static const unsigned long BATT_CHECK_INTERVAL = 10000;

// ================================= LIMITS ==================================
static const uint8_t MAX_ROUTE_HOPS = NET_DIAMETER;
static const uint8_t MAX_ROUTE_ENTRIES = 16;
static const uint8_t MAX_SEEN_RREQ = 48;
static const uint8_t MAX_SEEN_RERR = 16;
static const uint8_t MAX_TX_QUEUE = 24;
static const uint8_t MAX_FIELDS = 16;

// ======================== INA219 / 2S BATTERY ==============================
static const float BATTERY_CAPACITY_AH = 2.2f;
static const float BATTERY_FULL_VOLTAGE = 8.40f;
static const float BATTERY_EMPTY_EST_VOLTAGE = 6.40f;
static const float BATTERY_LOW_WARNING_PERCENT = 20.0f;
static const float BATTERY_EMA_ALPHA = 0.15f;

#endif // TX_CONFIG_H
