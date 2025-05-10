#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <driver/uart.h>
#include <driver/periph_ctrl.h>  // For periph_module_reset() and periph_module_enable()

// *****************************************************************************
//                          Macro Definitions & Constants
// *****************************************************************************
//
// (USB_DETECT_PIN remains defined for compatibility but is no longer used.)
#define USB_DETECT_PIN 0

// UART1 Configuration (for general communication)
#define UART_RX_PIN 18
#define UART_TX_PIN 17
#define UART_NUM UART_NUM_1

// UART2 Configuration (for bridging or potential USB replacement)
#define UART_RX_PIN_2 44
#define UART_TX_PIN_2 43
#define UART_NUM_2 2

// Other UART and buffer settings
const size_t UART_BUFFER_SIZE = 512;
const size_t JSON_BUFFER_SIZE = 2048;
const TickType_t UART_TIMEOUT = pdMS_TO_TICKS(100);

#define LINES_PER_BATCH    2          // Send only 2 messages per batch
#define LINE_BUFFER_SIZE   1024
#define TEMP_BUFFER_SIZE   256
#define BATCH_DELAY_MS     200        // Throttle delay after processing a batch (ms)

// Access Point (AP) settings
const char* ap_ssid = "Evil-BW16";
const char* ap_password = "password1234";

// *****************************************************************************
//                         Function Prototypes
// *****************************************************************************
String getContentType(String filename);
String getSDFileList(const char* dirname, uint8_t levels = 0);
void logSDCardFiles(const char* dirname, uint8_t levels = 0);

void onWsEvent(AsyncWebServer* server,
               AsyncWebSocketClient* client,
               AwsEventType type,
               void* arg,
               unsigned char* data,
               unsigned int len);

bool setupUART();      // Sets up UART1
bool setupUART2();     // Sets up UART2 (used for bridging and as a fallback)
bool ensureUART2Driver();  // Minimal installation of the UART2 driver
void setupWebServer();
bool sendUARTCommand(const char* command);
void sendUartDataToClients(const char* data);

// In this simplified version, we force USB mode—so isUSBConnected() always returns true.
bool isUSBConnected() { return true; }
void switchUart2Mode();  // For USB mode, this function will simply run the USB branch.
void uart_event_task(void* pvParameters);
void uart_event_task_2(void* pvParameters);
void uart_bridge_task(void* pvParameters);

// Helper function for token/message processing:
int process_bridge_direction(int dst_uart, char* buffer, size_t *buf_len);

// *****************************************************************************
//                         Global Instances and Handles
// *****************************************************************************
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
QueueHandle_t uart_queue;
QueueHandle_t uart_queue_2;

// Global flag indicating USB mode. We force this to true.
bool g_usbMode = true;

// *****************************************************************************
//                    USB Mode Setup (Forced USB Mode)
// *****************************************************************************
// In USB mode we leave the USB CDC active.
// We still install the minimal UART2 driver (for bridging or future use).
bool ensureUART2Driver() {
  esp_err_t err = uart_driver_install((uart_port_t)UART_NUM_2, 2048, 2048, 20, &uart_queue_2, 0);
  if (err == ESP_OK) {
      Serial.println("[DEBUG] UART2 driver installed (minimal).");
      return true;
  } else if (err == ESP_ERR_INVALID_STATE || err == -1) {
      Serial.println("[DEBUG] UART2 driver already installed (minimal).");
      return true;
  } else {
      Serial.printf("[ERROR] UART2 driver installation failed (minimal): %d\n", err);
      return false;
  }
}

// In USB mode, we simply call ensureUART2Driver() and print a message.
void switchUart2Mode() {
  ensureUART2Driver();
}

