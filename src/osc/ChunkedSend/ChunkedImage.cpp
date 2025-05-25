#include "ChunkedImage.hpp"

// #include <stdexcept> // No longer catching std::bad_weak_ptr in this file

#include "../Bundler/ChunkedImageBundler.hpp"

#define QOI_IMPLEMENTATION
#include "qoi/qoi.h"

#include <algorithm>

ChunkedImage::ChunkedImage(uint8_t* _pixels, int32_t _width, int32_t _height):
  ChunkedSend(_pixels, _width * _height * ChunkedImage::DEPTH), // ChunkedSend now handles _pixels with shared_ptr
  width(_width), height(_height) {
    // Removed diagnostic try-catch block for shared_from_this()

    // Original constructor logic follows
    // TODO?: throw on compression failure, catch in caller and dispose
    if (!compressData()) WARN("failed to compress image data"); // This calls the (currently no-op) compressData
    findChunkSize();
  }

bool ChunkedImage::compressData() {
  // Temporarily bypassing QOI compression to diagnose std::bad_weak_ptr.
  // The ChunkedSend::data and ChunkedSend::size are already initialized
  // with uncompressed pixel data and size by the ChunkedImage constructor
  // calling the ChunkedSend constructor.
  //
  // Original compression logic commented out:
  // qoi_desc desc;
  // desc.width = width;
  // desc.height = height;
  // desc.channels = ChunkedImage::DEPTH;
  // desc.colorspace = 0;
  //
  // int compressedLength{0};
  // void* compressedData = qoi_encode(data, &desc, &compressedLength);
  //
  // if (!compressedData) return false;
  //
  // delete[] data;
  // data = new uint8_t[compressedLength];
  // size = compressedLength;
  // memcpy(data, compressedData, size);
  // free(compressedData);

  return true;
}

#include <functional> // Make sure std::function is available

ChunkedSendBundler* ChunkedImage::getBundlerForChunk(int32_t chunkNum) {
  // 1. Gather all necessary data from 'this' (ChunkedImage object)
  int32_t currentId = this->id;
  int32_t currentNumChunks = this->numChunks; // Calculated by findChunkSize -> setChunkSize -> calculateNumChunks
  int32_t currentChunkSize = this->chunkSize; // Set by findChunkSize
  int64_t currentTotalSize = this->size;     // This is compressed size if compression were active, else original.
                                             // Since compression is bypassed, this->size is the original pixel data size.
  std::shared_ptr<uint8_t[]> currentPixelData = this->pixelData; // The shared_ptr to pixel data

  int32_t currentImageWidth = this->width;
  int32_t currentImageHeight = this->height;

  // 2. Pre-calculate isNoopStatus
  // Accessing chunkAckTimes and sendFailed directly from 'this'.
  bool isNoopStatus = this->sendFailed() || (this->chunkAckTimes.count(chunkNum) > 0);

  // 3. Create the callback for registerChunkSent
  // Using [this] capture. This assumes that OscSender/ChunkedManager ensures that
  // the ChunkedImage instance ('this') is alive when the callback is invoked.
  // This is a critical point for lifecycle management.
  std::function<void(int32_t)> _registerChunkSentCb = [this](int32_t cn) {
    this->registerChunkSent(cn);
  };

  // 4. Instantiate and return the new ChunkedImageBundler
  return new ChunkedImageBundler(
      chunkNum,
      currentId,
      currentNumChunks,
      currentChunkSize,
      currentTotalSize,
      currentPixelData,
      isNoopStatus,
      _registerChunkSentCb, // The lambda created above
      currentImageWidth,
      currentImageHeight
  );
}
