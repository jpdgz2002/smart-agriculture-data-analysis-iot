/*
Generador seno 1kHz (DAC con DacESP32) + ADS1115 (DFR0553) + OLED SSD1306 + LED RGB + SD + ThingSpeak + WDT
- DAC con DacESP32 en D26 (GPIO26, DAC2) -> ADS1115 A0
- LEDs: PIN_R=D32, PIN_G=D33, PIN_B=D14 (color según voltaje: rojo 0-1V, amarillo 1-2V, verde 2-3V, azul >3V)
- I2C: SDA=D21, SCL=D22 (OLED y ADS1115)
- SPI SD: CS=D5, MOSI=D23, MISO=D19, SCK=D18
- Muestreo cada 15 s, promedio de 4 lecturas cada minuto enviado a ThingSpeak y guardado en SD
- OLED: Muestra Samp, Raw, V, C_x, Avg V, Avg C_x, X h Y min Z seg (actualizado cada 15 s)
- WDT: Reinicia si no hay medidas en 5 min
- Duración: 30 min (OPERATION_TIME_MS = 1800000UL)
- Channel ID y WriteAPIKey modificados por el usuario
- Si la SD no inicializa, continua con DAC, ADC, OLED, ThingSpeak, LED RGB
*/

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>  // ADS1115 (DFR0553)
#include <DacESP32.h>          
#include <Adafruit_GFX.h>      
#include <Adafruit_SSD1306.h>  
#include <SD.h>                
#include <WiFi.h>              
#include <ThingSpeak.h>        
#include <esp_task_wdt.h>      

// ----- DAC (pin 26) -----
#define DAC_PIN 26
DacESP32 dac2(GPIO_NUM_26);

// ----- I2C Pins -----
#define SDA_PIN 21
#define SCL_PIN 22

// ----- ADC -----
Adafruit_ADS1115 ads;
int16_t adc0;
float volts0;

// ----- Configuración OLED -----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ----- Configuración LED RGB -----
#define LED_COMMON_ANODE false
#define PIN_R 32
#define PIN_G 33
#define PIN_B 14

#if LED_COMMON_ANODE
  #define LED_ON(pin) digitalWrite(pin, LOW)
  #define LED_OFF(pin) digitalWrite(pin, HIGH)
#else
  #define LED_ON(pin) digitalWrite(pin, HIGH)
  #define LED_OFF(pin) digitalWrite(pin, LOW)
#endif

// ----- Configuración SD (SPI) -----
#define SD_CS 5
#define SD_MOSI 23
#define SD_MISO 19
#define SD_SCK 18
File dataFile;
bool sdInitialized = false;

// ----- Configuración ThingSpeak -----
const char* ssid = "IoT-B19";
const char* password = "lcontrol2020*";
unsigned long channelID = 3040125;
const char* WriteAPIKey = "QE2JRZZSF91DRG84";
WiFiClient cliente;

// ----- Configuración WDT -----
const uint32_t WDT_TIMEOUT = 300;
esp_task_wdt_config_t wdt_config = {
    .timeout_ms = WDT_TIMEOUT * 1000,
    .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
    .trigger_panic = true
};

// ----- Variables Globales -----
unsigned long startTime = 0;
const unsigned long SAMPLE_INTERVAL_MS = 15000;
const unsigned long OPERATION_TIME_MS = 1800000UL;
const int READINGS_PER_MINUTE = 4;
int16_t rawReadings[READINGS_PER_MINUTE];
float voltReadings[READINGS_PER_MINUTE];
float capReadings[READINGS_PER_MINUTE];
int readingCount = 0;

// ----- Task Handles -----
TaskHandle_t dacTaskHandle;
TaskHandle_t adcTaskHandle;

// ===================================================================
//  FUNCIÓN DE CAPACITANCIA (MODELO EMPÍRICO DE TU TESIS)
//  Cx = exp(0.120 * V + 3.914) - 55.859   → en µF
// ===================================================================
float calculateCapacitance(float measuredVoltage) {
  if (measuredVoltage < 0.05f) {
    return 0.0f;
  }
  float Cx = exp(0.120f * measuredVoltage + 3.914f) - 55.859f;
  if (Cx < 0.0f) Cx = 0.0f;
  return Cx;   // µF
}

