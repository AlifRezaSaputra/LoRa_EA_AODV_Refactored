/*
  LoRa EA-AODV - Node TX (SOURCE + RELAY)

  File ini sengaja dibuat tipis. Detail perangkat ada di Hardware.cpp,
  sedangkan protokol dan routing ada di Router.cpp.
*/
#include "Router.h"

void setup() {
  routerBegin();
}

void loop() {
  routerUpdate();
}
