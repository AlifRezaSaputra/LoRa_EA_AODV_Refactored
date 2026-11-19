#ifndef TX_CONFIG_H
#define TX_CONFIG_H

#include <Arduino.h>

// ======================== PER-DEVICE CONFIGURATION ========================
// Ganti NODE_ID agar unik pada setiap node SOURCE + RELAY ("NODE01", "NODE02", dst).
static const char NODE_ID[] = "NODE01";
static const char BASE_ID[] = "BASE";

// true: EA-AODV (max-min residual energy, lalu hop terkecil)
// false: AODV (hop terkecil). Nilai harus sama di seluruh jaringan.
static const bool USE_EA_AODV = true;

// Boot session token 32-bit (probabilistic global uniqueness generated once via esp_random())
extern uint32_t globalBootId;


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

// ============================== TIMEOUTS ==================================
static const unsigned long SEND_INTERVAL = 5000;
static const unsigned long LED_DURATION = 3000;
static const unsigned long ACK_TIMEOUT_BASE_MS = 1800;
static const unsigned long ACK_TIMEOUT_PER_HOP_MS = 750;
static const unsigned long ACK_TIMEOUT_MAX_MS = 7000;
static const unsigned long ROUTE_LIFETIME = 60000;
static const unsigned long RREQ_RETRY_INTERVAL = 6000;
static const unsigned long RREQ_MAX_BACKOFF = 24000;
static const unsigned long FORWARD_JITTER_MAX_MS = 120;
static const unsigned long RREQ_FORWARD_MIN_INTERVAL = 30;
static const unsigned long HELLO_INTERVAL = 60000;
static const unsigned long NEIGHBOR_TIMEOUT = 180000;
static const uint8_t RREQ_INITIAL_TTL = 6;
static const uint8_t RREQ_RING_MAX_TTL = 6;
static const unsigned long RREQ_SEEN_LIFETIME = 30000;
static const float BATT_LOW_THRESHOLD = 20.0;
static const unsigned long BATT_CHECK_INTERVAL = 10000;

// ================================= LIMITS ==================================
static const uint8_t MAX_ROUTE_TTL = 6;
static const uint8_t MAX_ROUTE_HOPS = 6;
static const uint8_t MAX_ROUTE_ENTRIES = 16;
static const uint8_t MAX_SEEN_RREQ = 48;
static const uint8_t MAX_SEEN_RERR = 16;
static const uint8_t MAX_TX_QUEUE = 24;
static const uint8_t MAX_FIELDS = 16;

// ======================== INA219 / 2S BATTERY ==============================
static const float BATTERY_CAPACITY_AH = 3.0f; // Baterai 2S 3.7V 3000mAh
static const float BATTERY_FULL_VOLTAGE = 8.40f;
static const float BATTERY_EMPTY_EST_VOLTAGE = 6.40f;
static const float BATTERY_LOW_WARNING_PERCENT = 20.0f;
static const float BATTERY_EMA_ALPHA = 0.15f;

#endif // TX_CONFIG_H