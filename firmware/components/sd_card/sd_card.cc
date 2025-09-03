#include "include/sd_card.hpp"
#include <array>
#include <cstring>
#include <unistd.h>  // for access()
#include <dirent.h>  // for directory operations

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

  // Open the directory
  DIR* dir = opendir(music_dir);
  if (!dir) {
    LOG("Failed to open music directory: %s", music_dir);
    return files;
  }

  // Read directory entries
  while (struct dirent* entry = readdir(dir)) {
    if (file_count >= kMaxFiles) {
      break;
    }

    // Check if file ends with .mp3
    const char* name = entry->d_name;
    const size_t name_len = strlen(name);
    if (name_len > 4 && strcmp(name + name_len - 4, ".mp3") == 0) {
      // Check if full path will fit
      const size_t full_len = strlen(music_dir) + 1 + name_len;  // +1 for '/'
      if (full_len >= kMaxPathLength) {
        LOG("Path too long for file: %s", name);
        continue;
      }
      
      char* dest = files[file_count].data();
      strcpy(dest, music_dir);
      strcat(dest, "/");
      strcat(dest, name);
      file_count++;
    }
  }

  closedir(dir);
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
    
    // Verify the file exists
    if (access(dest, F_OK) == 0) {
      file_count++;
    } else {
      LOG("File in playback order not found: %s", dest);
    }
  }

  fclose(f);
  return files;
}

/// @brief read bluetooth mac address from config file
/// @param mac_address array to store the 6-byte mac address
/// @return true if mac address was successfully read and parsed
/// @note expects XX:XX:XX:XX:XX:XX format in /sdcard/config/bt_config.txt
bool SDCard::read_bluetooth_config(std::array<uint8_t, 6>& mac_address) {
  // Build config file path
  char config_path[kMaxPathLength];
  const size_t base_len = strlen(mount_point_.data());
  if (base_len + 22 >= kMaxPathLength) {  // 22 for "/config/bt_config.txt\0"
    LOG("Mount point path too long for config file");
    return false;
  }
  strcpy(config_path, mount_point_.data());
  strcat(config_path, "/config/bt_config.txt");
  
  FILE* f = fopen(config_path, "r");
  if (!f) {
    LOG("Failed to open bluetooth config file: %s", config_path);
    return false;
  }

  // Read MAC address in format "XX:XX:XX:XX:XX:XX"
  char mac_str[18];
  if (fgets(mac_str, sizeof(mac_str), f) == nullptr) {
    fclose(f);
    return false;
  }
  fclose(f);

  // Parse MAC address
  unsigned int values[6];
  if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x", 
             &values[0], &values[1], &values[2],
             &values[3], &values[4], &values[5]) != 6) {
    return false;
  }

  // Convert to bytes
  for (int i = 0; i < 6; i++) {
    mac_address[i] = static_cast<uint8_t>(values[i]);
  }

  return true;
}
