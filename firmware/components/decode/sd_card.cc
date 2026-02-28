#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <system_error>
#include <string_view>

#define MINIMP3_IMPLEMENTATION
#include "minimp3.h"

#include "sd_card.hpp"

namespace {

constexpr const char* kComponentTag = "SdCardObject";
constexpr std::string_view kConfigPath = "/config/playback_order.txt";
constexpr const int kThreadPeriodMs = 5;

static mp3dec_t dec_;
static std::array<std::uint8_t, 16 * 1024> mp3_buf_;
static std::array<std::int16_t, MINIMP3_MAX_SAMPLES_PER_FRAME> pcm_buf_;

}

SdCardObject::SdCardObject(const Config& config, std::shared_ptr<rtos::StreamingQueue> audio_queue)
  : ActiveObject("SdCardObject", ActiveObject::MemoryLoad::kHeavy2, ActiveObject::Priority::kHigh, kThreadPeriodMs),
    config_(config),
    audio_queue_(audio_queue) {}

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
  // file_paths_ = get_mp3_files();
  // ESP_LOGI(kComponentTag, "MP3 files on SD card:");
  // for (const auto& file : file_paths_) {
  //   ESP_LOGI(kComponentTag, "\t%s", file.c_str());
  // }
  
  // Read playback order
  auto paths = read_playback_order();
  queue_ = std::deque<std::string>(paths.begin(), paths.end());

  ESP_LOGI(kComponentTag, "Found %zu files in playback order:", queue_.size());
  for (const auto& file : queue_) {
    ESP_LOGI(kComponentTag, "\t%s", file.c_str());
  }

  ESP_LOGI(kComponentTag, "SD card initialization complete");
}

void SdCardObject::task() {
  static size_t mp3_data_size = 0;

  if (!file_) {
    if (queue_.empty()) return;

    const auto& path = queue_.front();
    file_.reset(fopen(path.c_str(), "rb"));

    if (!file_) {
      ESP_LOGE(kComponentTag, "could not open: %s", path.c_str());
      queue_.pop_front();
      return;
    }

    mp3dec_init(&dec_);
    mp3_data_size = 0;

    ESP_LOGI(kComponentTag, "decoding: %s", path.c_str());
  }

  // Read new data
  size_t read = fread(mp3_buf_.data() + mp3_data_size,
                      1,
                      mp3_buf_.size() - mp3_data_size,
                      file_.get());

  // ESP_LOG_BUFFER_HEX(kComponentTag, mp3_buf_.data(), 16);

  if (read == 0 && mp3_data_size_ == 0) {
      file_.reset();
      queue_.push_back(queue_.front());
      queue_.pop_front();
      return;
  }

  mp3_data_size += read;

  size_t offset = 0;

  while (offset < mp3_data_size) {
      mp3dec_frame_info_t info;
      int samples = mp3dec_decode_frame(
          &dec_,
          mp3_buf_.data() + offset,
          mp3_data_size - offset,
          pcm_buf_.data(),
          &info
      );

      ESP_LOGI(kComponentTag, "INFO[bytes = %d, samples = %d]", info.frame_bytes, samples);

      if (info.frame_bytes == 0)
          break;

      offset += info.frame_bytes;

      if (samples > 0) {
          audio_queue_->write(std::span<uint8_t>(
              reinterpret_cast<uint8_t*>(pcm_buf_.data()),
              samples * info.channels * sizeof(int16_t)
          ));
      }
  }

  // Preserve leftovers
  mp3_data_size -= offset;
  memmove(mp3_buf_.data(),
          mp3_buf_.data() + offset,
          mp3_data_size);

  // mark_as_done();
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

