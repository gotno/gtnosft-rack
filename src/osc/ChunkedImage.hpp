#include "plugin.hpp"
#include <cmath>

struct ChunkedImage {
  inline static int32_t idCounter{0};
  static const int32_t CHUNK_SIZE = 49152; // 48kib
  int32_t id;
  int32_t depth{4};
  int32_t width{0};
  int32_t height{0};
  uint8_t* pixels;

  int32_t numChunks;

  ChunkedImage(int32_t _width, int32_t _height, uint8_t* _pixels):
    width(_width), height(_height), pixels(_pixels) {
      /* pixels = new uint8_t[totalBytes]; */
      /* memcpy(&pixels, _pixels, totalBytes); */

      numChunks =
        // integer ceiling
        ((width * height * depth) + CHUNK_SIZE - 1) / CHUNK_SIZE;
    }

  ~ChunkedImage() {
    delete[] pixels;
  }
};
