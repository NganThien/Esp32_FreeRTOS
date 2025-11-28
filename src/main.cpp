#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// --- CẤU HÌNH PHẦN CỨNG ---
#define DHT_PIN 15
#define DHT_TYPE DHT22
#define LED_PIN 2
#define PIR_PIN 13

// --- CẤU HÌNH WIFI & SERVER ---
const char* ssid = "Wokwi-GUEST"; // Mạng ảo của Wokwi
const char* password = "";
const char* serverName = "http://postman-echo.com/post"; // [cite: 8]

// --- BIẾN TOÀN CỤC (Shared Resources) ---
// Trong FreeRTOS thực tế nên dùng Queue hoặc Mutex, nhưng với bài lab này dùng biến toàn cục cho đơn giản.
float t = 0.0;
float h = 0.0;
int motion = 0;

DHT dht(DHT_PIN, DHT_TYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

// --- KHAI BÁO PROTOTYPE ---
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

  // --- TẠO CÁC TASK ---
  
  // Task 1: Blink LED (Mức ưu tiên thấp nhất) [cite: 3]
  xTaskCreate(
    TaskBlink,    // Hàm thực thi
    "Task Blink", // Tên Task
    1024,         // Stack size (Bytes)
    NULL,         // Tham số truyền vào
    1,            // Priority (1 là thấp)
    NULL          // Task handle
  );

  // Task 2: Đọc cảm biến (Mức ưu tiên trung bình) [cite: 4]
  xTaskCreate(
    TaskSensorRead,
    "Task Sensor",
    4096,         // Cần stack lớn hơn chút cho DHT
    NULL,
    2,            // Priority cao hơn LED
    NULL
  );

  // Task 3: Gửi dữ liệu (Mức ưu tiên cao nhất hoặc ngang bằng) [cite: 5]
  xTaskCreate(
    TaskSendingData,
    "Task HTTP",
    8192,         // Stack RẤT QUAN TRỌNG: HTTPClient cần nhiều RAM, nếu để thấp sẽ bị crash (Stack Overflow)
    NULL,
    2,
    NULL
  );
}

void loop() {
  // Trong FreeRTOS, loop() được coi là Idle Task, chúng ta để trống.
  vTaskDelete(NULL);
}

// --- ĐỊNH NGHĨA CÁC TASK ---

// 1. Task nháy đèn LED mỗi 1 giây
void TaskBlink(void *pvParameters) {
  for (;;) { // Vòng lặp vô tận thay cho loop()
    digitalWrite(LED_PIN, HIGH);
    vTaskDelay(500 / portTICK_PERIOD_MS); // Dùng vTaskDelay thay cho delay()
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

    // Log ra Serial (Lưu ý: Serial không an toàn luồng, nhưng tạm chấp nhận ở bài lab)
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

      // Gửi POST Request [cite: 6, 8]
      int httpResponseCode = http.POST(httpRequestData);

      Serial.print("[HTTP] Sending data: ");
      Serial.println(httpRequestData);

      if (httpResponseCode > 0) {
        Serial.printf("[HTTP] Response code: %d\n", httpResponseCode);
        String payload = http.getString();
        // Serial.println(payload); // In phản hồi từ server nếu cần
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