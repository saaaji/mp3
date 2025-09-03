#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

extern "C" {
#include "driver/gpio.h"
#include "driver/sdspi_host.h"
#include "driver/spi_common.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"
}

#include "component.hpp"
#include "util.hpp"

class SDCard : public Component {
public:
  /// @brief maximum length for file paths and mount points
  /// @note this includes null terminator and must accommodate longest possible path
  static constexpr std::size_t kMaxPathLength = 128;
  
  /// @brief maximum number of files to track
  /// @note defines the size of the fixed arrays returned by file listing functions
  static constexpr std::size_t kMaxFiles = 32;
  
  /// @brief configuration structure for sd card spi pins
  struct Config {
    gpio_num_t miso;    ///< master in slave out pin
    gpio_num_t mosi;    ///< master out slave in pin
    gpio_num_t sck;     ///< serial clock pin
    gpio_num_t cs;      ///< chip select pin
  };

  /// @brief create an sd card component with the specified pin configuration
  /// @param config pin configuration for the sd card
  /// @note inherits from Component with standard memory load and high priority
  explicit SDCard(const Config& config);

  /// @brief mount the SD card and initialize the filesystem
  /// @return boolean indicating successful mounting of the SD card
  bool mount();
  
  /// @brief get ordered list of mp3 files based on config/playback_order.txt
  /// @return fixed-size array of paths in playback order, empty entries indicated by \0 at position 0
  /// @note reads from /sdcard/config/playback_order.txt, one filename per line
  std::array<std::array<char, kMaxPathLength>, kMaxFiles> get_ordered_mp3_files();
  
  /// @brief get all mp3 files from the music directory
  /// @return fixed-size array of paths, empty entries indicated by \0 at position 0
  /// @note scans /sdcard/music/ directory for .mp3 files
  std::array<std::array<char, kMaxPathLength>, kMaxFiles> list_all_mp3_files();
  
  /// @brief read bluetooth mac address from config file
  /// @param mac_address array to store the 6-byte mac address
  /// @return true if mac address was successfully read and parsed
  /// @note expects XX:XX:XX:XX:XX:XX format in /sdcard/config/bt_config.txt
  bool read_bluetooth_config(std::array<uint8_t, 6>& mac_address);

protected:
  /// @brief perform SD card and SPI bus initialization
  void initialize() override;

  /// @brief main task implementation for SD card operations
  void task_impl() override;

private:
  /// @brief pin configuration for the SD card
  const Config config_;

  /// @brief handle to the SD card
  sdmmc_card_t* card_{nullptr};

  /// @brief mount point for the SD card filesystem
  std::array<char, kMaxPathLength> mount_point_{"/sdcard\0"};
};
