#ifndef LORA_EA_AODV_PACKET_FORMAT_H
#define LORA_EA_AODV_PACKET_FORMAT_H

#include <Arduino.h>

// Fungsi utilitas format frame; delimiter protokol tetap '|' dan '>'.
uint8_t splitFields(const String& text, String out[], uint8_t maxFields);
String joinFields(String fields[], uint8_t count);
bool pathContains(const String& path, const String& id);
uint8_t pathCount(const String& path);
String pathNodeAt(const String& path, uint8_t wanted);
String appendPath(const String& path, const String& id);
uint8_t min8(uint8_t a, uint8_t b);

#endif // LORA_EA_AODV_PACKET_FORMAT_H
