#ifndef TX_HARDWARE_H
#define TX_HARDWARE_H

#include <Arduino.h>

enum HardwareRadioStatus {
  HW_RADIO_ERROR = -1,
  HW_RADIO_OK = 0,
  HW_RADIO_CRC_MISMATCH = 1
};

// Seluruh akses fisik Node TX berada di modul ini.
bool txHardwareBegin();
int txTransmitFrame(const String& frame);
bool txPollRadio(String& packet, int& radioState);

void txLedRedOn();
void txLedGreenOn();
void txLedUpdate();

bool txSosPressed();
void txGpsUpdate();
bool txGetGpsLocation(float& latitude, float& longitude);

bool txIsInaAvailable();
float txGetBatteryVoltage();
float txGetBatteryCurrentMilliAmps();
float txGetBatteryPowerMilliWatts();
uint8_t txCalculateResidualEnergyPercent(float voltage);
uint8_t txGetResidualEnergyPercent();

#endif // TX_HARDWARE_H