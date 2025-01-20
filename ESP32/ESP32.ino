#include <Arduino.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <AsyncTCP.h>
#include <ArduinoJson.h>
#include <SD.h>
#include <driver/uart.h>

// Constants
const char* ap_ssid = "Evil-BW16";
const char* ap_password = "password1234";
const size_t UART_BUFFER_SIZE = 512;  // Match BW16 buffer size
const size_t UART_CHUNK_SIZE = 256;   // Match BW16 chunk size
const size_t JSON_BUFFER_SIZE = 2048;
const TickType_t UART_TIMEOUT = pdMS_TO_TICKS(100);

// UART Configuration
#define UART_RX_PIN 18
#define UART_TX_PIN 17
#define UART_NUM UART_NUM_1

// UART2 Configuration
#define UART_RX_PIN_2 44
#define UART_TX_PIN_2 43
#define UART_NUM_2 UART_NUM_2

// Global instances
AsyncWebServer server(80);
AsyncWebSocket ws("/ws");
QueueHandle_t uart_queue;
QueueHandle_t uart_queue_2;

// Memory-safe version of sendUartDataToClients
void sendUartDataToClients(const char* data) {
    if (!data || strlen(data) == 0) {
        Serial.println("[WARNING] Empty UART data received");
        return;
    }

    // Only send to WebSocket if clients are connected
    if (ws.count() > 0) {
        // Create JSON document with pre-calculated capacity
        const size_t required_capacity = JSON_OBJECT_SIZE(2) + strlen(data) + 50;
        if (required_capacity > JSON_BUFFER_SIZE) {
            Serial.println("[ERROR] UART data too large for buffer");
            return;
        }

        StaticJsonDocument<JSON_BUFFER_SIZE> doc;
        doc["type"] = "serial_data";  // Ensure this matches what the client expects
        doc["message"] = data;

        String jsonString;
        serializeJson(doc, jsonString);

        if (jsonString.length() > 0) {
            ws.textAll(jsonString);
            Serial.printf("[UART TO WS] %s\n", data);
        }
    }
}

// Function to send UART command with error checking
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

    // Add newline if needed
    if (command[length - 1] != '\n') {
        uart_write_bytes(UART_NUM, "\n", 1);
    }

    Serial.printf("[UART SENT] %s\n", command);
    return true;
}

