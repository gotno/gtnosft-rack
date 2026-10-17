#include "SettingsBundler.hpp"

#include "patch.hpp"
#include "settings.hpp"

SettingsBundler::SettingsBundler(): Bundler("SettingsBundler") {
  messages.emplace_back(
    "/set/settings",
    [=](osc::OutboundPacketStream& pstream) {
      pstream << rack::settings::rackBrightness
        << rack::settings::cableOpacity
        << rack::settings::cableTension
        ;
      for (auto& color : rack::settings::cableColors) {
        pstream << rack::color::toHexString(color).c_str();
      }
    }
  );
}
