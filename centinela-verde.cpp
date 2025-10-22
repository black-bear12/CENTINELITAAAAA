
#include <Arduino.h>
#include <DHT.h> // Para el sensor DHT22
#include <OneWire.h> // Para el sensor DS18B20
#include <DallasTemperature.h> // Para el sensor DS18B20
#include <LoRa.h> // Para el módulo LoRa SX1278
#include <SD.h> // Para la tarjeta SD
#include <SPI.h> // Para comunicación SPI (LoRa y SD)

// --- Definiciones de Pines GPIO ---
// DHT22
#define DHT_PIN 27
#define DHT_TYPE DHT22 // Tipo de sensor DHT
DHT dht(DHT_PIN, DHT_TYPE);

// DS18B20
#define ONE_WIRE_BUS 26
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Sensores MQ (Analógicos)
#define MQ2_PIN 34 // ADC1_CH6
#define MQ135_PIN 35 // ADC1_CH7

// LoRa SX1278
#define LORA_SCK 18
#define LORA_MISO 19
#define LORA_MOSI 23
#define LORA_CS 5
#define LORA_RST 14
#define LORA_DIO0 2
#define LORA_FREQUENCY 433E6 // Frecuencia de LoRa (433 MHz)

// Alertas Locales
#define LED_PIN 12
#define BUZZER_PIN 13

// Tarjeta SD
#define SD_CS_PIN 15 // Chip Select para la tarjeta SD

// --- Umbrales Críticos ---
#define TEMP_CRITICA 40.0 // Temperatura en °C
#define HUM_CRITICA 20.0 // Humedad relativa en %
#define GAS_UMBRAL_MQ2 1500 // Valor ADC de ejemplo para MQ-2 (ajustar según calibración)
#define GAS_UMBRAL_MQ135 1200 // Valor ADC de ejemplo para MQ-135 (ajustar según calibración)

// --- Variables Globales ---
float currentTemperature = 0.0;
float currentHumidity = 0.0;
int mq2Value = 0;
int mq135Value = 0;
float internalTemperature = 0.0; // Temperatura del DS18B20

enum AlertLevel {
  AL_BAJA,
  AL_MEDIA,
  AL_ALTA,
  AL_CRITICA
};
AlertLevel currentAlertLevel = AL_BAJA;

// --- Prototipos de Funciones ---
void readAllSensors();
void evaluateAlertLevel();
void activateLocalAlerts(AlertLevel level);
void sendLoRaAlert(AlertLevel level, float temp, float hum, int mq2, int mq135);
void logDataToSD(String data);
String getAlertLevelString(AlertLevel level);

// --- Configuración Inicial ---
void setup() {
  Serial.begin(115200);
  while (!Serial); // Esperar a que el monitor serial se conecte

  Serial.println("Iniciando Sentinela Verde...");

  // Configurar pines de salida para LED y Zumbador
  pinMode(LED_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW); // Asegurarse de que el LED esté apagado al inicio
  digitalWrite(BUZZER_PIN, LOW); // Asegurarse de que el zumbador esté apagado

  // Inicializar DHT22
  dht.begin();
  Serial.println("DHT22 inicializado.");

  // Inicializar DS18B20
  sensors.begin();
  Serial.println("DS18B20 inicializado.");

  // Inicializar LoRa
  SPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS); // Inicializar SPI para LoRa
  LoRa.setPins(LORA_CS, LORA_RST, LORA_DIO0);
  if (!LoRa.begin(LORA_FREQUENCY)) {
    Serial.println("Error al iniciar LoRa. Verifique las conexiones.");
    while (1); // Detener ejecución si LoRa no inicia
  }
  LoRa.setSpreadingFactor(7); // Factor de dispersión (SF7 a SF12)
  LoRa.setSignalBandwidth(125E3); // Ancho de banda de la señal (125 kHz)
  LoRa.setCodingRate4(5); // Tasa de codificación (4/5)
  Serial.println("LoRa inicializado.");

  // Inicializar Tarjeta SD
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("Error al iniciar la tarjeta SD o no encontrada.");
    // No detener la ejecución, el sistema puede funcionar sin SD
  } else {
    Serial.println("Tarjeta SD inicializada.");
  }

  Serial.println("Setup completo. Iniciando monitoreo...");
}

