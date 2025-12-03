#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

#define DHT_PIN 15
#define DHT_TYPE DHT22
#define LED_PIN 26 
#define PIR_PIN 27

// Cấu hình WiFi và Server
const char* ssid = "Wokwi-GUEST"; // Mạng ảo của Wokwi
const char* password = "";
const char* serverName = "http://postman-echo.com/post";

// Biến toàn cục để lưu dữ liệu cảm biến
float t = 0.0;
float h = 0.0;
int motion = 0;

DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Khai báo các Task
void TaskBlink(void *pvParameters);
void TaskSensorRead(void *pvParameters);
void TaskSendingData(void *pvParameters);

void setup() {
  Serial.begin(9600);
  
  // Khởi tạo phần cứng
  pinMode(LED_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
  dht.begin();
  lcd.init();
  lcd.backlight();

  // Kết nối WiFi
  Serial.print("Connecting to WiFi");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  Serial.println("Starting FreeRTOS Tasks...");
  
  // Task 1: Blink LED mỗi 1 giây
  xTaskCreate(
    TaskBlink,    
    "Task Blink", 
    1024,         
    NULL,         
    1,            
    NULL        
  );

  // Task 2: Đọc cảm biến mỗi 2 giây
  xTaskCreate(
    TaskSensorRead,
    "Task Sensor",
    4096,         
    NULL,
    2,            
    NULL
  );

  // Task 3: Gửi dữ liệu lên server mỗi 5 giây
  xTaskCreate(
    TaskSendingData,
    "Task HTTP",
    8192,         
    NULL,
    2,
    NULL
  );
}

void loop() {
  vTaskDelete(NULL);
}

// 1. Task nháy đèn LED mỗi 1 giây
void TaskBlink(void *pvParameters) {
  for (;;) {
    digitalWrite(LED_PIN, HIGH);
    vTaskDelay(500 / portTICK_PERIOD_MS); 
    digitalWrite(LED_PIN, LOW);
    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// 2. Task đọc DHT22 và PIR mỗi 2 giây
void TaskSensorRead(void *pvParameters) {
  for (;;) {
    // Đọc dữ liệu
    float newT = dht.readTemperature();
    float newH = dht.readHumidity();
    int newMotion = digitalRead(PIR_PIN);

    // Kiểm tra lỗi đọc DHT
    if (!isnan(newT) && !isnan(newH)) {
      t = newT; // Cập nhật biến toàn cục
      h = newH;
    }
    motion = newMotion;

    // Hiển thị LCD
    lcd.setCursor(0, 0);
    lcd.printf("T:%.1f H:%.1f", t, h);
    lcd.setCursor(0, 1);
    lcd.printf("Motion: %s", motion ? "YES" : "NO ");

    // Log ra Serial Monitor
    Serial.printf("[Sensor] Temp: %.2f, Hum: %.2f, Motion: %d\n", t, h, motion);

    vTaskDelay(2000 / portTICK_PERIOD_MS); // Chờ 2s
  }
}

// 3. Task gửi dữ liệu lên Server Postman mỗi 5 giây
void TaskSendingData(void *pvParameters) {
  for (;;) {
    // Chỉ gửi khi có WiFi
    if (WiFi.status() == WL_CONNECTED) {
      HTTPClient http;

      // Chuẩn bị URL và Header
      http.begin(serverName);
      http.addHeader("Content-Type", "application/json");

      // Đóng gói JSON
      String httpRequestData = "{\"temperature\":\"" + String(t) + "\",\"humidity\":\"" + String(h) + "\",\"motion\":\"" + String(motion) + "\"}";

      // Gửi POST Request và nhận phản hồi
      int httpResponseCode = http.POST(httpRequestData);

      Serial.print("[HTTP] Sending data: ");
      Serial.println(httpRequestData);

      if (httpResponseCode > 0) {
        Serial.printf("[HTTP] Response code: %d\n", httpResponseCode);
        String payload = http.getString();
        // Serial.println(payload); 
      } else {
        Serial.printf("[HTTP] Error code: %d\n", httpResponseCode);
      }
      
      http.end(); // Giải phóng tài nguyên
    } else {
      Serial.println("[HTTP] WiFi Disconnected");
    }

    vTaskDelay(5000 / portTICK_PERIOD_MS); // Gửi mỗi 5s
  }
}