#pragma once

#define MSG_BUFFER_SIZE 1452 // best case, 1232 worst case
// according to https://io7m.com/documents/udp-reliable/

#define EMPTY_BUNDLE_SIZE 16
#define TX_PORT 7746 // RSIM
#define RX_PORT 7225 // RACK
#define HEARTBEAT_DELAY 1000 // ms between heartbeats
