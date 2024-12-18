#include "plugin.hpp"

struct ChunkedImage {
  int depth{4};
  int width{0};
  int height{0};
  uint8_t* pixels;


  int numChunks;

  ChunkedImage(int _width, int _height, uint8_t* _pixels):
    width(_width), height(_height) {
      pixels = new uint8_t[width * height * depth];
      memcpy(&pixels, _pixels, width * height * depth);
    }

  ~ChunkedImage() {
    delete[] pixels;
  }
}
