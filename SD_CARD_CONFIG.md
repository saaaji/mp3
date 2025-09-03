# SD Card Configuration Guide

## File System Requirements
- Format: FAT32
- Maximum number of files supported: 32
- Maximum path length: 128 characters

## Directory Structure
```
/sdcard/                    # Root directory (mount point)
├── music/                  # Directory containing MP3 files
│   ├── song1.mp3
│   ├── song2.mp3
│   └── ...
└── config/                # Configuration directory
    ├── bt_config.txt      # Bluetooth configuration file
    └── playback_order.txt # Playback order configuration
```

## Configuration Files

### Playback Order (config/playback_order.txt)
- One MP3 filename per line
- Files must exist in the music directory
- Example:
  ```
  song1.mp3
  song2.mp3
  song3.mp3
  ```
- If a file is not listed, it won't be played
- Empty lines are ignored
- Files are played in the order listed

## File Specifications

### MP3 Files
- Location: `/sdcard/music/*.mp3`
- Format: Standard MP3 files
- Naming: Any valid FAT32 filename
- Recommendation: Use simple filenames without special characters

### Bluetooth Configuration
- File: `/sdcard/bt_config.txt`
- Format: Single line containing MAC address
- MAC Address Format: `XX:XX:XX:XX:XX:XX`
  - Example: `A4:C1:38:8D:12:34`
- Each byte must be in hexadecimal format
- Bytes must be separated by colons
- Case insensitive (both `a4` and `A4` are valid)

## Preparing the SD Card
1. Format the SD card as FAT32
2. Create the directory structure:
   ```bash
   mkdir -p /sdcard/music
   mkdir -p /sdcard/playlists
   ```
3. Create bt_config.txt with your earbuds' MAC address:
   ```bash
   echo "XX:XX:XX:XX:XX:XX" > /sdcard/bt_config.txt
   ```
4. Copy your MP3 files to the `/sdcard/music/` directory

## Error Handling
- If `bt_config.txt` is missing or malformed:
  - Device will not attempt Bluetooth connection
  - Error will be logged
- If no MP3 files are found:
  - Empty file list will be returned
  - Error will be logged

## Design Considerations & Future Changes

### Bluetooth MAC Storage Migration
**Current Implementation**: Bluetooth MAC address stored in `/sdcard/config/bt_config.txt`

**Planned Change**: Move Bluetooth MAC storage to ESP32's built-in NVS (Non-Volatile Storage)

**Rationale**:
- **Thread Safety**: Prevents data races between SD card component and WiFi component
- **Corruption Prevention**: Eliminates risk of SD card corruption from concurrent access
- **Lock-Free Design**: Avoids need for explicit synchronization between threads
- **Reliability**: NVS is independent of removable SD card storage
- **Capacity**: Several KB available in NVS (more than sufficient for MAC addresses)

### File System API Migration
**Current Implementation**: Uses C libraries (`dirent.h`, `opendir`, `readdir`)

**Planned Change**: Migrate to C++ `<filesystem>` header with non-throwing variants

**Rationale**:
- **Error Handling**: Less prone to programmer errors in checking return codes
- **Modern C++**: Better integration with C++ codebase
- **Exception Safety**: Use non-throwing variants as recommended by ESP-IDF docs
- **Reference**: [ESP-IDF C++ Filesystem Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/cplusplus.html#cplusplus-filesystem)

### Thread Safety Strategy
**General Principle**: Decouple components and avoid shared resources where possible

**Shared Resource Guidelines**:
- **Avoid**: Multiple threads accessing SD card simultaneously
- **Prefer**: Lock-free designs with independent storage (NVS vs SD)
- **When Necessary**: Use RTOS queues or mutexes for explicit thread safety
- **Example**: SD component → Playback component data transfer will need synchronization

## Future Considerations
- Playlist support via .m3u files
- Support for nested directories
- Metadata parsing from MP3 files
- Configuration for other settings (volume, equalizer, etc.)
- Migration to NVS for configuration storage
- C++ filesystem API adoption
