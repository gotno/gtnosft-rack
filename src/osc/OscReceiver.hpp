/* #include <atomic> */
/* #include <thread> */
/* #include <queue> */
/* #include <mutex> */
/* #include <condition_variable> */

/* #include "../../dep/oscpack/ip/IpEndpointName.h" */
/* #include "../../dep/oscpack/osc/OscOutboundPacketStream.h" */
/* #include "../../dep/oscpack/ip/UdpSocket.h" */

/* #define MSG_BUFFER_SIZE 65507 // oscpack MAX_BUFFER_SIZE */
/* #define ENDPOINT "127.0.0.1" */
/* #define PORT 7000 */

struct OscReceiver {
  OscReceiver();
  ~OscReceiver();
};
