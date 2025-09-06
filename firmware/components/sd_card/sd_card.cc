#include "sd_card.hpp"
#include <sys/stat.h>
#include <dirent.h>
#include <string.h>
#include <algorithm>

extern "C" {
#include "driver/spi_common.h"
#include "driver/sdspi_host.h"
}

SDCard::SDCard(const Config& config)
  : Component("SDCard", Component::MemoryLoad::kStandard, Component::Priority::kHigh, false),
    config_(config) {}

SDCard::~SDCard() {
  if (card_) {
    esp_vfs_fat_sdcard_unmount(mount_point_.data(), card_);
    if (config_.interface == Interface::SPI) {
      spi_bus_free(SDSPI_DEFAULT_HOST);
    }
  }
}

void SDCard::initialize() {
  esp_err_t ret = initialize_spi_interface();
  if (ret != ESP_OK) {
    card_ = nullptr;
  }
}

esp_err_t SDCard::initialize_spi_interface() {
  // Official ESP-IDF SDSPI approach
  
  // Initialize SPI bus
  spi_bus_config_t bus_cfg = {};
  bus_cfg.mosi_io_num = config_.mosi;
  bus_cfg.miso_io_num = config_.miso;
  bus_cfg.sclk_io_num = config_.sck;
  bus_cfg.quadwp_io_num = -1;
  bus_cfg.quadhd_io_num = -1;
  bus_cfg.max_transfer_sz = 4000;
  
  esp_err_t ret = spi_bus_initialize(SDSPI_DEFAULT_HOST, &bus_cfg, SDSPI_DEFAULT_DMA);
  if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
    return ret;
  }

  // This initializes the slot without card detect (CD) and write protect (WP) signals.
  // Modify slot_config.gpio_cd and slot_config.gpio_wp if your board has these signals.
  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  slot_config.gpio_cs = (gpio_num_t)config_.cs;
  slot_config.host_id = SDSPI_DEFAULT_HOST;

  // Options for mounting the filesystem.
  esp_vfs_fat_mount_config_t mount_config = {
    .format_if_mount_failed = false,
    .max_files = config_.max_open_files,
    .allocation_unit_size = 16 * 1024,
    .disk_status_check_enable = false,
    .use_one_fat = false
  };

  const char* base_path = mount_point_.data();
  
  // Use settings defined above to initialize SD card and mount FAT filesystem.
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  ret = esp_vfs_fat_sdspi_mount(base_path, &host, &slot_config, &mount_config, &card_);

  if (ret != ESP_OK) {
    if (ret == ESP_FAIL) {
      // Failed to mount filesystem
    } else {
      // Failed to initialize the card
    }
    spi_bus_free(SDSPI_DEFAULT_HOST);
    return ret;
  }

  // Card has been initialized, print its properties
  if (card_) {
    // Create required directories
    create_directories();
  }

  return ret;
}

void SDCard::create_directories() {
  struct stat st;
  
  // Create music directory
  char music_dir[kMaxPathLength];
  strlcpy(music_dir, mount_point_.data(), sizeof(music_dir));
  strlcat(music_dir, "/music", sizeof(music_dir));
  if (stat(music_dir, &st) != 0) {
    mkdir(music_dir, 0755);
  }
  
  // Create config directory
  char config_dir[kMaxPathLength];
  strlcpy(config_dir, mount_point_.data(), sizeof(config_dir));
  strlcat(config_dir, "/config", sizeof(config_dir));
  if (stat(config_dir, &st) != 0) {
    mkdir(config_dir, 0755);
  }
}

bool SDCard::mount() {
  return card_ != nullptr;
}

void SDCard::unmount() {
  if (card_) {
    esp_vfs_fat_sdcard_unmount(mount_point_.data(), card_);
    card_ = nullptr;
    if (config_.interface == Interface::SPI) {
      spi_bus_free(SDSPI_DEFAULT_HOST);
    }
  }
}

std::size_t SDCard::discover_mp3_files() {
  if (!card_) {
    return 0;
  }

  char music_dir[kMaxPathLength];
  strlcpy(music_dir, mount_point_.data(), sizeof(music_dir));
  strlcat(music_dir, "/music", sizeof(music_dir));

  DIR* dir = opendir(music_dir);
  if (!dir) {
    return 0;
  }

  std::size_t file_count = 0;
  struct dirent* entry;
  
  while ((entry = readdir(dir)) != nullptr && file_count < kMaxFiles) {
    if (entry->d_type == DT_REG) {  // Regular file
      const char* ext = strrchr(entry->d_name, '.');
      if (ext && strcasecmp(ext, ".mp3") == 0) {
        strlcpy(files[file_count].data(), music_dir, kMaxPathLength);
        strlcat(files[file_count].data(), "/", kMaxPathLength);
        strlcat(files[file_count].data(), entry->d_name, kMaxPathLength);
        
        // Log each found MP3 file
        ESP_LOGI("SDCard", "Found MP3: %s", entry->d_name);
        file_count++;
      }
    }
  }
  
  closedir(dir);
  file_count_ = file_count;
  return file_count;
}

std::vector<std::string> SDCard::get_mp3_files() const {
  std::vector<std::string> result;
  for (std::size_t i = 0; i < file_count_; i++) {
    result.emplace_back(files[i].data());
  }
  return result;
}

std::vector<std::string> SDCard::read_playback_order() {
  std::vector<std::string> order;
  if (!card_) {
    return order;
  }

  char config_file[kMaxPathLength];
  strlcpy(config_file, mount_point_.data(), sizeof(config_file));
  strlcat(config_file, "/config/playback_order.txt", sizeof(config_file));

  FILE* file = fopen(config_file, "r");
  if (!file) {
    return order;
  }

  char line[256];
  while (fgets(line, sizeof(line), file) && order.size() < kMaxFiles) {
    // Remove trailing newline
    char* newline = strchr(line, '\n');
    if (newline) *newline = '\0';
    
    if (strlen(line) > 0) {
      order.emplace_back(line);
    }
  }
  
  fclose(file);
  return order;
}

void SDCard::task_impl() {
  // Wait for initialization to complete
  vTaskDelay(pdMS_TO_TICKS(200));
  
  if (card_) {
    ESP_LOGI("SDCard", "SD card mounted successfully with %s interface", 
             config_.interface == Interface::SPI ? "SPI" : "SDMMC");
    
    // Discover MP3 files
    std::size_t mp3_count = discover_mp3_files();
    ESP_LOGI("SDCard", "Found %zu MP3 files", mp3_count);
  } else {
    ESP_LOGE("SDCard", "SD card mount failed");
  }
  
  // Periodic monitoring
  while (true) {
    vTaskDelay(pdMS_TO_TICKS(10000));  // Check every 10 seconds
    if (card_) {
      // Periodic status check could go here
    }
  }
}