// *****************************************************************************
//             Token-Based Message Processing Helper Function
// *********************************************************************************
int process_bridge_direction(int dst_uart, char* buffer, size_t *buf_len) {
  // Define the tokens we care about.
  const char* tokens[] = {"[INFO]", "[ERROR]", "[DEBUG]"};
  const int numTokens = sizeof(tokens) / sizeof(tokens[0]);

  // Store up to 3 token start positions.
  int tokenIndices[3] = { -1, -1, -1 };
  int tokenCount = 0;
  int len = *buf_len;

  // Walk through the buffer to find token occurrences.
  char* p = buffer;
  while ((p - buffer) < len && tokenCount < 3) {
    int nearestPos = len;  // Assume token is at the end by default.
    bool foundAny = false;
    for (int i = 0; i < numTokens; i++) {
      char* pos = strstr(p, tokens[i]);
      if (pos != NULL) {
        int posIndex = pos - buffer;
        if (posIndex < nearestPos) {
          nearestPos = posIndex;
          foundAny = true;
        }
      }
    }
    if (foundAny) {
      tokenIndices[tokenCount++] = nearestPos;
      p = buffer + nearestPos + 1;
    } else {
      break;
    }
  }

  // If fewer than the desired number of tokens are found, wait for more data.
  if (tokenCount < LINES_PER_BATCH) {
    return 0;
  }

  int batchEnd;
  if (tokenCount >= 3) {
    batchEnd = tokenIndices[2]; // Send up to the start of the third token.
  } else {
    if (len > 0 && buffer[len - 1] == '\n') {
      batchEnd = len;
    } else {
      return 0;
    }
  }

  int bytesToSend = batchEnd;
  esp_err_t write_err = uart_write_bytes((uart_port_t)dst_uart, buffer, bytesToSend);
  if (write_err != ESP_OK) {
    Serial.printf("[ERROR] Failed to write batch to UART %d: %d\n", dst_uart, write_err);
  }
  uart_wait_tx_done((uart_port_t)dst_uart, 100 / portTICK_PERIOD_MS);

  int remaining = len - batchEnd;
  memmove(buffer, buffer + batchEnd, remaining);
  *buf_len = remaining;
  buffer[remaining] = '\0';

  return LINES_PER_BATCH; // Indicates two messages were sent.
}

// *****************************************************************************
//             Remaining Helper Functions (SD, WebSocket, etc.)
// *********************************************************************************
String getContentType(String filename) {
  if (filename.endsWith(".html")) return "text/html";
  if (filename.endsWith(".css"))  return "text/css";
  if (filename.endsWith(".js"))   return "application/javascript";
  if (filename.endsWith(".png"))  return "image/png";
  if (filename.endsWith(".woff2")) return "font/woff2";
  if (filename.endsWith(".woff"))  return "font/woff";
  if (filename.endsWith(".txt"))   return "text/plain";
  return "application/octet-stream";
}

String getSDFileList(const char* dirname, uint8_t levels) {
  String output = "[";
  File root = SD.open(dirname);
  if (!root || !root.isDirectory()) return "[]";
  File file = root.openNextFile();
  bool first = true;
  while (file) {
    if (!first) output += ",";
    first = false;
    if (file.isDirectory()) {
      output += "{\"name\":\"" + String(file.name()) + "\",\"type\":\"dir\"";
      if (levels) {
        output += ",\"children\":" + getSDFileList(file.name(), levels - 1);
      }
      output += "}";
    } else {
      output += "{\"name\":\"" + String(file.name()) + "\",\"type\":\"file\",\"size\":" + String(file.size()) + "}";
    }
    file = root.openNextFile();
  }
  output += "]";
  return output;
}

void logSDCardFiles(const char* dirname, uint8_t levels) {
  Serial.printf("[SD] Listing directory: %s\n", dirname);
  File root = SD.open(dirname);
  if (!root) {
    Serial.println("[ERROR] Failed to open directory");
    return;
  }
  if (!root.isDirectory()) {
    Serial.println("[ERROR] Not a directory");
    return;
  }
  File file = root.openNextFile();
  while (file) {
    if (file.isDirectory()) {
      Serial.printf("[SD] DIR : %s\n", file.name());
      if (levels) logSDCardFiles(file.name(), levels - 1);
    } else {
      Serial.printf("[SD] FILE: %s\tSIZE: %d\n", file.name(), file.size());
    }
    file = root.openNextFile();
  }
}

