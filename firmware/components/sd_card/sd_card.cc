#include "include/sd_card.hpp"
#include <array>
#include <cstring>
#include <unistd.h>  // for access()
#include <filesystem>  // C++ filesystem operations
#include <system_error>  // for std::error_code

SDCard::SDCard(const Config& config)
  : Component("SDCard", Component::MemoryLoad::kStandard, Component::Priority::kHigh, false),
    config_(config) {}

void SDCard::initialize() {
  // Initialize SPI bus
  spi_bus_config_t bus_config = {};  // Zero initialize first
  bus_config.mosi_io_num = config_.mosi;
  bus_config.miso_io_num = config_.miso;
  bus_config.sclk_io_num = config_.sck;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  bus_config.max_transfer_sz = 4000;
  // All other fields will be 0/default initialized

  // Initialize SD card
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = config_.cs;
  slot_config.host_id = SDSPI_DEFAULT_HOST;

  // Mount FAT filesystem
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = 5,
    .allocation_unit_size = 16 * 1024,
    .disk_status_check_enable = false,
    .use_one_fat = false
  };

  esp_err_t ret = spi_bus_initialize(
    static_cast<spi_host_device_t>(SDSPI_DEFAULT_HOST), 
    &bus_config, 
    SDSPI_DEFAULT_DMA);
  CHECK_EQ(ret, ESP_OK, "Failed to initialize SPI bus");

  ret = esp_vfs_fat_sdspi_mount(
    mount_point_.data(), 
    &host, 
    &slot_config, 
    &mount_config, 
    &card_);
  CHECK_EQ(ret, ESP_OK, "Failed to mount SD card");
}

void SDCard::task_impl() {
  // Task for monitoring SD card status if needed
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

bool SDCard::mount() {
  // Mount is handled in initialize()
  return card_ != nullptr;
}

/// @brief get all mp3 files from the music directory
/// @return fixed-size array of paths, empty entries indicated by \0 at position 0
/// @note scans /sdcard/music/ directory for .mp3 files
std::array<std::array<char, SDCard::kMaxPathLength>, SDCard::kMaxFiles> SDCard::list_all_mp3_files() {
  std::array<std::array<char, kMaxPathLength>, kMaxFiles> files{};
  size_t file_count = 0;

  // Build music directory path
  char music_dir[kMaxPathLength];
  const size_t base_len = strlen(mount_point_.data());
  if (base_len + 7 >= kMaxPathLength) {  // 7 for "/music\0"
    LOG("Mount point path too long");
    return files;
  }
  strcpy(music_dir, mount_point_.data());
  strcat(music_dir, "/music");

  // Iterate through directory using C++ filesystem (non-throwing)
  std::error_code ec;
  for (const auto& entry : std::filesystem::directory_iterator(music_dir, ec)) {
    if (ec) {
      LOG("Failed to iterate music directory: %s (error: %s)", music_dir, ec.message().c_str());
      break;
    }
    
    if (file_count >= kMaxFiles) {
      break;
    }

    // Check if it's a regular file with .mp3 extension
    if (!entry.is_regular_file(ec) || ec) {
      continue;  // Skip directories and handle errors gracefully
    }

    const std::filesystem::path& file_path = entry.path();
    if (file_path.extension() != ".mp3") {
      continue;  // Skip non-MP3 files
    }

    // Get the full path as string
    const std::string full_path_str = file_path.string();
    if (full_path_str.length() >= kMaxPathLength) {
      LOG("Path too long for file: %s", file_path.filename().c_str());
      continue;
    }
    
    char* dest = files[file_count].data();
    strncpy(dest, full_path_str.c_str(), kMaxPathLength - 1);
    dest[kMaxPathLength - 1] = '\0';  // Ensure null termination
    file_count++;
  }
  return files;
}

/// @brief get ordered list of mp3 files based on config/playback_order.txt
/// @return fixed-size array of paths in playback order, empty entries indicated by \0 at position 0
/// @note reads from /sdcard/config/playback_order.txt, one filename per line, falls back to directory listing if file not found
std::array<std::array<char, SDCard::kMaxPathLength>, SDCard::kMaxFiles> SDCard::get_ordered_mp3_files() {
  std::array<std::array<char, kMaxPathLength>, kMaxFiles> files{};
  size_t file_count = 0;

  // Build config file path for playback order
  char order_path[kMaxPathLength];
  const size_t base_len = strlen(mount_point_.data());
  if (base_len + 26 >= kMaxPathLength) {  // 26 for "/config/playback_order.txt\0"
    LOG("Mount point path too long for playback order file");
    return files;
  }
  strcpy(order_path, mount_point_.data());
  strcat(order_path, "/config/playback_order.txt");

  // Try to open the playback order file
  FILE* f = fopen(order_path, "r");
  if (!f) {
    LOG("No playback order file found, falling back to directory listing");
    return list_all_mp3_files();
  }

  // Read filenames from the order file
  char line[256];
  while (fgets(line, sizeof(line), f) && file_count < kMaxFiles) {
    // Remove newline character
    line[strcspn(line, "\n")] = '\0';
    
    // Skip empty lines
    if (strlen(line) == 0) {
      continue;
    }

    // Build full path to the file
    const size_t music_dir_len = base_len + 7;  // "/music\0"
    const size_t line_len = strlen(line);
    if (music_dir_len + 1 + line_len >= kMaxPathLength) {  // +1 for '/'
      LOG("Path too long for file: %s", line);
      continue;
    }

    char* dest = files[file_count].data();
    strcpy(dest, mount_point_.data());
    strcat(dest, "/music/");
    strcat(dest, line);
    
    // Verify the file exists using C++ filesystem (non-throwing)
    std::error_code ec;
    if (std::filesystem::exists(dest, ec) && !ec) {
      file_count++;
    } else {
      LOG("File in playback order not found: %s", dest);
    }
  }

  fclose(f);
  return files;
}

/// @brief get the current mount point for ESP-ADF integration
/// @return string_view of the mount point path
/// @note provides mount point for ESP-ADF fatfs_stream configuration
std::string_view SDCard::get_mount_point() const {
  return std::string_view(mount_point_.data());
}

