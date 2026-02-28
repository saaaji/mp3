#pragma once

#include <span>
#include <cstddef>
#include <cstring>

extern "C" {

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

}

namespace rtos {

using ::xRingbufferCreate;
using ::vRingbufferDelete;
using ::xRingbufferSend;
using ::xRingbufferReceiveUpTo;
using ::vRingbufferReturnItem;

class StreamingQueue {
public:
  StreamingQueue(const std::size_t capacity_bytes) {
    handle_ = xRingbufferCreate(capacity_bytes, RINGBUF_TYPE_BYTEBUF);
  }

  ~StreamingQueue() {
    if (handle_) {
      vRingbufferDelete(handle_);
    }
  }

  std::size_t write(std::span<uint8_t> src, const TickType_t timeout = portMAX_DELAY) {
    std::size_t bytes_written = 0;
    const std::size_t max_chunk_size = xRingbufferGetMaxItemSize(handle_);
    
    while (bytes_written < src.size()) {
      std::size_t bytes_remaining = src.size() - bytes_written;
      bytes_remaining = std::min(bytes_remaining, max_chunk_size);

      BaseType_t stat = xRingbufferSend(handle_, static_cast<void*>(src.data() + bytes_written), bytes_remaining, timeout);

      if (stat != pdTRUE) break;
      bytes_written += bytes_remaining;
    }

    return bytes_written;
  }

  [[nodiscard]] std::size_t read(std::span<uint8_t> dest) {
    std::size_t bytes_read = 0;

    void *data = xRingbufferReceiveUpTo(handle_, &bytes_read, 0, dest.size());
    if (!data) return 0;
    std::memcpy(dest.data(), data, bytes_read);
    vRingbufferReturnItem(handle_, data);

    return bytes_read;
  }

private:
  RingbufHandle_t handle_{nullptr};
};

}