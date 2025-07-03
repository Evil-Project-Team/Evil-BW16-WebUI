<div align="center">

![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg) ![GitHub issues](https://img.shields.io/github/issues/Evil-Project-Team/Evil-BW16-WebUI/issues) ![GitHub stars](https://img.shields.io/github/stars/Evil-Project-Team/Evil-BW16-WebUI?style=social) ![GitHub forks](https://img.shields.io/github/forks/Evil-Project-Team/Evil-BW16-WebUI?style=social)
  
# Evil-BW16 WebUI 🚀

![Project Logo](UI/static/logo.png)

A powerful WiFi deauthentication tool running on BW16 and ESP32 platforms, featuring:

</div>

- ⚡ **Dual-band** 2.4GHz/5GHz support  
- 🌐 **Web-based** control interface  
- 🔧 **Real-time** UART communication  
- 💡 **UART Bridge** bridge functionality  
- 💾 **SD card** file management  
- 🔗 **WebSocket**-based communication  
- 📁 **JSON** configuration handling  
- 🕵️ **Integrated Sniffer** for beacon/probe/deauth frames  

---

## Table of Contents 📖

1. [Features](#features-)
2. [Hardware Requirements](#hardware-requirements-)
   - [BW16 Setup](#bw16-setup-)
   - [ESP32 Setup](#esp32-setup-)
3. [Installation](#installation-)
   - [Development Environment Setup](#development-environment-setup-)
   - [BW16 Firmware Installation](#bw16-firmware-installation-)
   - [ESP32 Web Interface Installation](#esp32-web-interface-installation-)
4. [Web Interface Usage](#web-interface-usage-)
5. [Advanced Configuration](#advanced-configuration-)
   - [Command Reference](#command-reference-)
6. [File Structure](#file-structure-)
7. [Troubleshooting](#troubleshooting-)
8. [Development](#development-)
   - [Contributing](#contributing-)
9. [Legal Disclaimer](#legal-disclaimer-)
10. [License](#license-)
11. [Acknowledgments](#acknowledgments-)
12. [Credits](#credits-)

---

## Features ⚡

- **Dual-Band Support**: Target both 2.4GHz and 5GHz networks.
- **Web Interface**: Control the device through a modern web interface.
- **Real-Time Monitoring**: View network activity and attack status in real-time.
- **Customizable Attacks**: Configure attack parameters and targets.
- **SD Card Storage**: Save logs and configurations to SD card.
- **UART Bridge**: Read UART from UART or Serial USB.
- **Advanced Packet Injection**: Precise control over frame timing and content.
- **Channel Hopping**: Automatic channel switching for comprehensive coverage.
- **MAC Address Filtering**: Target specific devices or networks.
- **Power Management**: Optimized power consumption for extended operation.
- **Enhanced Sniffer**: Capture and analyze beacon, probe, deauth, and EAPOL frames.

---

## Hardware Requirements 🔌

### BW16 Setup 🤖

- **Ai-Thinker BW16** development board  
- **Power supply** (5V/3.3V, 2A minimum)    
- *Optional:* External battery pack (18650 Li-ion recommended)

### ESP32 Setup 🌐

- **ESP32** development board (ESP32-S3 recommended)  
- **MicroSD card module** (SPI interface)  
- **Power supply** (5V/3.3V, 2A minimum)  
- *Optional:* External battery pack (18650 Li-ion recommended)

---

## Installation 🛠

### Development Environment Setup 💻

1. **Install Arduino IDE** (1.8.19 or later).  
   - If using Arduino IDE 2.0 or newer, ensure that the Board Manager and Library Manager references match your environment.

2. **Add board support**:  
   - **BW16**: In the Arduino IDE preferences, add:  
     ```
     https://raw.githubusercontent.com/Ai-Thinker-Open/Ai-Thinker-Open.github.io/master/package_ai-thinker_index.json
     ```
   - **ESP32**: In the Arduino IDE preferences, add:  
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```

3. **Install required libraries**:
   - [ESPAsyncWebServer](https://github.com/me-no-dev/ESPAsyncWebServer) (version 3.4.0 or later)
   - [AsyncTCP](https://github.com/me-no-dev/AsyncTCP) (version 3.2.15 or later)
   - [ArduinoJson](https://arduinojson.org/) (version 7.2.1 or later)
   - **WiFi** (built-in for ESP32; for BW16, ensure the WiFi library is included in the board support package)
   - **SD** (built-in for ESP32)

> **Note**: Make sure to select the correct libraries in the Arduino Library Manager and match the required versions if possible.

---

### BW16 Firmware Installation 📲

1. **Connect the BW16 board** via the USB-to-Serial adapter (TX → RX, RX → TX, GND → GND).
2. **Open the sketch**:  
   `Evil-BW16/Evil-BW16.ino` in the Arduino IDE.
3. **Select board**:  
   `Tools > Board > Ai-Thinker BW16`.
4. **Configure settings**:
   - Upload Speed: `921600`
   - CPU Frequency: `160MHz`
   - Flash Size: `4MB (32Mb)`
5. **Upload** the sketch to the BW16 board.

---

### ESP32 Web Interface Installation 🌐

1. **Connect the ESP32 board** via USB.
2. **Open the sketch**:  
   `ESP32/ESP32.ino` in the Arduino IDE.
3. **Select board**:  
   `Tools > Board > ESP32 Dev Module`.
4. **Configure settings**:
   - Upload Speed: `921600`
   - CPU Frequency: `240MHz`
   - Flash Size: `4MB (32Mb)`

5. **Upload** the sketch to the ESP32.

---

## Web Interface Usage 💡

1. **Power on** the device (BW16 + ESP32).
2. **Connect to the WiFi access point**:  
   - **SSID**: `Evil-BW16`  
   - **Password**: `password1234`
3. Open a web browser and navigate to:  
   `http://192.168.4.1`
4. **Access and control** the interface to:
   - **Scan** for networks
   - **Select** AP/clients as targets
   - **Configure** deauth parameters
   - **Start/Stop** attacks
   - **Monitor** attack logs and network status
   - **Adjust** advanced settings (sniffer, beacon capture, channel hopping, etc.)

---

## Advanced Configuration ⚙️

Configuration is primarily handled in **ESP32.ino** through hardcoded constants.

```cpp
// Access Point Credentials
const char* ap_ssid     = "Evil-BW16";
const char* ap_password = "password1234";

// UART Configuration
const size_t UART_BUFFER_SIZE = 512;
const size_t UART_CHUNK_SIZE  = 256;
const int UART_BAUD_RATE      = 115200;
```

To modify these settings:

1. Open `ESP32/ESP32.ino` in the Arduino IDE.
2. Update the relevant constants near the top of the file.
3. Recompile and upload.

---

### Command Reference 📝

> **Note**: Commands can be sent via the web UI console or UART terminal.

#### Basic Commands

- `scan`  
  Perform a WiFi scan.
- `results`  
  Show the last scan results.
- `start`  
  Start deauthentication attack.
- `stop`  
  Stop all attacks.
- `disassoc`  
  Start continuous disassociation.
- `random`  
  Attack a random AP.
- `help`  
  Show command reference.

#### Configuration Commands

- `set cycle_delay <ms>`  
  Set delay between cycles (in milliseconds).
- `set scan_time <ms>`  
  Set WiFi scan duration.
- `set num_frames <n>`  
  Set number of frames per AP.
- `set start_channel <n>`  
  Set start channel (1 for 2.4GHz, 36 for 5GHz).
- `set scan_cycles <on/off>`  
  Enable/disable scan between cycles.
- `set led <on/off>`  
  Enable/disable onboard LEDs.
- `set debug <on/off>`  
  Enable/disable debug mode.

#### Sniffer Commands

- `start sniff`  
  Enable packet sniffing.
- `sniff beacon`  
  Capture beacon frames.
- `sniff probe`  
  Capture probe requests/responses.
- `sniff deauth`  
  Capture deauth/disassoc frames.
- `sniff eapol`  
  Capture EAPOL frames.
- `sniff pwnagotchi`  
  Capture Pwnagotchi beacons.
- `sniff all`  
  Capture all frames.
- `stop sniff`  
  Stop sniffing.

---

## File Structure 📂

Below is the repository layout:

```
.
├── Evil-BW16/
│   └── Evil-BW16.ino       # Main BW16 firmware
├── ESP32/
│   └── ESP32.ino            # Web interface code for ESP32
├── UI/
│   ├── index.html                 # Main web interface
│   └── static/
│       ├── bootstrap-icons.css
│       ├── bootstrap.bundle.min.js
│       ├── bootstrap.min.css
│       ├── bootstrap.min.js
│       ├── logo.png
│       ├── rbbt.PNG
│       ├── script.js
│       ├── socket.io.min.js
│       ├── style.css
│       ├── welcome_msg.txt
│       └── fonts/
│           ├── bootstrap-icons.woff
│           └── bootstrap-icons.woff2
├── LICENSE
└── README.md                      # This file
```

---

## Troubleshooting 🏮

### 1. Device Not Responding

- Check power supply and verify at least **3.3V/5V** at **2A** or more.
- Ensure correct **serial connection** (TX ↔ RX, GND ↔ GND).
- Verify the **baud rate** (default: 115200).

### 2. Web Interface Not Loading

- Confirm you are connected to the **Evil-BW16** SSID.
- Verify the **IP address** is `192.168.4.1`.
- Try clearing your **browser cache** or use a different browser.

### 3. SD Card Not Detected (ESP32)

- Check **SPI** wiring (MISO, MOSI, SCK, CS).
- Ensure SD card is formatted as **FAT32**.
- Check logs for any **SD mount** errors.

### 4. Performance Issues

- Reduce the **number of targets** or channels.
- Increase the **cycle delay** between attacks.
- Use a stable **external power source** (battery pack or regulated supply).

---

## Development 🔭

### Contributing 🤝

1. **Fork** the repository.
2. **Create** a new branch with a descriptive name.
3. **Make** your changes or additions.
4. **Submit** a pull request for review.

We welcome improvements to documentation, bug fixes, new features, or general enhancements.

---

## Legal Disclaimer ⚠️

> This tool is created for **educational** and **ethical testing** purposes only.  
> Any misuse or illegal use of this tool is **strictly prohibited**.  
> The creators and contributors assume **no liability** for misuse or damage caused by this tool.  
> Users must comply with **all applicable laws and regulations** in their jurisdiction regarding network testing and ethical hacking.

---

## License 📝

This project is licensed under the **MIT License**.  
See the [LICENSE](LICENSE) file for full details.

---

## Acknowledgments 🌟

- **ESP32 Arduino Core** team
- **ESPAsyncWebServer** library maintainers
- **ArduinoJson** library contributors
- **BW16** development community
- **OpenWRT** project
- **Aircrack-ng** team

---

## Credits ❤️

- **Developers:**  
  - 🛠 [7h30th3r0n3](https://github.com/7h30th3r0n3)  
  - 🤖 [dagnazty](https://github.com/dagnazty)  
  - 🦾 [Hosseios](https://github.com/Hosseios)

---
