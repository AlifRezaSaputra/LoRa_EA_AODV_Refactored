#ifndef RX_HARDWARE_H
#define RX_HARDWARE_H

#include <Arduino.h>

enum HardwareRadioStatus {
  HW_RADIO_ERROR = -1,
  HW_RADIO_OK = 0,
  HW_RADIO_CRC_MISMATCH = 1
};

// Seluruh akses fisik Basecamp berada di modul ini.
bool rxHardwareBegin();
int rxTransmitFrame(const String& frame);
bool rxPollRadio(String& packet, int& radioState);

void rxLedPulse();
void rxLedUpdate();
void rxActivateBuzzer();
void rxDeactivateBuzzer();

#endif // RX_HARDWARE_H