bool sendUARTCommand(const char* command) {
  if (!command || strlen(command) == 0) {
    Serial.println("[ERROR] Cannot send empty command via UART");
    return false;
  }
  size_t length = strlen(command);
  if (length >= UART_BUFFER_SIZE) {
    Serial.println("[ERROR] UART command too long");
    return false;
  }
  int written = uart_write_bytes(UART_NUM, command, length);
  if (written < 0) {
    Serial.println("[ERROR] Failed to write UART command");
    return false;
  }
  if (command[length - 1] != '\n') {
    uart_write_bytes(UART_NUM, "\n", 1);
  }
  Serial.printf("[UART SENT] %s\n", command);
  return true;
}

void sendUartDataToClients(const char* data) {
  if (!data || strlen(data) == 0) {
    Serial.println("[WARNING] Empty UART data received");
    return;
  }
  if (ws.count() > 0) {
    const size_t required_capacity = JSON_OBJECT_SIZE(2) + strlen(data) + 50;
    if (required_capacity > JSON_BUFFER_SIZE) {
      Serial.println("[ERROR] UART data too large for buffer");
      return;
    }
    StaticJsonDocument<JSON_BUFFER_SIZE> doc;
    doc["type"] = "serial_data";
    doc["message"] = data;
    String jsonString;
    serializeJson(doc, jsonString);
    if (jsonString.length() > 0) {
      ws.textAll(jsonString);
      Serial.printf("[UART TO WS] %s\n", data);
    }
  }
}

void onWsEvent(AsyncWebSocket* server,
               AsyncWebSocketClient* client,
               AwsEventType type,
               void* arg,
               unsigned char* data,
               unsigned int len) {
  if (type == WS_EVT_CONNECT) {
    Serial.println("[WS] Client connected");
  } else if (type == WS_EVT_DISCONNECT) {
    Serial.println("[WS] Client disconnected");
  } else if (type == WS_EVT_DATA) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (info->opcode == WS_TEXT) {
      char* msg = (char*)malloc(len + 1);
      if (!msg) {
        Serial.println("[ERROR] Failed to allocate message buffer");
        return;
      }
      memcpy(msg, data, len);
      msg[len] = '\0';
      Serial.printf("[WS] Received from client: %s\n", msg);
      StaticJsonDocument<JSON_BUFFER_SIZE> doc;
      DeserializationError error = deserializeJson(doc, msg);
      free(msg);
      if (error) {
        Serial.println("[ERROR] Failed to parse JSON");
        client->text("{\"type\":\"command_status\",\"success\":false,\"message\":\"Invalid JSON\"}");
        return;
      }
      if (doc.containsKey("action")) {
        const char* action = doc["action"];
        if (strcmp(action, "connect") == 0) {
          client->text("{\"type\":\"command_status\",\"success\":true,\"message\":\"Already connected.\"}");
        } else if (strcmp(action, "send_command") == 0) {
          const char* command = doc["command"];
          if (sendUARTCommand(command)) {
            client->text("{\"type\":\"command_status\",\"success\":true,\"message\":\"Command sent.\"}");
          } else {
            client->text("{\"type\":\"command_status\",\"success\":false,\"message\":\"Failed to send command.\"}");
          }
        } else if (strcmp(action, "set") == 0) {
          const char* key = doc["key"];
          if (strcmp(key, "all") == 0) {
            JsonObject params = doc["value"].as<JsonObject>();
            bool success = true;
            for (JsonPair param : params) {
              String cmd = "set " + String(param.key().c_str()) + " " + param.value().as<String>();
              if (!sendUARTCommand(cmd.c_str())) {
                success = false;
                break;
              }
            }
            client->text(success ?
              "{\"type\":\"command_status\",\"success\":true,\"message\":\"Parameters applied successfully.\"}" :
              "{\"type\":\"command_status\",\"success\":false,\"message\":\"Failed to apply parameters.\"}");
          } else {
            client->text("{\"type\":\"command_status\",\"success\":false,\"message\":\"Unknown set key.\"}");
          }
        } else {
          client->text("{\"type\":\"command_status\",\"success\":false,\"message\":\"Unknown action.\"}");
        }
      }
    }
  }
}