// --- Bucle Principal ---
void loop() {
  readAllSensors(); // Leer todos los sensores
  evaluateAlertLevel(); // Evaluar el nivel de alerta
  activateLocalAlerts(currentAlertLevel); // Activar alertas visuales/sonoras
  sendLoRaAlert(currentAlertLevel, currentTemperature, currentHumidity, mq2Value, mq135Value); // Enviar alerta por LoRa
  
  // Registrar datos en SD
  String logEntry = String(millis()) + "," +
                    String(currentTemperature, 1) + "," +
                    String(currentHumidity, 1) + "," +
                    String(internalTemperature, 1) + "," +
                    String(mq2Value) + "," +
                    String(mq135Value) + "," +
                    getAlertLevelString(currentAlertLevel);
  logDataToSD(logEntry);

  Serial.println("------------------------------------");
  delay(5000); // Esperar 5 segundos antes de la próxima lectura
}

// --- Implementación de Funciones ---

/**
 * @brief Lee los valores de todos los sensores conectados.
 */
void readAllSensors() {
  // Leer DHT22
  currentHumidity = dht.readHumidity();
  currentTemperature = dht.readTemperature(); // Lee temperatura en Celsius

  if (isnan(currentHumidity) || isnan(currentTemperature)) {
    Serial.println("Error al leer el sensor DHT22!");
    currentTemperature = 0.0; // Valores por defecto en caso de error
    currentHumidity = 0.0;
  } else {
    Serial.printf("DHT22: Temp=%.1f°C, Hum=%.1f%%\n", currentTemperature, currentHumidity);
  }

  // Leer DS18B20 (temperatura interna)
  sensors.requestTemperatures(); // Enviar comando para obtener temperaturas
  internalTemperature = sensors.getTempCByIndex(0); // Obtener la primera (y única) temperatura

  if (internalTemperature == DEVICE_DISCONNECTED_C) {
    Serial.println("Error al leer el sensor DS18B20!");
    internalTemperature = 0.0; // Valor por defecto en caso de error
  } else {
    Serial.printf("DS18B20: Temp Interna=%.1f°C\n", internalTemperature);
  }

  // Leer MQ-2 (valor analógico)
  mq2Value = analogRead(MQ2_PIN);
  Serial.printf("MQ-2: %d\n", mq2Value);

  // Leer MQ-135 (valor analógico)
  mq135Value = analogRead(MQ135_PIN);
  Serial.printf("MQ-135: %d\n", mq135Value);
}

/**
 * @brief Evalúa las lecturas de los sensores para determinar el nivel de alerta.
 *        Basado en la tabla de "Lógica de Alertas" del documento.
 */
void evaluateAlertLevel() {
  bool tempHigh = (currentTemperature > TEMP_CRITICA);
  bool humLow = (currentHumidity < HUM_CRITICA);
  bool gasDetected = (mq2Value > GAS_UMBRAL_MQ2 || mq135Value > GAS_UMBRAL_MQ135);

  // Lógica de Alertas (del documento)
  // Crítica: T + H + G (Las 3 juntas) -> 85-95%
  if (tempHigh && humLow && gasDetected) {
    currentAlertLevel = AL_CRITICA;
  }
  // Alta: T + G -> 60-75%
  else if (tempHigh && gasDetected) {
    currentAlertLevel = AL_ALTA;
  }
  // Media: T + H o H + G -> 40-60%
  else if ((tempHigh && humLow) || (humLow && gasDetected)) {
    currentAlertLevel = AL_MEDIA;
  }
  // Baja: Ninguna o Solo Una -> <30%
  else {
    currentAlertLevel = AL_BAJA;
  }

  Serial.printf("Nivel de Alerta: %s\n", getAlertLevelString(currentAlertLevel).c_str());
}

