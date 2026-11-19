#include "Hardware.h"

#include <SPI.h>
#include <RadioLib.h>

#include "Config.h"

namespace {
  SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1);
  volatile bool rxFlag = false;
  bool normalLedPulse = false;
  unsigned long normalLedPulseStart = 0;

  void onReceiveFlag() {
    rxFlag = true;
  }

  void startRadioReceive() {
    const int state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE) {
      Serial.print("[RADIO] Gagal masuk receive mode: ");
      Serial.println(state);
    }
  }
}

bool rxHardwareBegin() {
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_BASE, OUTPUT);
  digitalWrite(LED_BASE, LOW);
  noTone(BUZZER_PIN);

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  const int state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                                LORA_SYNCWORD, LORA_POWER, LORA_PREAMBLE);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[INIT] LoRa gagal dimulai. Kode: ");
    Serial.println(state);
    digitalWrite(LED_BASE, HIGH);
    return false;
  }

  radio.setCRC(true);
  radio.setDio0Action(onReceiveFlag, RISING);
  startRadioReceive();
  tone(BUZZER_PIN, 1500, 300);
  return true;
}

int rxTransmitFrame(const String& frame) {
  String txFrame = frame;
  const int state = radio.transmit(txFrame);

  noInterrupts();
  rxFlag = false;
  interrupts();

  startRadioReceive();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[RADIO] Transmit error: ");
    Serial.println(state);
    return HW_RADIO_ERROR;
  }
  return HW_RADIO_OK;
}

bool rxPollRadio(String& packet, int& radioState) {
  if (!rxFlag) return false;

  noInterrupts();
  rxFlag = false;
  interrupts();

  const int rawState = radio.readData(packet);
  if (rawState == RADIOLIB_ERR_NONE) radioState = HW_RADIO_OK;
  else if (rawState == RADIOLIB_ERR_CRC_MISMATCH) radioState = HW_RADIO_CRC_MISMATCH;
  else radioState = HW_RADIO_ERROR;
  startRadioReceive();
  return true;
}

void rxLedPulse() {
  digitalWrite(LED_BASE, HIGH);
  normalLedPulse = true;
  normalLedPulseStart = millis();
}

void rxLedUpdate() {
  if (normalLedPulse && millis() - normalLedPulseStart >= 50) {
    digitalWrite(LED_BASE, LOW);
    normalLedPulse = false;
  }
}

void rxActivateBuzzer() {
  tone(BUZZER_PIN, 2000);
  digitalWrite(LED_BASE, HIGH);
}

void rxDeactivateBuzzer() {
  noTone(BUZZER_PIN);
  digitalWrite(LED_BASE, LOW);
}
