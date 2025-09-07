#include "include/sd_card.hpp"
#include <array>
#include <cstring>
#include <unistd.h>  // for access()
#include <dirent.h>  // for directory operations
#include <sys/stat.h>  // for mkdir

SDCard::SDCard(const Config& config)
  : Component("SDCard", Component::MemoryLoad::kStandard, Component::Priority::kHigh, 1000, false),
    config_(config) {}

SDCard::~SDCard() {
  unmount();
}

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
  slot_config.gpio_cs = static_cast<gpio_num_t>(config_.cs);
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
  
  LOG("SD card mounted successfully!");
  
  // Create required directories
  create_directories();
  
  // Discover MP3 files during initialization
  discover_mp3_files();
  LOG("Found %zu MP3 files", file_count_);
  
  // Get and display all discovered MP3 files
  auto mp3_files = get_mp3_files();
  LOG("MP3 files on SD card:");
  for (const auto& file : mp3_files) {
    LOG("  %s", file.c_str());
  }
  
  // Read playback order
  auto playback_order = read_playback_order();
  LOG("Found %zu files in playback order:", playback_order.size());
  for (const auto& file : playback_order) {
    LOG("  %s", file.c_str());
  }
  
  LOG("SD card initialization complete");
}

void SDCard::task_impl() {
  // Periodic SD card monitoring (no infinite loop - framework handles timing)
  // Could check card status, file system health, etc.
  // For now, just a placeholder
}

bool SDCard::mount() {
  // Mount is handled in initialize()
  return card_ != nullptr;
}

void SDCard::unmount() {
  if (card_) {
    esp_vfs_fat_sdcard_unmount(mount_point_.data(), card_);
    card_ = nullptr;
  }
}

std::size_t SDCard::discover_mp3_files() {
  file_count_ = 0;
  
  // Build music directory path
  char music_dir[kMaxPathLength];
  strlcpy(music_dir, mount_point_.data(), sizeof(music_dir));
  strlcat(music_dir, "/music", sizeof(music_dir));

  // Open the directory
  DIR* dir = opendir(music_dir);
  if (!dir) {
    LOG("Failed to open music directory: %s", music_dir);
    return 0;
  }

  // Read directory entries
  while (struct dirent* entry = readdir(dir)) {
    if (file_count_ >= kMaxFiles) {
      break;
    }

    // Check if file ends with .mp3
    const char* name = entry->d_name;
    const size_t len = strlen(name);
    if (len > 4 && strcmp(name + len - 4, ".mp3") == 0) {
      strlcpy(files[file_count_].data(), music_dir, kMaxPathLength);
      strlcat(files[file_count_].data(), "/", kMaxPathLength);
      strlcat(files[file_count_].data(), name, kMaxPathLength);
      file_count_++;
    }
  }

  closedir(dir);
  return file_count_;
}

std::vector<std::string> SDCard::get_mp3_files() const {
  std::vector<std::string> result;
  for (std::size_t i = 0; i < file_count_; ++i) {
    result.emplace_back(files[i].data());
  }
  return result;
}

std::vector<std::string> SDCard::read_playback_order() {
  std::vector<std::string> order;
  
  // Try to open the playback order file
  char order_path[kMaxPathLength];
  strlcpy(order_path, mount_point_.data(), sizeof(order_path));
  strlcat(order_path, "/config/playback_order.txt", sizeof(order_path));
  
  FILE* f = fopen(order_path, "r");
  if (!f) {
    LOG("No playback order file found, using discovery order");
    return get_mp3_files();
  }

  // Read file names from the order file
  char line[kMaxPathLength];
  while (fgets(line, sizeof(line), f) && order.size() < kMaxFiles) {
    // Remove newline
    line[strcspn(line, "\n")] = 0;
    if (strlen(line) > 0) {
      order.emplace_back(line);
    }
  }
  
  fclose(f);
  return order;
}

void SDCard::create_directories() {
  // Create music directory
  char music_dir[kMaxPathLength];
  strlcpy(music_dir, mount_point_.data(), sizeof(music_dir));
  strlcat(music_dir, "/music", sizeof(music_dir));
  
  // Create config directory  
  char config_dir[kMaxPathLength];
  strlcpy(config_dir, mount_point_.data(), sizeof(config_dir));
  strlcat(config_dir, "/config", sizeof(config_dir));
  
  // Create directories (mkdir returns 0 on success, -1 if already exists)
  mkdir(music_dir, 0755);
  mkdir(config_dir, 0755);
}

