#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>

namespace aiws::remote {

/**
 * @brief Wire format shared by the application and aiws_send.
 *
 * Every message is one tag byte, a 4-byte big-endian payload length, then
 * the payload bytes. A request uses tag kAdd with the absolute file path in
 * UTF-8. A reply uses tag kOk or kRejected with a UTF-8 reason (may be empty).
 */
constexpr std::uint8_t kAdd = 'A';
constexpr std::uint8_t kOk = 0;
constexpr std::uint8_t kRejected = 1;
constexpr std::uint32_t kMaxPayload = 64 * 1024;

using Clock = std::chrono::steady_clock;

/** @brief /tmp/aiws-$USER.sock, or /tmp/aiws-<uid>.sock when USER is not set. */
std::string default_socket_path();

/** @brief Encodes one message as tag, length, and payload bytes. */
std::string encode(std::uint8_t tag, const std::string& payload);

/** @brief Writes all of data before the deadline. */
bool send_all(int fd, const std::string& data, Clock::time_point deadline);

/**
 * @brief Reads one whole message, however many pieces it arrives in.
 * @param stop Optional flag; reading gives up soon after it becomes true.
 * @return false on timeout, stop, disconnect, or an oversized payload.
 */
bool read_message(int fd, std::uint8_t& tag, std::string& payload,
                  Clock::time_point deadline, const std::atomic<bool>* stop = nullptr);

}  // namespace aiws::remote