// Improved UART event task with larger chunk handling
void uart_event_task(void *pvParameters) {
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) heap_caps_malloc(UART_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    if (!dtmp) {
        Serial.println("[ERROR] Failed to allocate UART buffer");
        vTaskDelete(NULL);
        return;
    }

    for(;;) {
        if(xQueueReceive(uart_queue, (void*)&event, portMAX_DELAY)) {
            memset(dtmp, 0, UART_BUFFER_SIZE);
            
            switch(event.type) {
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
        vTaskDelay(10 / portTICK_PERIOD_MS); // Yield to other tasks
    }

    heap_caps_free(dtmp);
    vTaskDelete(NULL);
}

void uart_event_task_2(void *pvParameters) {
    uart_event_t event;
    uint8_t* dtmp = (uint8_t*) heap_caps_malloc(UART_BUFFER_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    
    if (!dtmp) {
        Serial.println("[ERROR] Failed to allocate UART2 buffer");
        vTaskDelete(NULL);
        return;
    }

    for(;;) {
        if(xQueueReceive(uart_queue_2, (void*)&event, portMAX_DELAY)) {
            memset(dtmp, 0, UART_BUFFER_SIZE);
            
            switch(event.type) {
                case UART_DATA: {
                    size_t length = 0;
                    esp_err_t err = uart_get_buffered_data_len(UART_NUM_2, &length);
                    if (err != ESP_OK) {
                        Serial.printf("[ERROR] Failed to get UART2 buffer length: %d\n", err);
                        continue;
                    }

                    if (length > 0 && length < UART_BUFFER_SIZE) {
                        int read_length = uart_read_bytes(UART_NUM_2, dtmp, length, UART_TIMEOUT);
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
                    uart_flush(UART_NUM_2);
                    break;
                case UART_BUFFER_FULL:
                    Serial.println("[WARNING] UART2 Buffer Full - clearing buffer");
                    uart_flush(UART_NUM_2);
                    break;
                default:
                    Serial.printf("[UART2 EVENT] Unhandled event type: %d\n", event.type);
                    break;
            }
        }
        vTaskDelay(10 / portTICK_PERIOD_MS); // Yield to other tasks
    }

    heap_caps_free(dtmp);
    vTaskDelete(NULL);
}

void uart_bridge_task(void *pvParameters) {
    uint8_t data1[UART_BUFFER_SIZE];
    uint8_t data2[UART_BUFFER_SIZE];
    size_t len1 = 0, len2 = 0;

    for (;;) {
        // Read from UART1 and send to UART2
        esp_err_t err1 = uart_get_buffered_data_len(UART_NUM, &len1);
        if (err1 == ESP_OK && len1 > 0) {
            len1 = uart_read_bytes(UART_NUM, data1, min(len1, UART_BUFFER_SIZE - 1), UART_TIMEOUT);
            if (len1 > 0) {
                data1[len1] = '\0'; // Null-terminate

                // Send in chunks to match BW16
                for (size_t i = 0; i < len1; i += UART_CHUNK_SIZE) {
                    size_t chunkSize = min(UART_CHUNK_SIZE, len1 - i);
                    esp_err_t write_err = uart_write_bytes(UART_NUM_2, data1 + i, chunkSize);
                    if (write_err != ESP_OK) {
                        Serial.printf("[ERROR] Failed to write to UART2: %d\n", write_err);
                        break;
                    }
                    vTaskDelay(50 / portTICK_PERIOD_MS); // Delay to prevent buffer overflow
                }
            }
        } else if (err1 != ESP_OK) {
            Serial.printf("[ERROR] Failed to read from UART1: %d\n", err1);
        }

        // Read from UART2 and send to UART1
        esp_err_t err2 = uart_get_buffered_data_len(UART_NUM_2, &len2);
        if (err2 == ESP_OK && len2 > 0) {
            len2 = uart_read_bytes(UART_NUM_2, data2, min(len2, UART_BUFFER_SIZE - 1), UART_TIMEOUT);
            if (len2 > 0) {
                data2[len2] = '\0'; // Null-terminate

                // Send in chunks to match BW16
                for (size_t i = 0; i < len2; i += UART_CHUNK_SIZE) {
                    size_t chunkSize = min(UART_CHUNK_SIZE, len2 - i);
                    esp_err_t write_err = uart_write_bytes(UART_NUM, data2 + i, chunkSize);
                    if (write_err != ESP_OK) {
                        Serial.printf("[ERROR] Failed to write to UART1: %d\n", write_err);
                        break;
                    }
                    vTaskDelay(50 / portTICK_PERIOD_MS); // Delay to prevent buffer overflow
                }
            }
        } else if (err2 != ESP_OK) {
            Serial.printf("[ERROR] Failed to read from UART2: %d\n", err2);
        }

        vTaskDelay(10 / portTICK_PERIOD_MS); // Small delay to yield to other tasks
    }
}

// WebSocket event handler
void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len) {
    if(type == WS_EVT_CONNECT) {
        Serial.println("[WS] Client connected");
    } 
    else if(type == WS_EVT_DISCONNECT) {
        Serial.println("[WS] Client disconnected");
    } 
    else if(type == WS_EVT_DATA) {
        AwsFrameInfo *info = (AwsFrameInfo*)arg;
        if(info->opcode == WS_TEXT) {
            // Ensure null termination
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
                }
                else if (strcmp(action, "send_command") == 0) {
                    const char* command = doc["command"];
                    if (sendUARTCommand(command)) {
                        client->text("{\"type\":\"command_status\",\"success\":true,\"message\":\"Command sent.\"}");
                    } else {
                        client->text("{\"type\":\"command_status\",\"success\":false,\"message\":\"Failed to send command.\"}");
                    }
                }
                else if (strcmp(action, "set") == 0) {
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
                }
                else {
                    client->text("{\"type\":\"command_status\",\"success\":false,\"message\":\"Unknown action.\"}");
                }
            }
        }
    }
}

// Initialize UART with error checking
bool setupUART() {
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    esp_err_t err = uart_param_config(UART_NUM, &uart_config);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] UART parameter configuration failed: %d\n", err);
        return false;
    }

    err = uart_set_pin(UART_NUM, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] UART pin configuration failed: %d\n", err);
        return false;
    }

    err = uart_driver_install(UART_NUM, UART_BUFFER_SIZE * 2, UART_BUFFER_SIZE * 2, 20, &uart_queue, 0);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] UART driver installation failed: %d\n", err);
        return false;
    }

    BaseType_t task_created = xTaskCreate(
        uart_event_task,
        "uart_event_task",
        4096,
        NULL,
        12,
        NULL
    );

    if (task_created != pdPASS) {
        Serial.println("[ERROR] Failed to create UART task");
        return false;
    }

    return true;
}

bool setupUART2() {
    uart_config_t uart_config_2 = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    esp_err_t err = uart_param_config(UART_NUM_2, &uart_config_2);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] UART2 parameter configuration failed: %d\n", err);
        return false;
    }

    err = uart_set_pin(UART_NUM_2, UART_TX_PIN_2, UART_RX_PIN_2, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] UART2 pin configuration failed: %d\n", err);
        return false;
    }

    err = uart_driver_install(UART_NUM_2, UART_BUFFER_SIZE * 2, UART_BUFFER_SIZE * 2, 20, &uart_queue_2, 0);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] UART2 driver installation failed: %d\n", err);
        return false;
    }

    xTaskCreate(uart_event_task_2, "uart_event_task_2", 4096, NULL, 12, NULL);

    return true;
}

