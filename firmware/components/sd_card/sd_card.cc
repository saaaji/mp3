#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <string_view>

#include "include/sd_card.hpp"

namespace {

constexpr const char* kComponentTag = "SdCardObject";
constexpr std::string_view kConfigPath = "/config/playback_order.txt";

struct FileGuard {
  void operator()(FILE* file) const noexcept {
    if (file) fclose(file);
  }
};

}

SdCardObject::SdCardObject(const Config& config)
  : ActiveObject("SdCardObject", ActiveObject::MemoryLoad::kStandard, ActiveObject::Priority::kHigh, 1000),
    config_(config) {}

SdCardObject::~SdCardObject() {
  unmount();
}

void SdCardObject::initialize() {
  // Initialize SPI bus
  spi_bus_config_t bus_config;
  std::memset(&bus_config, 0, sizeof(bus_config));

  // copy config values
  bus_config.mosi_io_num = config_.mosi;
  bus_config.miso_io_num = config_.miso;
  bus_config.sclk_io_num = config_.sck;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  bus_config.max_transfer_sz = 4000;

  // try to initialize SPI bus
  ESP_ERROR_CHECK(spi_bus_initialize(
    SDSPI_DEFAULT_HOST, 
    &bus_config, 
    SDSPI_DEFAULT_DMA
  ));

  // try to initialize SD card
  sdmmc_host_t host = SDSPI_HOST_DEFAULT();
  sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
  
  slot_config.gpio_cs = config_.cs;
  slot_config.host_id = SDSPI_DEFAULT_HOST;

  // FAT mount config
  esp_vfs_fat_sdmmc_mount_config_t mount_config = {
    .format_if_mount_failed = config_.format_if_mount_failed,
    .max_files = config_.max_open_files,
    .allocation_unit_size = 16 * 1024,
    .disk_status_check_enable = false,
    .use_one_fat = false
  };
  
  ESP_ERROR_CHECK(esp_vfs_fat_sdspi_mount(
    mount_point_.data(), 
    &host, 
    &slot_config, 
    &mount_config, 
    &card_
  ));
  
  ESP_LOGI(kComponentTag, "SD card mount was successful");
  
  // Create required directories
  assert(create_directories());
  
  // Get and display all discovered MP3 files
  file_paths_ = get_mp3_files();
  ESP_LOGI(kComponentTag, "MP3 files on SD card:");
  for (const auto& file : file_paths_) {
    ESP_LOGI(kComponentTag, "\t%s", file.c_str());
  }
  
  // Read playback order
  queue_ = read_playback_order();
  ESP_LOGI(kComponentTag, "Found %zu files in playback order:", queue_.size());
  for (const auto& file : queue_) {
    ESP_LOGI(kComponentTag, "\t%s", file.c_str());
  }
  
  ESP_LOGI(kComponentTag, "SD card initialization complete");
}

void SdCardObject::task() {
  // Periodic SD card monitoring (no infinite loop - framework handles timing)
  // Could check card status, file system health, etc.
  // For now, just a placeholder
  mark_as_done();
}

bool SdCardObject::mount() {
  // Mount is handled in initialize()
  return card_ != nullptr;
}

void SdCardObject::unmount() {
  if (card_) {
    ESP_ERROR_CHECK(esp_vfs_fat_sdcard_unmount(mount_point_.data(), card_));
    card_ = nullptr;
  }
}

std::vector<std::string> SdCardObject::get_mp3_files() {
  std::vector<std::string> files;
  std::error_code ec;

  const auto music_path = std::filesystem::path(mount_point_.data()) / "music";
  std::filesystem::directory_iterator music_iter(music_path, ec), end{};

  if (ec) {
    ESP_LOGE(kComponentTag, "Could not initialize directory_iterator: '%s'", music_path.c_str());
    return {};
  }

  while (music_iter != end) {
    const auto& entry = *music_iter;

    if (entry.is_regular_file() && entry.path().extension() == ".mp3") {
      files.push_back(entry.path());
    }

    // move to next entry
    music_iter.increment(ec);
    if (ec) {
      ESP_LOGE(kComponentTag, "Increment error: %s", ec.message().c_str());
    }
  }

  return files;
}

std::vector<std::string> SdCardObject::read_playback_order() {
  std::vector<std::string> order;

  const auto order_path = std::filesystem::path(mount_point_.data()) / kConfigPath;
  if (std::filesystem::exists(order_path)) {
    // open the file
    std::unique_ptr<FILE, FileGuard> file{fopen(order_path.c_str(), "r")};
    
    if (!file.get()) {
      ESP_LOGE(kComponentTag, "Playback order file could not be opened, defaulting to filesystem order");
      return get_mp3_files();
    }

    // Read file names from the order file
    std::array<char, kMaxPathLength> line{'\0'};
    while (fgets(line.data(), line.size(), file.get())) {
      // Remove newline
      line.at(strcspn(line.data(), "\n")) = '\0';
      
      if (strlen(line.data()) > 0) {
        order.emplace_back(line.data());
      }
    }
    
    return order;
  } else {
    ESP_LOGI(kComponentTag, "No playback order specified, defaulting to filesystem order");
    return get_mp3_files();
  }
}

bool SdCardObject::create_directories() {
  for (const auto& name : {"music", "config"}) {
    const auto path = std::filesystem::path(mount_point_.data()) / name;
    
    std::error_code ec;
    if (!std::filesystem::create_directory(path, ec)) {
      if (ec) {
        ESP_LOGE(kComponentTag, "Could not create directory '%s': %s", path.c_str(), ec.message().c_str());
        return false;
      } else {
        ESP_LOGI(kComponentTag, "Directory '%s' already exists", path.c_str());
      }
    }
  }

  return true;
}

