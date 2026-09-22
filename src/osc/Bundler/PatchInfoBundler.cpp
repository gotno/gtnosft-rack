#include "PatchInfoBundler.hpp"

#include "patch.hpp"

PatchInfoBundler::PatchInfoBundler(
  int64_t ctrlId
): Bundler("PatchInfoBundler") {
  std::string filename = rack::system::getFilename(APP->patch->path);

  messages.emplace_back(
    "/set/patch_info",
    [=](osc::OutboundPacketStream& pstream) {
      pstream << ctrlId
        << filename.c_str()
        ;
    }
  );
}
