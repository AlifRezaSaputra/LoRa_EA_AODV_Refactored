#include "Hardware.h"

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_INA219.h>
#include <RadioLib.h>
#include <TinyGPSPlus.h>

#include "Config.h"

namespace {
  HardwareSerial gpsSerial(2);
  TinyGPSPlus gps;
  SX1276 radio = new Module(LORA_CS, LORA_DIO0, LORA_RST, LORA_DIO1);
  Adafruit_INA219 ina219;

  bool inaAvailable = false;
  volatile bool rxFlag = false;
  bool ledRedActive = false;
  unsigned long ledRedStart = 0;
  bool ledGreenActive = false;
  unsigned long ledGreenStart = 0;

  float filteredBatteryVoltage = 0.0f;
  bool batteryVoltageInitialized = false;

  struct BatteryPoint {
    float voltage;
    uint8_t percent;
  };

  // Tabel tegangan 2S asli (6.40V Cut-off = 0% hingga 8.40V = 100%).
  const BatteryPoint batteryTable[] = {
    {6.40f,  0},
    {6.80f,  5},
    {7.00f, 10},
    {7.20f, 20},
    {7.40f, 35},
    {7.60f, 50},
    {7.70f, 60},
    {7.80f, 70},
    {7.90f, 80},
    {8.00f, 90},
    {8.20f, 95},
    {8.40f, 100}
  };

  void onReceiveFlag() {
    rxFlag = true;
  }

  void startRadioReceive() {
    int state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE) {
      Serial.print("[RADIO] Gagal masuk receive mode: ");
      Serial.println(state);
    }
  }
}

bool txHardwareBegin() {
  pinMode(SOS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_GREEN, LOW);

  Wire.begin(I2C_SDA, I2C_SCL);
  if (!ina219.begin()) {
    Serial.println("[INIT] INA219 gagal / tidak terdeteksi!");
    txLedRedOn();
  } else {
    inaAvailable = true;
    Serial.println("[INIT] INA219 siap.");
  }

  gpsSerial.begin(9600, SERIAL_8N1, GPS_RX, GPS_TX);
  Serial.println("[INIT] GPS NEO-7M UART2 siap (RX=16, TX=17, 9600 baud).");

  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  int state = radio.begin(LORA_FREQ, LORA_BW, LORA_SF, LORA_CR,
                          LORA_SYNCWORD, LORA_POWER, LORA_PREAMBLE);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.print("[INIT] LoRa gagal dimulai. Kode: ");
    Serial.println(state);
    txLedRedOn();
    return false;
  }

  radio.setCRC(true);
  radio.setDio0Action(onReceiveFlag, RISING);
  startRadioReceive();
  return true;
}

int txTransmitFrame(const String& frame) {
  // Salinan mutable dipakai sesuai kebutuhan RadioLib yang digunakan sketch asal.
  String txFrame = frame;
  int state = radio.transmit(txFrame);

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

bool txPollRadio(String& packet, int& radioState) {
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

void txLedRedOn() {
  digitalWrite(LED_RED, HIGH);
  ledRedActive = true;
  ledRedStart = millis();
}

void txLedGreenOn() {
  digitalWrite(LED_GREEN, HIGH);
  ledGreenActive = true;
  ledGreenStart = millis();
}

void txLedUpdate() {
  const unsigned long now = millis();

  if (ledRedActive && now - ledRedStart >= LED_DURATION) {
    digitalWrite(LED_RED, LOW);
    ledRedActive = false;
  }
  if (ledGreenActive && now - ledGreenStart >= LED_DURATION) {
    digitalWrite(LED_GREEN, LOW);
    ledGreenActive = false;
  }
}

bool txSosPressed() {
  return digitalRead(SOS_BUTTON_PIN) == LOW;
}

void txGpsUpdate() {
  while (gpsSerial.available() > 0) {
    gps.encode(gpsSerial.read());
  }

  if (gps.location.isUpdated() && gps.location.isValid()) {
    Serial.print("LAT: ");
    Serial.println(gps.location.lat(), 7);
    Serial.print("LNG: ");
    Serial.println(gps.location.lng(), 7);
    Serial.println("--------------------");
  }
}

bool txGetGpsLocation(float& latitude, float& longitude) {
  const bool valid = gps.location.isValid() && gps.location.age() < 5000;
  if (!valid) {
    latitude = 0.0f;
    longitude = 0.0f;
    return false;
  }
  latitude = gps.location.lat();
  longitude = gps.location.lng();
  return true;
}

bool txIsInaAvailable() {
  return inaAvailable;
}

float txGetBatteryVoltage() {
  if (!inaAvailable) return 0.0f;

  // INA219 bus voltage + shunt voltage = tegangan pada sisi VIN+.
  const float busVoltage = ina219.getBusVoltage_V();
  const float shuntVoltage = ina219.getShuntVoltage_mV() / 1000.0f;
  const float measuredVoltage = busVoltage + shuntVoltage;

  if (!isfinite(measuredVoltage) || measuredVoltage < 1.0f ||
      measuredVoltage > 10.0f) {
    return 0.0f;
  }

  // EMA menghaluskan voltage sag sesaat ketika transmisi LoRa.
  if (!batteryVoltageInitialized) {
    filteredBatteryVoltage = measuredVoltage;
    batteryVoltageInitialized = true;
  } else {
    filteredBatteryVoltage =
      BATTERY_EMA_ALPHA * measuredVoltage +
      (1.0f - BATTERY_EMA_ALPHA) * filteredBatteryVoltage;
  }

  return filteredBatteryVoltage;
}

float txGetBatteryCurrentMilliAmps() {
  if (!inaAvailable) return 0.0f;
  const float current = ina219.getCurrent_mA();
  return isfinite(current) ? current : 0.0f;
}

float txGetBatteryPowerMilliWatts() {
  if (!inaAvailable) return 0.0f;
  const float power = ina219.getPower_mW();
  return isfinite(power) ? power : 0.0f;
}

uint8_t txCalculateResidualEnergyPercent(float voltage) {
  if (!inaAvailable || !isfinite(voltage) || voltage <= 0.0f) return 0;

  const int count = sizeof(batteryTable) / sizeof(batteryTable[0]);
  if (voltage <= batteryTable[0].voltage) return 0;
  if (voltage >= batteryTable[count - 1].voltage) return 100;

  for (int i = 0; i < count - 1; ++i) {
    const BatteryPoint& low = batteryTable[i];
    const BatteryPoint& high = batteryTable[i + 1];

    if (voltage >= low.voltage && voltage < high.voltage) {
      const float ratio =
        (voltage - low.voltage) / (high.voltage - low.voltage);
      const float percent =
        low.percent + ratio * (high.percent - low.percent);
      return (uint8_t)constrain((int)roundf(percent), 0, 100);
    }
  }
  return 0;
}

uint8_t txGetResidualEnergyPercent() {
  return txCalculateResidualEnergyPercent(txGetBatteryVoltage());
}