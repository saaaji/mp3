# MP3 Player Project

## Project Idea
This project aims to create a Bluetooth enabled MP3 player using the ESP32, with the following features:
- SD card storage for MP3 files 
- Bluetooth audio output to earbuds/headphones
- Wifi Access point for configuring bluetooth device MAC adress and other settings
- Physical controls (subject to change):
  - Power switch (on/off)
  - Play/pause button
  - Next track button
  - Previous track button
  - Volume up button
  - Volume down button

## Project Structure
This project uses the ESP IDF and FreeRTOS for real time operations. The project is organized with a CMake based build system and follows the standard ESP IDF project structure.

### Building and Flashing Firmware
> Hardware versions will eventually be tracked and instructions may change. The current instructions assume possession of an ESP32 devboard for prototyping. 
1. [Setup `esp-idf` toolchain.](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/linux-macos-setup.html#get-started-linux-macos-first-steps)
    - It is probably a good idea to add the `get_idf` command to your shell. 
2. In the root of the repository, run `get_idf` if you haven't already. 
    - If `sdkconfig` is missing, you may need to run: 
        - `idf.py set-target esp32`
        - `idf.py menuconfig`
3. From inside the `firmware/` directory: 
    - Run `idf.py build`
    - Run `idf.py -p <serial_port> flash monitor` to flash and monitor subsequent output
    - Exit the monitor via `ctrl-[`

### Architecture
> Changes to the project architecture should eventually be documented under the `hardware/` directory. 
- `v0 [prototype]` (ESP32 + spi-SD)