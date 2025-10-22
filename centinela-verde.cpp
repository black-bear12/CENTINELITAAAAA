#include <Arduino.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LoRa.h>
#include <SD.h>
#include <SPI.h>

// --- Pines GPIO ---
#define DHT_PIN 27
#define DHT_TYPE DHT22
DHT dht(DHT_PIN, DHT_TYPE);

#define ONE_WIRE_BUS 26
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#define MQ2_PIN 34
#define MQ135_PIN 35

#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define LORA_RST 14
#define LORA_DIO0 2
#define LORA_FREQUENCY 433E6

#define LED_PIN 12
#define BUZZER_PIN 13

#define SD_CS_PIN 15

// --- Umbrales críticos ---
#define TEMP_CRITICA 40.0
#define HUM_CRITICA 20.0
#define GAS_UMBRAL_MQ2 1500
#define GAS_UMBRAL_MQ135 1200

// --- Variables ---
float currentTemperature = 0.0;
float currentHumidity = 0.0;
int mq2Value = 0;
int mq135Value = 0;
float internalTemperature = 0.0;

enum AlertLevel {
  AL_BAJA,
  AL_MEDIA,
  AL_ALTA,
  AL_CRITICA
};
AlertLevel currentAlertLevel = AL_BAJA;

// Estado SD
bool sdAvailable = false;

// --- Prototipos ---
void readAllSensors();
void evaluateAlertLevel();
void activateLocalAlerts(AlertLevel level);
void sendLoRaAlert(AlertLevel level);
void logDataToSD();
String getAlertLevelString(AlertLevel level);

// --- Setup ---
void setup() {
  Serial.begin(115200);
  while (!Serial);

  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  dht.begin();
  sensors.begin();

  // LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("LoRa no iniciado.");
  } else {
    LoRa.setSpreadingFactor(7);
    LoRa.setSignalBandwidth(125E3);
    LoRa.setCodingRate4(5);
    Serial.println("LoRa iniciado.");
  }

  // SD
  if (SD.begin(SD_CS_PIN)) {
    sdAvailable = true;
    Serial.println("Tarjeta SD inicializada.");
  } else {
    sdAvailable = false;
    Serial.println("No se pudo inicializar la tarjeta SD.");
  }

  Serial.println("Sistema listo.");
}

// --- Loop ---
void loop() {
  readAllSensors();
  evaluateAlertLevel();
  activateLocalAlerts(currentAlertLevel);
  sendLoRaAlert(currentAlertLevel);
  logDataToSD();

  Serial.println("--------------------------------");
  delay(5000);
}

// --- Funciones ---
void readAllSensors() {
  // DHT22
  currentHumidity = dht.readHumidity();
  currentTemperature = dht.readTemperature();

  if (isnan(currentHumidity) || isnan(currentTemperature)) {
    Serial.println("Error DHT22.");
    currentHumidity = 0.0;
    currentTemperature = 0.0;
  } else {
    Serial.printf("DHT22: %.1f°C, %.1f%%\n", currentTemperature, currentHumidity);
  }

  // DS18B20
  sensors.requestTemperatures();
  internalTemperature = sensors.getTempCByIndex(0);
  if (internalTemperature == DEVICE_DISCONNECTED_C) {
    Serial.println("Error DS18B20.");
    internalTemperature = 0.0;
  } else {
    Serial.printf("DS18B20: %.1f°C\n", internalTemperature);
  }

  // MQ
  mq2Value = analogRead(MQ2_PIN);
  mq135Value = analogRead(MQ135_PIN);
  Serial.printf("MQ2: %d, MQ135: %d\n", mq2Value, mq135Value);
}

void evaluateAlertLevel() {
  bool tempHigh = (currentTemperature > TEMP_CRITICA);
  bool humLow = (currentHumidity < HUM_CRITICA);
  bool gasDetected = (mq2Value > GAS_UMBRAL_MQ2 || mq135Value > GAS_UMBRAL_MQ135);

  if (tempHigh && humLow && gasDetected) {
    currentAlertLevel = AL_CRITICA;
  } else if (tempHigh && gasDetected) {
    currentAlertLevel = AL_ALTA;
  } else if ((tempHigh && humLow) || (humLow && gasDetected)) {
    currentAlertLevel = AL_MEDIA;
  } else {
    currentAlertLevel = AL_BAJA;
  }

  Serial.print("Nivel de Alerta: ");
  Serial.println(getAlertLevelString(currentAlertLevel));
}

void activateLocalAlerts(AlertLevel level) {
  switch (level) {
    case AL_BAJA:
      digitalWrite(LED_PIN, LOW);
      digitalWrite(BUZZER_PIN, LOW);
      break;
    case AL_MEDIA:
      digitalWrite(LED_PIN, HIGH);
      digitalWrite(BUZZER_PIN, LOW);
      break;
    case AL_ALTA:
      digitalWrite(LED_PIN, (millis() / 200) % 2);
      if ((millis() / 500) % 2) {
        tone(BUZZER_PIN, 1500);
      } else {
        noTone(BUZZER_PIN);
      }
      break;
    case AL_CRITICA:
      digitalWrite(LED_PIN, (millis() / 100) % 2);
      tone(BUZZER_PIN, 2000);
      break;
  }
}

void sendLoRaAlert(AlertLevel level) {
  if (level == AL_BAJA || !LoRa.beginPacket()) return;

  String message = "ALERTA_INCENDIO,Nivel:" + getAlertLevelString(level)
                 + ",Temp:" + String(currentTemperature, 1)
                 + ",Hum:" + String(currentHumidity, 1)
                 + ",MQ2:" + String(mq2Value)
                 + ",MQ135:" + String(mq135Value)
                 + ",ID:Sentinela001";

  LoRa.print(message);
  LoRa.endPacket();

  Serial.println("LoRa enviado: " + message);
}

void logDataToSD() {
  if (!sdAvailable) return;

  File dataFile = SD.open("/log_incendios.txt", FILE_APPEND);
  if (dataFile) {
    String log = String(millis() / 1000) + "s," +
                 String(currentTemperature, 1) + "," +
                 String(currentHumidity, 1) + "," +
                 String(internalTemperature, 1) + "," +
                 String(mq2Value) + "," +
                 String(mq135Value) + "," +
                 getAlertLevelString(currentAlertLevel);
    dataFile.println(log);
    dataFile.close();
    Serial.println("Log guardado en SD.");
  } else {
    Serial.println("Error al escribir en la SD.");
  }
}

String getAlertLevelString(AlertLevel level) {
  switch (level) {
    case AL_BAJA: return "BAJA";
    case AL_MEDIA: return "MEDIA";
    case AL_ALTA: return "ALTA";
    case AL_CRITICA: return "CRITICA";
    default: return "DESCONOCIDO";
  }
}
