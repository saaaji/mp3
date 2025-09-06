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

class SDCard : public Component {
public:
  /// @brief maximum length for file paths and mount points
  static constexpr std::size_t kMaxPathLength = 300;
  
  /// @brief maximum number of files to track
  static constexpr std::size_t kMaxFiles = 32;
  
  /// @brief SD card interface type
  enum class Interface {
    SPI,      ///< Use SPI interface (SDSPI)
    SDMMC     ///< Use SDMMC interface
  };

  /// @brief configuration structure for SD card interface
  struct Config {
    Interface interface = Interface::SPI;
    
    // SPI-specific configuration
    int miso = -1;
    int mosi = -1;
    int sck = -1;
    int cs = -1;
    
    // Common configuration
    uint32_t max_frequency_khz = 20000;
    bool format_if_mount_failed = false;
    int max_open_files = 5;
  };

  explicit SDCard(const Config& config);
  ~SDCard();

  /// @brief check if SD card is mounted
  bool mount();
  
  /// @brief unmount the SD card
  void unmount();
  
  /// @brief discover MP3 files on the SD card
  /// @return number of MP3 files found
  std::size_t discover_mp3_files();
  
  /// @brief get list of discovered MP3 files
  /// @return vector of MP3 file paths
  std::vector<std::string> get_mp3_files() const;
  
  /// @brief read playback order from config file
  /// @return vector of file names in playback order
  std::vector<std::string> read_playback_order();
  
  /// @brief get mount point path
  std::string_view get_mount_point() const { return mount_point_.data(); }

protected:
  void initialize() override;
  void task_impl() override;

private:
  /// @brief initialize SPI interface using official ESP-IDF approach
  esp_err_t initialize_spi_interface();
  
  /// @brief create required directories
  void create_directories();

  const Config config_;
  sdmmc_card_t* card_{nullptr};
  std::array<char, kMaxPathLength> mount_point_{"/sdcard\0"};
  std::array<std::array<char, kMaxPathLength>, kMaxFiles> files;
  std::size_t file_count_{0};
};
