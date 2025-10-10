#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <variant>
#include <type_traits>
#include <span>
#include <tuple>

#include "util.hpp"

extern "C" {

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/ringbuf.h"

}

namespace mp3 {

using ::xRingbufferCreate;
using ::vRingbufferDelete;
using ::xRingbufferSendAcquire;
using ::xRingbufferSendComplete;
using ::xRingbufferReceive;
using ::vRingbufferReturnItem;

template<typename T>
concept Serializable = requires(T t, uint8_t* buf, const uint8_t* const_buf) {
  { t.serialize(buf) } -> std::same_as<size_t>; // should return number of bytes written
  { T::deserialize(const_buf) } -> std::same_as<T>; // should return an instance of T
  { T::serialized_size() } -> std::same_as<size_t>; // should return size of serialized T
};

template<typename T>
concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

/// @brief interface for sending and receiving messages between RTOS tasks
/// @tparam ...ExplicitMessageTypes list of message types that can be sent/received
template<typename... ExplicitMessageTypes> 
requires ((Serializable<ExplicitMessageTypes> || TriviallyCopyable<ExplicitMessageTypes>) && ...)
class Mailbox {
private:
  /// @brief type ID for binary blob messages
  static constexpr std::uint8_t kBlobTypeId = 255;

  /// @brief determine the maximum alignment required by the message types
  static constexpr std::size_t max_alignment_ = []() constexpr {
    std::size_t result = 1;
    ((result = std::max(result, alignof(ExplicitMessageTypes))), ...);
    return result;
  }();

  /// @brief metadata header for messages
  struct alignas(max_alignment_) MessageHeader {
    std::uint8_t type_id; // type identifier for the message
    std::size_t payload_size; // size of the message payload in bytes
  };

public:
  /// @brief handle for sending messages
  class SendHandle {
  public:
    /// @brief create a SendHandle to manage the lifetime of a Ringbuffer slice
    /// @param handle esp32 Ringbuffer handle
    /// @param raw_slice data slice acquired from the Ringbuffer (including header)
    /// @param payload_size size of the message payload in bytes (excluding header)
    SendHandle(RingbufHandle_t handle, std::uint8_t* raw_slice, std::size_t payload_size) 
      : payload(raw_slice + sizeof(MessageHeader), payload_size),
        raw_slice_(raw_slice),
        handle_(handle) {}

    /// @brief send the message
    ~SendHandle() {
      if (handle_ && raw_slice_) {
        xRingbufferSendComplete(handle_, static_cast<void*>(raw_slice_));
      }
    }

    SendHandle(const SendHandle&) = delete;
    SendHandle& operator=(const SendHandle&) = delete;
    
    /// @brief slice of the Ringbuffer to write to directly (excluding header)
    std::span<std::uint8_t> payload;

  private:
    /// @brief pointer to the start of the raw slice (including header)
    std::uint8_t* raw_slice_{nullptr};  

    /// @brief handle for the underlying esp32 Ringbuffer
    RingbufHandle_t handle_{nullptr};
  };

  class RecvHandle {
  public:
    /// @brief create a RecvHandle to manage the lifetime of a Ringbuffer slice
    /// @param handle esp32 Ringbuffer handle
    /// @param raw_slice the raw slice acquired from the Ringbuffer (including header)
    RecvHandle(RingbufHandle_t handle, std::uint8_t* raw_slice) 
      : raw_slice_(raw_slice),
        handle_(handle), 
        header_(*reinterpret_cast<MessageHeader*>(raw_slice)),
        payload_(raw_slice + sizeof(MessageHeader), header_.payload_size) {}

    /// @brief return the data to the Ringbuffer
    ~RecvHandle() {
      if (handle_ && raw_slice_) {
        vRingbufferReturnItem(handle_, static_cast<void*>(raw_slice_));
      }
    }

    template<typename Visitor>
    void visit(Visitor&& visitor) const {
      if (header_.type_id == kBlobTypeId) {
        visitor(payload_);
      } else {
        visit_impl<0>(std::forward<Visitor>(visitor));
      }
    }

    RecvHandle(const RecvHandle&) = delete;
    RecvHandle& operator=(const RecvHandle&) = delete;
    RecvHandle(RecvHandle&&) = delete;
    RecvHandle& operator=(RecvHandle&&) = delete;

  private:
    template<std::size_t Index, typename Visitor>
    void visit_impl(Visitor&& visitor) const {
      if constexpr (Index < sizeof...(ExplicitMessageTypes)) {
        using M = std::tuple_element_t<Index, std::tuple<ExplicitMessageTypes...>>;
        if (header_.type_id == Index) {
          if constexpr (Serializable<M>) {
            M msg = M::deserialize(payload_.data());
            
            visitor(msg);
            return;
          } else if constexpr (TriviallyCopyable<M>) {
            const M& msg = *reinterpret_cast<const M*>(payload_.data());

            visitor(msg);
            return;
          } else {
            static_assert(always_false_v<M>, "message type should either be Serializable or TriviallyCopyable");
          }
        } else {
          visit_impl<Index + 1>(std::forward<Visitor>(visitor));
        }
      }
    }

    /// @brief pointer to the slice in the underlying esp32 Ringbuffer
    std::uint8_t* raw_slice_{nullptr};