// Initialize web server
void setupWebServer() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(SD, "/index.html", "text/html");
    });

    // Configure static file serving with cache control
    AsyncStaticWebHandler* staticHandler = new AsyncStaticWebHandler("/static/", SD, "/static/", "max-age=600");
    staticHandler->setDefaultFile("index.html");
    server.addHandler(staticHandler);

    // Configure server to handle larger files with better error handling
    server.onNotFound([](AsyncWebServerRequest *request){
        if(request->method() == HTTP_GET){
            String path = request->url();
            Serial.printf("[WEB] Handling request for: %s\n", path.c_str());
            
            if(path.endsWith("/")) {
                path += "index.html";
            }

            if(!SD.exists(path)) {
                Serial.printf("[WEB] File not found: %s\n", path.c_str());
                request->send(404, "text/plain", "File not found");
                return;
            }

            File file = SD.open(path, FILE_READ);
            if(!file) {
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

    
    ws.onEvent(onWsEvent);
    server.addHandler(&ws);
    
    server.begin();
    Serial.println("[INFO] Web server started");
}

// Helper function to get content type
String getContentType(String filename) {
    if(filename.endsWith(".html")) return "text/html";
    if(filename.endsWith(".css")) return "text/css";
    if(filename.endsWith(".js")) return "application/javascript";
    if(filename.endsWith(".png")) return "image/png";
    if(filename.endsWith(".woff2")) return "font/woff2";
    if(filename.endsWith(".woff")) return "font/woff";
    if(filename.endsWith(".txt")) return "text/plain";
    return "application/octet-stream";
}

void loop() {
    ws.cleanupClients();
    delay(1000);
}

// Function to recursively list files on SD card and return as JSON array
String getSDFileList(const char* dirname, uint8_t levels = 0) {
    String output = "[";
    File root = SD.open(dirname);
    if(!root){
        return "[]";
    }
    if(!root.isDirectory()){
        return "[]";
    }

    File file = root.openNextFile();
    bool first = true;
    while(file){
        if(!first) {
            output += ",";
        }
        first = false;
        
        if(file.isDirectory()){
            output += "{\"name\":\"" + String(file.name()) + "\",\"type\":\"dir\"";
            if(levels){
                output += ",\"children\":" + getSDFileList(file.name(), levels -1);
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

// Function to recursively list files on SD card (for serial logging)
void logSDCardFiles(const char* dirname, uint8_t levels = 0) {
    Serial.printf("[SD] Listing directory: %s\n", dirname);
    
    File root = SD.open(dirname);
    if(!root){
        Serial.println("[ERROR] Failed to open directory");
        return;
    }
    if(!root.isDirectory()){
        Serial.println("[ERROR] Not a directory");
        return;
    }

    File file = root.openNextFile();
    while(file){
        if(file.isDirectory()){
            Serial.printf("[SD] DIR : %s\n", file.name());
            if(levels){
                logSDCardFiles(file.name(), levels -1);
            }
        } else {
            Serial.printf("[SD] FILE: %s\tSIZE: %d\n", file.name(), file.size());
        }
        file = root.openNextFile();
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial) { ; }

    if (!SD.begin()) {
        Serial.println("[ERROR] SD Card Mount Failed");
        return;
    }
    uint8_t cardType = SD.cardType();
    if (cardType == CARD_NONE) {
        Serial.println("[ERROR] No SD card attached");
        return;
    }
    Serial.printf("[INFO] SD Card initialized. Type: %d, Size: %.2f MB\n", 
        cardType, 
        SD.cardSize() / (1024.0 * 1024.0));
        
    // Log all files on SD card
    Serial.println("Listing all files on SD card:");
    String fileList = getSDFileList("/");
    Serial.println(fileList);

    if (!setupUART()) {
        Serial.println("[ERROR] Failed to setup UART1");
    } else {
        Serial.println("[INFO] UART1 initialized successfully");
        Serial.printf("[INFO] UART1 Config: RX Pin: %d, TX Pin: %d, Baud: 115200\n", UART_RX_PIN, UART_TX_PIN);
    }

    if (!setupUART2()) {
        Serial.println("[ERROR] Failed to setup UART2");
    } else {
        Serial.println("[INFO] UART2 initialized successfully");
        Serial.printf("[INFO] UART2 Config: RX Pin: %d, TX Pin: %d, Baud: 115200\n", UART_RX_PIN_2, UART_TX_PIN_2);
    }

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
