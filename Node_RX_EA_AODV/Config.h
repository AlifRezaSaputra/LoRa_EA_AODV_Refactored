#ifndef RX_CONFIG_H
#define RX_CONFIG_H

#include <Arduino.h>

// ======================== PER-DEVICE CONFIGURATION ========================
static const char NODE_ID[] = "BASE";
static const char BASE_ID[] = "BASE";
static const bool USE_EA_AODV = true;

// Boot session token 32-bit (probabilistic global uniqueness generated once via esp_random())
extern uint32_t globalBootId;


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

// ============================== TIMEOUTS ==================================
static const unsigned long RX_TIMEOUT = 100;
static const unsigned long STATUS_INTERVAL = 5000;
static const unsigned long SOS_ALERT_DURATION = 5000;
static const unsigned long ROUTE_LIFETIME = 60000;
static const unsigned long RREQ_SEEN_LIFETIME = 30000;
static const unsigned long ROUTE_DISCOVERY_WINDOW = 900;
static const unsigned long HELLO_INTERVAL = 60000;
static const unsigned long NEIGHBOR_TIMEOUT = 180000;

// ================================= LIMITS ==================================
static const uint8_t MAX_ROUTE_TTL = 6;
static const uint8_t MAX_ROUTE_ENTRIES = 4;
static const uint8_t MAX_SEEN_RREQ = 48;
static const uint8_t MAX_SEEN_DATA = 32;
static const uint8_t MAX_SEEN_RERR = 16;
static const uint8_t MAX_DISCOVERY_SLOTS = 4;
static const uint8_t MAX_CANDIDATES = 6;
static const uint8_t MAX_FIELDS = 16;

#endif // RX_CONFIG_H
