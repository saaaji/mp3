#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>
#include <string>

extern "C" {

#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

}

#include "component.hpp"
#include "util.hpp"

class SdCardObject : public ActiveObject {
public:
  /// @brief maximum length for file paths and mount points
  static constexpr std::size_t kMaxPathLength = 300;
  
  /// @brief SD card interface type
  enum class Interface : std::uint8_t {
    SPI,      ///< Use SPI interface (SDSPI)
    SDMMC     ///< Use SDMMC interface
  };

  /// @brief configuration structure for SD card interface
  struct Config {
    /// @brief SD interface
    Interface interface = Interface::SPI;
    
    /// @brief SPI configuration
    gpio_num_t miso, mosi, sck, cs;
    
    /// @brief common configuration
    std::uint32_t max_frequency_khz = 20000;
    std::uint8_t max_open_files = 5;

    /// @brief flags
    bool format_if_mount_failed = false;
  };

  /// @brief SD constructor
  /// @param config configuration for the SD card
  SdCardObject(const Config& config);

  /// @brief unmount the SD card on destruction
  ~SdCardObject();

  /// @brief check if SD card is mounted
  bool mount();
  
  /// @brief unmount the SD card
  void unmount();
  
  /// @brief get list of discovered MP3 files
  /// @return vector of MP3 file paths
  std::vector<std::string> get_mp3_files();
  
  /// @brief read playback order from config file
  /// @return vector of file names in playback order
  std::vector<std::string> read_playback_order();
  
  /// @brief get mount point path
  std::string_view get_mount_point() const { return mount_point_.data(); }

protected:
  void initialize() override;
  void task() override;

private:
  /// @brief create required directories
  /// @return boolean indicating success
  bool create_directories();

  /// @brief SD config
  const Config config_;

  /// @brief handle for SD card
  sdmmc_card_t* card_{nullptr};

  /// @brief mount point path
  std::array<char, kMaxPathLength> mount_point_{"/sdcard\0"};

  /// @brief vector of file paths under the music folder
  std::vector<std::string> file_paths_;

  /// @brief vector of mp3 names as listed in the playback config
  std::vector<std::string> queue_;
};
