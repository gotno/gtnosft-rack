#pragma once

// 1452 best case/1232 worst case according to io7m.com/documents/udp-reliable/
#define MSG_BUFFER_SIZE 1232

#define EMPTY_BUNDLE_SIZE 16

#define TX_PORT 7746 // RSIM
#define RX_PORT 7225 // RACK

#define MAX_MISSED_HEARTBEATS 5
#define HEARTBEAT_INTERVAL_MS 1000 // ms between heartbeats
#define AUDIO_REANNOUNCE_HEARTBEATS 2
