#include "EchoPacker.hpp"

EchoPacker::EchoPacker(std::string _toEcho) {
  toEcho = _toEcho;
  path = "/echo";
}

void EchoPacker::pack(osc::OutboundPacketStream& message) {
  message << "HELLO FROM ECHOPACKER";
  if (toEcho != "") message << (std::string("ECHO: ").append(toEcho)).c_str();
}