// ----- Reconectar WiFi si se pierde -----
void reconnectWiFi() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado, intentando reconectar...");
    WiFi.begin(ssid, password);
    unsigned long wifiTimeout = millis();
    const unsigned long WIFI_TIMEOUT_MS = 10000;
    while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < WIFI_TIMEOUT_MS) {
      delay(500);
      Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nReconectado al WiFi");
      display.clearDisplay();
      display.setCursor(0, 0);
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.println("WiFi Reconectado");
      display.display();
      delay(1000);
    } else {
      Serial.println("\nNo se pudo reconectar al WiFi");
      display.clearDisplay();
      display.setCursor(0, 0);
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.println("WiFi Error");
      display.display();
      delay(1000);
    }
  }
}

// ----- Función para controlar LED RGB según voltaje -----
void setLEDColor(float voltage, bool forceOff = false) {
  LED_OFF(PIN_R); 
  LED_OFF(PIN_G); 
  LED_OFF(PIN_B);
  if (forceOff) return;
  
  if (voltage < 1.0f) {
    static unsigned long lastBlink = millis();
    static bool ledState = false;
    if (millis() - lastBlink >= 500) {
      ledState = !ledState;
      lastBlink = millis();
      if (ledState) LED_ON(PIN_R);
    }
  } else if (voltage < 2.0f) {
    LED_ON(PIN_R); LED_ON(PIN_G);
  } else if (voltage < 3.0f) {
    LED_ON(PIN_G);
  } else {
    LED_ON(PIN_B);
  }
}

