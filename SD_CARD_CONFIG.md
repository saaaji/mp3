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

## Future Considerations
- Playlist support via .m3u files
- Support for nested directories
- Metadata parsing from MP3 files
- Configuration for other settings (volume, equalizer, etc.)
