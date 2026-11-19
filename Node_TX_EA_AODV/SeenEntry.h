#ifndef LORA_EA_AODV_TX_SEEN_ENTRY_H
#define LORA_EA_AODV_TX_SEEN_ENTRY_H

#include <Arduino.h>

struct SeenEntry {
  bool used = false;
  String key;           // source_id + boot_id + sequence_number or routing request ID
  unsigned long at = 0;
};

#endif // LORA_EA_AODV_TX_SEEN_ENTRY_H