// ----- DAC Task -----
void taskGenerateDAC(void *pvParameters) {
  esp_task_wdt_add(NULL);
  Serial.println("Iniciando generación DAC en GPIO26...");
  dac2.outputCW(1000);                    // ← 1000 Hz (calibración de tu tesis)
  Serial.println("DAC iniciado - Generando onda senoidal de 1000 Hz");
  while (true) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ----- ADC Task (completo, tal como lo tenías) -----
void taskReadADC(void *pvParameters) {
  esp_task_wdt_add(NULL);
  Serial.println("Iniciando DFR0553 (ADS1115)...");
  
  if (!ads.begin()) {
    Serial.println("Error: No se pudo iniciar DFR0553.");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.println("DFR0553 Error");
    display.display();
    vTaskDelete(NULL);
    return;
  }
  
  ads.setGain(GAIN_TWOTHIRDS);
  Serial.println("DFR0553 iniciado");

  startTime = millis();
  unsigned long lastSample = millis();

  for (int j = 0; j < READINGS_PER_MINUTE; ++j) {
    rawReadings[j] = 0;
    voltReadings[j] = 0.0f;
    capReadings[j] = 0.0f;
  }

  while (millis() - startTime < OPERATION_TIME_MS) {
    unsigned long now = millis();

    if (now - lastSample >= SAMPLE_INTERVAL_MS) {
      esp_task_wdt_reset();
      lastSample = now;

      float sumSquared = 0.0f;
      const int NUM_SAMPLES = 100;
      
      for (int i = 0; i < NUM_SAMPLES; i++) {
        adc0 = ads.readADC_SingleEnded(0);
        float sampleVolt = ads.computeVolts(adc0);
        sumSquared += sampleVolt * sampleVolt;
        delayMicroseconds(100);
      }
      
      volts0 = sqrt(sumSquared / NUM_SAMPLES);

      if (adc0 < 0 || adc0 > 32767) {
        Serial.println("Advertencia: Lectura ADC fuera de rango");
        continue;
      }

      if (volts0 < 0.256 && ads.getGain() != GAIN_SIXTEEN) {
        ads.setGain(GAIN_SIXTEEN);
        delay(10);
        continue;
      } else if (volts0 >= 0.256 && ads.getGain() != GAIN_TWOTHIRDS) {
        ads.setGain(GAIN_TWOTHIRDS);
        delay(10);
        continue;
      }

      if (readingCount < READINGS_PER_MINUTE) {
        rawReadings[readingCount] = adc0;
        voltReadings[readingCount] = volts0;
        capReadings[readingCount] = calculateCapacitance(volts0);   // ← Aquí usa la fórmula correcta
        readingCount++;
      }

      unsigned long elapsed = now - startTime;
      unsigned long hours = elapsed / (1000UL * 60 * 60);
      unsigned long minutes = (elapsed / (1000UL * 60)) % 60;
      unsigned long seconds = (elapsed / 1000UL) % 60;

      float avgRaw = 0.0f, avgV = 0.0f, avgCap = 0.0f;
      if (readingCount > 0) {
        for (int k = 0; k < readingCount; ++k) {
          avgRaw += (float)rawReadings[k];
          avgV += voltReadings[k];
          avgCap += capReadings[k];
        }
        avgRaw /= readingCount;
        avgV /= readingCount;
        avgCap /= readingCount;
      }

      // OLED
      display.clearDisplay();
      display.setCursor(0, 0); display.setTextSize(1); display.setTextColor(SSD1306_WHITE);
      display.printf("Samp: %lu", elapsed / 1000);
      display.setCursor(0, 10); display.printf("Raw: %d", adc0);
      display.setCursor(0, 20); display.printf("V: %.4f", volts0);
      display.setCursor(0, 30); display.printf("C_x: %.2f", (readingCount > 0) ? capReadings[readingCount-1] : 0.0f);
      display.setCursor(0, 40); display.printf("Avg V: %.4f", avgV);
      display.setCursor(0, 50); display.printf("Avg C_x: %.2f", avgCap);
      display.setCursor(0, 56); display.printf("%luh %lum %lus", hours, minutes, seconds);
      display.display();

      setLEDColor(volts0);

      Serial.printf("Lectura: Raw=%d | V=%.4f | C_x=%.2f | AvgV=%.4f | AvgC_x=%.2f | Tiempo=%luh %lum %lus\n",
                    adc0, volts0, (readingCount > 0) ? capReadings[readingCount-1] : 0.0f, avgV, avgCap, hours, minutes, seconds);

      if (readingCount >= READINGS_PER_MINUTE) {
        reconnectWiFi();
        if (WiFi.status() == WL_CONNECTED) {
          ThingSpeak.setField(1, avgRaw);
          ThingSpeak.setField(2, avgV);
          ThingSpeak.setField(3, avgCap);
          ThingSpeak.writeFields(channelID, WriteAPIKey);
        }

        if (sdInitialized && dataFile) {
          char timestamp[20];
          snprintf(timestamp, sizeof(timestamp), "%luh%lum%lus", hours, minutes, seconds);
          dataFile.printf("%s,%.1f,%.4f,%.2f\n", timestamp, avgRaw, avgV, avgCap);
          dataFile.flush();
        }

        readingCount = 0;
        for (int k = 0; k < READINGS_PER_MINUTE; ++k) {
          rawReadings[k] = 0;
          voltReadings[k] = 0.0f;
          capReadings[k] = 0.0f;
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(10));
  }

  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(40, 20);
  display.println("FIN");
  display.display();

  setLEDColor(0, true);

  if (sdInitialized && dataFile) dataFile.close();
  Serial.println("=== Fin del periodo de medicion ===");
  vTaskDelete(NULL);
}

// ----- SETUP -----
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("=== Inicio: Capacímetro ESP32 con DacESP32 ===");

  esp_task_wdt_deinit();
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setClock(100000);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3D)) {
      Serial.println("OLED no encontrado");
      while(true) delay(1000);
    }
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println("Iniciando...");
  display.display();

  pinMode(PIN_R, OUTPUT); pinMode(PIN_G, OUTPUT); pinMode(PIN_B, OUTPUT);
  setLEDColor(0, true);
  LED_ON(PIN_R); delay(500); LED_OFF(PIN_R);
  LED_ON(PIN_G); delay(500); LED_OFF(PIN_G);
  LED_ON(PIN_B); delay(500); LED_OFF(PIN_B);

  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (SD.begin(SD_CS)) {
    dataFile = SD.open("data.csv", FILE_WRITE);
    if (dataFile) {
      dataFile.println("Timestamp,AvgRaw,AvgVolts,AvgCap");
      sdInitialized = true;
      Serial.println("SD inicializada correctamente");
    }
  } else {
    Serial.println("Error al inicializar tarjeta SD - continuando sin SD");
  }

  WiFi.begin(ssid, password);
  unsigned long wifiTimeout = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - wifiTimeout < 15000) {
    delay(500); Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) Serial.println("\nWiFi OK");

  ThingSpeak.begin(cliente);

  Serial.println("Probando DAC en GPIO26...");
  dac2.outputCW(1000);          // ← 1000 Hz
  delay(2000);
  Serial.println("Prueba DAC completada");

  esp_task_wdt_init(&wdt_config);

  xTaskCreatePinnedToCore(taskGenerateDAC, "TaskDAC", 4096, NULL, 1, &dacTaskHandle, 0);
  xTaskCreatePinnedToCore(taskReadADC,     "TaskADC", 8192, NULL, 2, &adcTaskHandle, 1);

  Serial.println("=== Sistema inicializado - Tareas ejecutándose ===");
}

void loop() {
  static unsigned long lastHeartbeat = 0;
  if (millis() - lastHeartbeat >= 30000) {
    lastHeartbeat = millis();
    Serial.printf("Sistema funcionando - Memoria libre: %d bytes\n", ESP.getFreeHeap());
  }
  vTaskDelay(pdMS_TO_TICKS(1000));
}