// *****************************************************************************
//                           UART Setup and Tasks
// *****************************************************************************
bool setupUART() {
  Serial.printf("[DEBUG] Free heap before UART1 init: %d\n", esp_get_free_heap_size());
  Serial.println("[DEBUG] Entering setupUART()");
  uart_config_t uart_config = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_APB,
  };
  esp_err_t err = uart_param_config(UART_NUM, &uart_config);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] UART1 parameter configuration failed: %d\n", err);
    return false;
  }
  Serial.println("[DEBUG] UART1 parameter configuration successful");
  err = uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] UART1 pin configuration failed: %d\n", err);
    return false;
  }
  Serial.println("[DEBUG] UART1 pin configuration successful");
  Serial.printf("[DEBUG] UART1 buffer size: %d bytes\n", UART_BUFFER_SIZE * 2);
  Serial.printf("[DEBUG] Free heap before UART1 driver install: %d bytes\n", esp_get_free_heap_size());
  err = uart_driver_install(UART_NUM, 2048, 2048, 20, &uart_queue, 0);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] UART1 driver installation failed: %d\n", err);
    return false;
  }
  Serial.println("[DEBUG] UART1 driver installation successful");
  xTaskCreate(uart_event_task, "uart_event_task", 4096, NULL, 12, NULL);
  Serial.printf("[DEBUG] Free heap after UART1 init: %d\n", esp_get_free_heap_size());
  Serial.println("[DEBUG] Exiting setupUART()");
  return true;
}

bool setupUART2() {
  Serial.println("[DEBUG] Entering setupUART2");
  Serial.println("\n[UART2 DIAG] Starting hardware validation...");
  Serial.flush();
  
  if (strcmp(ESP.getChipModel(), "ESP32-S3") != 0) {
    Serial.print("[UART2 CRITICAL] Invalid chip - requires ESP32-S3. Found: ");
    Serial.println(ESP.getChipModel());
    return false;
  }
  Serial.println("[DEBUG] Chip model validation successful.");
  
  Serial.println("[DEBUG] Configuring UART2 pins...");
  uart_set_pin((uart_port_t)UART_NUM_2, UART_TX_PIN_2, UART_RX_PIN_2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  Serial.println("[DEBUG] UART2 pins configured.");
  
  if (UART_NUM_2 >= UART_NUM_MAX) {
    Serial.printf("[UART2 CRITICAL] Invalid controller number: %d\n", UART_NUM_2);
    return false;
  }
  
  Serial.println("[DEBUG] Checking GPIO functionality...");
  Serial.printf("[UART2 DIAG] TX Pin %d capability: %s\n",
                UART_TX_PIN_2,
                GPIO_IS_VALID_OUTPUT_GPIO(UART_TX_PIN_2) ? "Valid" : "Invalid");
  Serial.printf("[UART2 DIAG] RX Pin %d capability: %s\n",
                UART_RX_PIN_2,
                GPIO_IS_VALID_GPIO(UART_RX_PIN_2) ? "Valid" : "Invalid");
  
  Serial.println("[DEBUG] Performing critical memory check...");
  if (esp_get_free_heap_size() < 1024 * 10) {
    Serial.println("[UART2 CRITICAL] Insufficient heap for UART initialization");
    return false;
  }
  
  Serial.println("[DEBUG] Resetting and enabling UART2 module...");
  periph_module_reset(PERIPH_UART2_MODULE);
  periph_module_enable(PERIPH_UART2_MODULE);
  Serial.println("[DEBUG] UART2 module reset and enabled.");
  delay(50);
  
  Serial.println("[DEBUG] Configuring UART2 parameters...");
  uart_config_t uart_config_2 = {
    .baud_rate = 115200,
    .data_bits = UART_DATA_8_BITS,
    .parity    = UART_PARITY_DISABLE,
    .stop_bits = UART_STOP_BITS_1,
    .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
    .source_clk = UART_SCLK_APB,
  };
  esp_err_t err = uart_param_config((uart_port_t)UART_NUM_2, &uart_config_2);
  if (err != ESP_OK) {
    Serial.printf("[UART2 CRITICAL] Config failed: 0x%x\n", err);
    return false;
  }
  delay(100);
  Serial.println("[DEBUG] UART2 parameter configuration successful");
  
  Serial.println("[DEBUG] Setting UART2 pins again...");
  err = uart_set_pin((uart_port_t)UART_NUM_2, UART_TX_PIN_2, UART_RX_PIN_2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] UART2 pin configuration failed: %d\n", err);
    return false;
  }
  Serial.println("[DEBUG] UART2 pin configuration successful");
  
  Serial.println("[DEBUG] Installing UART2 driver...");
  err = uart_driver_install((uart_port_t)UART_NUM_2, 2048, 2048, 20, &uart_queue_2, 0);
  if (err != ESP_OK) {
    Serial.printf("[ERROR] UART2 driver installation failed: %d\n", err);
    return false;
  }
  
  Serial.println("[DEBUG] Creating UART2 event task...");
  xTaskCreate(uart_event_task_2, "uart_event_task_2", 4096, NULL, 12, NULL);
  return true;
}