/**
 * @brief Activa las alertas locales (LED y Zumbador) según el nivel de alerta.
 * @param level El nivel de alerta actual.
 */
void activateLocalAlerts(AlertLevel level) {
  switch (level) {
    case AL_BAJA:
      digitalWrite(LED_PIN, LOW); // LED apagado
      digitalWrite(BUZZER_PIN, LOW); // Zumbador apagado
      break;
    case AL_MEDIA:
      digitalWrite(LED_PIN, HIGH); // LED encendido fijo
      digitalWrite(BUZZER_PIN, LOW); // Zumbador apagado (solo advertencia visual)
      break;
    case AL_ALTA:
      // LED parpadea rápido, zumbador suena intermitente
      digitalWrite(LED_PIN, (millis() / 100) % 2); // Parpadeo rápido
      if ((millis() / 500) % 2) { // Sonido intermitente
        tone(BUZZER_PIN, 1500); // Tono de 1.5 kHz
      } else {
        noTone(BUZZER_PIN);
      }
      break;
    case AL_CRITICA:
      // LED parpadea muy rápido, zumbador suena continuo y fuerte
      digitalWrite(LED_PIN, (millis() / 50) % 2); // Parpadeo muy rápido
      tone(BUZZER_PIN, 2000); // Tono de 2 kHz continuo
      break;
  }
}

/**
 * @brief Envía un mensaje de alerta por LoRa al centro de monitoreo.
 * @param level El nivel de alerta actual.
 * @param temp Temperatura actual.
 * @param hum Humedad actual.
 * @param mq2 Valor del sensor MQ-2.
 * @param mq135 Valor del sensor MQ-135.
 */
void sendLoRaAlert(AlertLevel level, float temp, float hum, int mq2, int mq135) {
  if (level == AL_BAJA) {
    // No enviar alertas LoRa para nivel bajo, solo monitoreo local
    return;
  }

  String message = "ALERTA_INCENDIO,Nivel:";
  message += getAlertLevelString(level);
  message += ",Temp:";
  message += String(temp, 1);
  message += ",Hum:";
  message += String(hum, 1);
  message += ",MQ2:";
  message += String(mq2);
  message += ",MQ135:";
  message += String(mq135);
  message += ",ID:Sentinela001"; // Identificador del nodo

  Serial.print("Enviando alerta LoRa: ");
  Serial.println(message);

  LoRa.beginPacket();
  LoRa.print(message);
  LoRa.endPacket();

  Serial.println("Alerta LoRa enviada.");
}

/**
 * @brief Registra los datos en un archivo en la tarjeta SD.
 * @param data La cadena de datos a registrar.
 */
void logDataToSD(String data) {
  if (SD.begin(SD_CS_PIN)) { // Re-inicializar SD por si se desconectó o falló
    File dataFile = SD.open("/log_incendios.txt", FILE_APPEND); // Abrir archivo en modo append

    if (dataFile) {
      dataFile.println(data);
      dataFile.close();
      Serial.println("Datos registrados en SD.");
    } else {
      Serial.println("Error al abrir el archivo en la tarjeta SD.");
    }
  } else {
    Serial.println("Tarjeta SD no disponible para registro.");
  }
}

/**
 * @brief Convierte el nivel de alerta a una cadena de texto.
 * @param level El nivel de alerta.
 * @return Una cadena de texto representando el nivel de alerta.
 */
String getAlertLevelString(AlertLevel level) {
  switch (level) {
    case AL_BAJA: return "BAJA";
    case AL_MEDIA: return "MEDIA";
    case AL_ALTA: return "ALTA";
    case AL_CRITICA: return "CRITICA";
    default: return "DESCONOCIDO";
  }
}
