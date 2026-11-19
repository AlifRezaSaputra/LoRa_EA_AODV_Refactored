#include "PacketFormat.h"

uint8_t splitFields(const String& text, String out[], uint8_t maxFields) {
  uint8_t count = 0;
  int start = 0;
  for (int i = 0; i <= (int)text.length(); ++i) {
    if (i == (int)text.length() || text.charAt(i) == '|') {
      if (count < maxFields) out[count++] = text.substring(start, i);
      start = i + 1;
    }
  }
  return count;
}

String joinFields(String fields[], uint8_t count) {
  String out;
  for (uint8_t i = 0; i < count; ++i) {
    if (i) out += '|';
    out += fields[i];
  }
  return out;
}

bool pathContains(const String& path, const String& id) {
  int start = 0;
  while (start <= (int)path.length()) {
    int pos = path.indexOf('>', start);
    if (pos < 0) pos = path.length();
    if (path.substring(start, pos) == id) return true;
    if (pos >= (int)path.length()) break;
    start = pos + 1;
  }
  return false;
}

uint8_t pathCount(const String& path) {
  if (!path.length()) return 0;
  uint8_t n = 1;
  for (uint16_t i = 0; i < path.length(); ++i) {
    if (path.charAt(i) == '>') n++;
  }
  return n;
}

String pathNodeAt(const String& path, uint8_t wanted) {
  uint8_t idx = 0;
  int start = 0;
  for (int i = 0; i <= (int)path.length(); ++i) {
    if (i == (int)path.length() || path.charAt(i) == '>') {
      if (idx == wanted) return path.substring(start, i);
      idx++;
      start = i + 1;
    }
  }
  return "";
}

String appendPath(const String& path, const String& id) {
  if (!path.length()) return id;
  return path + ">" + id;
}

uint8_t min8(uint8_t a, uint8_t b) {
  return (a < b) ? a : b;
}