// *****************************************************************************
//                   UART Bridge Task (Buffer/Message Splitting)
// *****************************************************************************
void uart_bridge_task(void* pvParameters) {
  static char buffer1[LINE_BUFFER_SIZE] = {0};  // Data from UART1 -> UART2
  static size_t buffer1_len = 0;
  static char buffer2[LINE_BUFFER_SIZE] = {0};  // Data from UART2 -> UART1
  static size_t buffer2_len = 0;
  char temp[TEMP_BUFFER_SIZE];

  for (;;) {
    // From UART1 to UART2
    size_t available1 = 0;
    if (uart_get_buffered_data_len(UART_NUM, &available1) == ESP_OK && available1 > 0) {
      int read_len = uart_read_bytes(UART_NUM, (uint8_t*)temp, min(available1, (size_t)(TEMP_BUFFER_SIZE - 1)), UART_TIMEOUT);
      if (read_len > 0) {
        temp[read_len] = '\0';
        if (buffer1_len + read_len < LINE_BUFFER_SIZE) {
          memcpy(buffer1 + buffer1_len, temp, read_len);
          buffer1_len += read_len;
          buffer1[buffer1_len] = '\0';
        } else {
          Serial.println("[ERROR] Buffer1 overflow. Clearing buffer.");
          buffer1_len = 0;
          buffer1[0] = '\0';
        }
      }
    }
    if (buffer1_len > 0) {
      int lines = process_bridge_direction(UART_NUM_2, buffer1, &buffer1_len);
      if (lines > 0) {
        vTaskDelay(BATCH_DELAY_MS / portTICK_PERIOD_MS);
      }
    }
    
    // From UART2 to UART1
    size_t available2 = 0;
    if (uart_get_buffered_data_len((uart_port_t)UART_NUM_2, &available2) == ESP_OK && available2 > 0) {
      int read_len = uart_read_bytes((uart_port_t)UART_NUM_2, (uint8_t*)temp, min(available2, (size_t)(TEMP_BUFFER_SIZE - 1)), UART_TIMEOUT);
      if (read_len > 0) {
        temp[read_len] = '\0';
        if (buffer2_len + read_len < LINE_BUFFER_SIZE) {
          memcpy(buffer2 + buffer2_len, temp, read_len);
          buffer2_len += read_len;
          buffer2[buffer2_len] = '\0';
        } else {
          Serial.println("[ERROR] Buffer2 overflow. Clearing buffer.");
          buffer2_len = 0;
          buffer2[0] = '\0';
        }
      }
    }
    if (buffer2_len > 0) {
      int lines = process_bridge_direction(UART_NUM, buffer2, &buffer2_len);
      if (lines > 0) {
        vTaskDelay(BATCH_DELAY_MS / portTICK_PERIOD_MS);
      }
    }
    
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// *****************************************************************************
//                     UART Event Task for UART1
// *****************************************************************************
void uart_event_task(void* pvParameters) {
  uart_event_t event;
  uint8_t* dtmp = (uint8_t*) heap_caps_malloc(UART_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!dtmp) {
    Serial.println("[ERROR] Failed to allocate UART buffer");
    vTaskDelete(NULL);
    return;
  }
  for (;;) {
    if (xQueueReceive(uart_queue, (void*)&event, portMAX_DELAY)) {
      memset(dtmp, 0, UART_BUFFER_SIZE);
      switch (event.type) {
        case UART_DATA: {
          size_t length = 0;
          esp_err_t err = uart_get_buffered_data_len(UART_NUM, &length);
          if (err != ESP_OK) {
            Serial.printf("[ERROR] Failed to get UART buffer length: %d\n", err);
            continue;
          }
          while (length > 0) {
            int chunk_size = (length > UART_BUFFER_SIZE - 1) ? UART_BUFFER_SIZE - 1 : length;
            int read_length = uart_read_bytes(UART_NUM, dtmp, chunk_size, UART_TIMEOUT);
            if (read_length > 0) {
              dtmp[read_length] = '\0';
              sendUartDataToClients((char*)dtmp);
              length -= read_length;
            } else {
              Serial.println("[ERROR] Failed to read UART data");
              break;
            }
          }
          break;
        }
        case UART_FIFO_OVF:
          Serial.println("[WARNING] UART FIFO Overflow - clearing FIFO");
          uart_flush(UART_NUM);
          break;
        case UART_BUFFER_FULL:
          Serial.println("[WARNING] UART Buffer Full - clearing buffer");
          uart_flush(UART_NUM);
          break;
        default:
          Serial.printf("[UART EVENT] Unhandled event type: %d\n", event.type);
          break;
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  heap_caps_free(dtmp);
  vTaskDelete(NULL);
}

// *****************************************************************************
//                   UART Event Task for UART2
// *****************************************************************************
void uart_event_task_2(void* pvParameters) {
  uart_event_t event;
  uint8_t* dtmp = (uint8_t*) heap_caps_malloc(UART_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!dtmp) {
    Serial.println("[ERROR] Failed to allocate UART2 buffer");
    vTaskDelete(NULL);
    return;
  }
  for (;;) {
    if (xQueueReceive(uart_queue_2, (void*)&event, portMAX_DELAY)) {
      memset(dtmp, 0, UART_BUFFER_SIZE);
      switch (event.type) {
        case UART_DATA: {
          size_t length = 0;
          esp_err_t err = uart_get_buffered_data_len((uart_port_t)UART_NUM_2, &length);
          if (err != ESP_OK) {
            Serial.printf("[ERROR] Failed to get UART2 buffer length: %d\n", err);
            continue;
          }
          if (length > 0 && length < UART_BUFFER_SIZE) {
            int read_length = uart_read_bytes((uart_port_t)UART_NUM_2, dtmp, length, UART_TIMEOUT);
            if (read_length > 0) {
              dtmp[read_length] = '\0';
              if (read_length < UART_BUFFER_SIZE) {
                Serial.printf("[UART2 RECEIVED] %s\n", dtmp);
              }
            }
          }
          break;
        }
        case UART_FIFO_OVF:
          Serial.println("[WARNING] UART2 FIFO Overflow - clearing FIFO");
          uart_flush((uart_port_t)UART_NUM_2);
          break;
        case UART_BUFFER_FULL:
          Serial.println("[WARNING] UART2 Buffer Full - clearing buffer");
          uart_flush((uart_port_t)UART_NUM_2);
          break;
        default:
          Serial.printf("[UART2 EVENT] Unhandled event type: %d\n", event.type);
          break;
      }
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  heap_caps_free(dtmp);
  vTaskDelete(NULL);
}

// *****************************************************************************
//                           Web Server Setup
// *****************************************************************************
void setupWebServer() {
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
    request->send(SD, "/index.html", "text/html");
  });
  AsyncStaticWebHandler* staticHandler = new AsyncStaticWebHandler("/static/", SD, "/static/", "max-age=600");
  staticHandler->setDefaultFile("index.html");
  server.addHandler(staticHandler);
  server.onNotFound([](AsyncWebServerRequest* request) {
    if (request->method() == HTTP_GET) {
      String path = request->url();
      Serial.printf("[WEB] Handling request for: %s\n", path.c_str());
      if (path.endsWith("/")) path += "index.html";
      if (!SD.exists(path)) {
        Serial.printf("[WEB] File not found: %s\n", path.c_str());
        request->send(404, "text/plain", "File not found");
        return;
      }
      File file = SD.open(path, FILE_READ);
      if (!file) {
        Serial.printf("[WEB] Failed to open file: %s\n", path.c_str());
        request->send(500, "text/plain", "Failed to open file");
        return;
      }
      String contentType = getContentType(path);
      Serial.printf("[WEB] Serving file: %s (%s)\n", path.c_str(), contentType.c_str());
      request->send(SD, path, contentType);
      file.close();
    }
  });
  ws.onEvent([](AsyncWebSocket* server, AsyncWebSocketClient* client, AwsEventType type, void* arg, unsigned char* data, unsigned int len) {
    onWsEvent(server, client, type, arg, data, len);
  });
  server.addHandler(&ws);
  server.begin();
  Serial.println("[INFO] Web server started");
}

// *****************************************************************************
//                           Arduino Setup and Loop
// *****************************************************************************
void setup() {
  Serial.begin(115200);
  delay(100);  // Brief delay for Serial initialization

  if (!SD.begin()) {
    Serial.println("[ERROR] SD Card Mount Failed");
    return;
  }
  uint8_t cardType = SD.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("[ERROR] No SD card attached");
    return;
  }
  Serial.printf("[INFO] SD Card initialized. Type: %d, Size: %.2f MB\n", cardType, SD.cardSize() / (1024.0 * 1024.0));
  Serial.println("Listing all files on SD card:");
  String fileList = getSDFileList("/");
  Serial.println(fileList);
  
  if (!setupUART()) {
    Serial.println("[ERROR] Failed to setup UART1");
  } else {
    Serial.println("[INFO] UART1 initialized successfully");
    Serial.printf("[INFO] UART1 Config: RX Pin: %d, TX Pin: %d, Baud: 115200\n", UART_RX_PIN, UART_TX_PIN);
  }
  
  // Force USB mode (whether powered by USB or by 5V, we operate in USB mode)
  g_usbMode = true;
  switchUart2Mode();
  
  // Create the UART bridge task.
  if (xTaskCreate(uart_bridge_task, "uart_bridge_task", 4096, NULL, 12, NULL) == pdPASS) {
    Serial.println("[INFO] UART bridge task created successfully");
  } else {
    Serial.println("[ERROR] Failed to create UART bridge task");
  }
  
  WiFi.softAP(ap_ssid, ap_password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("[INFO] AP IP address: ");
  Serial.println(IP);
  
  setupWebServer();
}

void loop() {
  ws.cleanupClients();
  delay(1000);
}