    /// @brief handle to underlying esp32 Ringbuffer
    RingbufHandle_t handle_{nullptr};

    /// @brief type ID
    MessageHeader header_;

    /// @brief payload data
    std::span<const std::uint8_t> payload_;
  };

  /// @brief lookup the type ID for a given message type
  /// @tparam M message type
  /// @tparam Index index for recursion (do not set)
  /// @return type ID of given message type, if it exists in ExplicitMessageTypes
  template<typename M, std::size_t Index = 0>
  static constexpr std::uint8_t get_type_id() {
    if constexpr (Index >= sizeof...(ExplicitMessageTypes)) {
      static_assert(always_false_v<M>, "type not found in ExplicitMessageTypes");
    } else if constexpr (std::is_same_v<M, std::tuple_element_t<Index, std::tuple<ExplicitMessageTypes...>>>) {
      return Index;
    } else {
      return get_type_id<M, Index + 1>();
    }
  }

  /// @brief lookup the payload size for a given message type
  /// @tparam M message type
  /// @return size of the given message type in bytes
  template<typename M>
  static constexpr std::size_t get_payload_size() {
    if constexpr (Serializable<M>) {
      return M::serialized_size();
    } else if constexpr (TriviallyCopyable<M>) {
      return sizeof(M);
    } else {
      static_assert(always_false_v<M>, "M must be Serializable or TriviallyCopyable");
    }
  }

public:
  /// @brief create a new mailbox with specified capacity
  /// @param capacity_bytes capacity of the mailbox in bytes
  Mailbox(const std::size_t capacity_bytes) {
    handle_ = xRingbufferCreate(capacity_bytes, RINGBUF_TYPE_NOSPLIT);
  }

  /// @brief destroy the mailbox and free the Ringbuffer
  ~Mailbox() {
    if (handle_) {
      vRingbufferDelete(handle_);
    }
  }

  /// @brief send explicit message to the mailbox
  /// @tparam M message type
  /// @param msg message value
  /// @param timeout tick timeout to wait for space in the mailbox
  /// @return boolean indicating successful send
  template<typename M>
  requires ((std::same_as<M, ExplicitMessageTypes> || ...))
  bool send_message(const M& msg, const TickType_t timeout = portMAX_DELAY) {
    if (!handle_) {
      return false;
    }

    // serialize the message into a byte buffer (slice of the Ringbuffer)
    constexpr std::uint8_t type_id = get_type_id<M>();
    constexpr std::size_t payload_size = get_payload_size<M>();
    constexpr std::size_t total_size = sizeof(MessageHeader) + payload_size;

    void* raw_ptr = nullptr;
    BaseType_t stat = xRingbufferSendAcquire(handle_, &raw_ptr, total_size, timeout);
    

    // unable to acquire space in the Ringbuffer (timed out)
    if (stat != pdTRUE) {
      return false;
    }

    // write header
    uint8_t* slice = static_cast<uint8_t*>(raw_ptr);
    new (slice) MessageHeader{type_id, payload_size};

    // write payload
    if constexpr (Serializable<M>) {
      msg.serialize(slice + sizeof(MessageHeader));
    } else if constexpr (TriviallyCopyable<M>) {
      std::memcpy(slice + sizeof(MessageHeader), &msg, payload_size);
    } else {
      static_assert(always_false_v<M>, "M must be Serializable or TriviallyCopyable");
    }

    // commit the write
    stat = xRingbufferSendComplete(handle_, static_cast<void*>(slice));
    return stat == pdTRUE;
  }

  /// @brief acquire a handle to send a binary blob message to the mailbox
  /// @param payload_size size of the binary blob payload in bytes
  /// @param timeout tick timeout to wait for space in the mailbox
  /// @return optional SendHandle to manage the lifetime of the acquired slice
  std::optional<SendHandle> acquire_send_handle(const std::size_t payload_size, const TickType_t timeout = portMAX_DELAY) {
    if (!handle_) {
      return std::nullopt;
    }

    const std::size_t total_size = sizeof(MessageHeader) + payload_size;  
    
    void* raw_ptr = nullptr;
    BaseType_t stat = xRingbufferSendAcquire(handle_, &raw_ptr, total_size, timeout);

    // unable to acquire space in the Ringbuffer (timed out)
    if (stat != pdTRUE) {
      return std::nullopt;
    }

    // write header
    uint8_t* slice = static_cast<uint8_t*>(raw_ptr);
    new (slice) MessageHeader{kBlobTypeId, payload_size};
    return std::make_optional<SendHandle>(handle_, slice, payload_size);
  }

  /// @brief receive a handle to the latest item in the Ringbuffer
  /// @param timeout tick timeout to wait for new items in the mailbox
  /// @return optional RecvHandle to manage the lifetime of the acquired slice
  std::optional<RecvHandle> acquire_recv_handle(const TickType_t timeout = portMAX_DELAY) {
    std::size_t total_size;
    uint8_t* raw_slice = static_cast<uint8_t*>(xRingbufferReceive(handle_, &total_size, timeout));

    // was unable to receive an item (timed out)
    if (raw_slice == nullptr) {
      return std::nullopt;
    }

    return std::make_optional<RecvHandle>(handle_, raw_slice);
  }

private:
  /// @brief handle for the underlying esp32 Ringbuffer
  RingbufHandle_t handle_{nullptr};
};

}