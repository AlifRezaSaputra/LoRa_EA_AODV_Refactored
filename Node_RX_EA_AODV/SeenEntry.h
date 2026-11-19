#ifndef LORA_EA_AODV_RX_SEEN_ENTRY_H
#define LORA_EA_AODV_RX_SEEN_ENTRY_H

#include <Arduino.h>

struct SeenEntry {
  bool used = false;
  String key;
  unsigned long at = 0;
};

#endif // LORA_EA_AODV_RX_SEEN_ENTRY_H
