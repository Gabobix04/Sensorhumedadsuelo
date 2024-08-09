#include <WiFi.h>
#include <HTTPClient.h>
#include <Base64.h>
#include <ESP32Time.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <BluetoothSerial.h>

#if !defined(CONFIG_BT_ENABLED) || !defined(CONFIG_BLUEDROID_ENABLED)
#error Bluetooth no activado! Activa la conexion Bluetooth.
#endif

// Definición de pines
#define PIN_HUMEDAD_SUELO 34 // Pin para el sensor de humedad del suelo
#define PIN_RELE 26          // Pin para el relé (bombilla)
#define PIN_BUZZER 27        // Pin para el buzzer de alerta

// Umbral de humedad para la alerta de sequía
#define UMBRAL_HUMEDAD 25  // Porcentaje de humedad mínimo aceptable

unsigned long previousMillis = 0;
const long interval = 3600000;

// Configuración de WiFi
const char* ssid = "Your wifi";
const char* password = "Your password";

// Configuración de Twilio
const char* twilio_account_sid = "your twilio sid";
const char* twilio_auth_token = "your twilio token";
const char* twilio_phone_number = "your twilio phone";
const char* recipient_phone_number = "your phone";
const char* twilio_whatsapp_number = "whatsapp:+(twilio phone number whatsapp)";
const char* recipient_whatsapp_number = "whatsapp:+(country code+phone number";

// Configuración de MQTT
const char* mqtt_server = "your ip server mqtt";
const int mqtt_port = " ";
const char* mqtt_user = "your user";
const char* mqtt_password = "your password";
const char* publishTopic = "data";

// Configuración Servidor NTP
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -5 * 3600;
const int daylightOffset_sec = 0;

WiFiClient wifiClient; // Cliente para MQTT
WiFiClientSecure secureClient; // Cliente seguro para HTTPS
ESP32Time rtc;
PubSubClient client(wifiClient);
BluetoothSerial SerialBT;

// Definir el callback
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensaje recibido en el tópico: ");
  Serial.println(topic);
  
  // Convertir el payload en una cadena
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Mensaje: ");
  Serial.println(message);

  // Procesar el mensaje recibido
  // Aquí puedes añadir lógica para manejar diferentes mensajes
}

void setup() {
  // Iniciar la comunicación serie
  Serial.begin(115200);

  // Configurar los pines
  pinMode(PIN_HUMEDAD_SUELO, INPUT);
  pinMode(PIN_RELE, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);

  // Inicializar el relé y el buzzer en estado apagado
  digitalWrite(PIN_RELE, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  
  struct tm timeinfo;
  if (getLocalTime(&timeinfo)) {
    rtc.setTimeStruct(timeinfo);
  } else {
    Serial.println("Failed to obtain time");
  }

  // Conectar a WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando a WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConectado a WiFi");

  // Configurar MQTT y el callback
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Configurar cliente seguro
  secureClient.setInsecure(); // Desactiva la verificación de certificados
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Intentando conexión MQTT...");
    if (client.connect("ESP32Client", mqtt_user, mqtt_password)) {
      Serial.println("Conectado!");
      client.subscribe(publishTopic);
      Serial.print("Suscrito al tema: ");
      Serial.println(publishTopic);
    } else {
      Serial.print("Falló con error ");
      Serial.print(client.state());
      Serial.println(" Intentando de nuevo en 5 segundos");
      delay(5000);
    }
  }
}

void loop() {

   if(!SerialBT.connect()){
    SerialBT.disconnect();
    SerialBT.end();
  }

  if (!client.connected()) {
    reconnect();
  }
  client.loop();  // Mantiene la conexión MQTT
  
  // Leer el valor analógico del sensor de humedad del suelo
  int valorHumedadSuelo = analogRead(PIN_HUMEDAD_SUELO);

  // Convertir el valor a un porcentaje (ajustar según calibración)
  float porcentajeHumedadSuelo = map(valorHumedadSuelo, 4095, 0, 0, 100);


  // Publicar datos en MQTT
  publishData(porcentajeHumedadSuelo);


  // Imprimir el valor en el Monitor Serie
  Serial.print("Valor de Humedad del Suelo: ");
  Serial.print(valorHumedadSuelo);
  Serial.print(" - Porcentaje de Humedad del Suelo: ");
  Serial.print(porcentajeHumedadSuelo);
  Serial.println("%");

  handleBluetoothCommunication();
  SerialBT.print(valorHumedadSuelo);
  SerialBT.println(";");

  // Verificar si el porcentaje de humedad está por debajo del umbral
  if (porcentajeHumedadSuelo < UMBRAL_HUMEDAD) {
    // Activar el relé (bombillo) y el buzzer
    digitalWrite(PIN_RELE, HIGH);
    delay(1000);
    digitalWrite(PIN_RELE, LOW);
    delay(1000);

    for (int i = 0; i < 5; i++) {
      digitalWrite(PIN_BUZZER, HIGH);
      delay(500);
      digitalWrite(PIN_BUZZER, LOW);
      delay(500);
    }

    // Enviar una alerta por SMS
    enviarAlertaSMS(porcentajeHumedadSuelo);
    enviarAlertaWhatsApp(porcentajeHumedadSuelo);

    // Imprimir mensaje de alerta en el Monitor Serie
    Serial.println("¡Alerta de sequía! Humedad del suelo críticamente baja.");
  } else {
    // Apagar el relé (bombillo) y el buzzer
    digitalWrite(PIN_RELE, LOW);
    digitalWrite(PIN_BUZZER, LOW);
  }

  // Esperar 10 segundos antes de la próxima lectura
  delay(10000);
}

void enviarAlertaSMS(float porcentajeHumedad) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "api twilio" + String(twilio_account_sid) + "/Messages.json";

    http.begin(secureClient, url);  // Usar HTTPS
    String auth = String(twilio_account_sid) + ":" + String(twilio_auth_token);
    auth = base64::encode(auth);
    http.addHeader("Authorization", "Basic " + auth);
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");

    String mensaje = "Alerta de sequía: La humedad del suelo ha caído al " + String(porcentajeHumedad) + "%.";
    String payload = "To=" + String(recipient_phone_number) + "&From=" + String(twilio_phone_number) + "&Body=" + mensaje;

    int httpResponseCode = http.POST(payload);
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println(httpResponseCode);
      Serial.println(response);
    } else {
      Serial.print("Error en la solicitud HTTP: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("No conectado a WiFi");
  }
}

void enviarAlertaWhatsApp(float porcentajeHumedad) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "api twilio";

    http.begin(secureClient, url);  // Usar HTTPS
    http.addHeader("Content-Type", "application/json");

    String mensaje = "¡Alerta de sequía! Humedad del suelo críticamente baja.";
    String payload = "{\"To\": \"" + String(recipient_whatsapp_number) + "\", \"From\": \"" + String(twilio_whatsapp_number) + "\", \"Body\": \"" + mensaje + "\"}";

    int httpResponseCode = http.POST(payload);
    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.println(httpResponseCode);
      Serial.println(response);
    } else {
      Serial.print("Error en la solicitud HTTP: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("No conectado a WiFi");
  }
}

void publishData(float porcentajeHumedadSuelo) {
  DynamicJsonDocument jsonDocument(256); // Tamaño del buffer de JSON, ajustar según sea necesario
  JsonObject root = jsonDocument.to<JsonObject>();

  root["humedadSuelo"] = porcentajeHumedadSuelo;
  root["timestamp"] = rtc.getTime("%d/%m/%Y, %H:%M:%S");

  String jsonStr;
  serializeJson(root, jsonStr);

  // Imprimir el contenido del JSON en el Monitor Serie
  Serial.print("Publicando datos en MQTT: ");
  Serial.println(jsonStr);

  client.publish(publishTopic, jsonStr.c_str());
}

void handleBluetoothCommunication() {
  client.endPublish();
  client.unsubscribe(publishTopic);  
  client.disconnect();
  delay(500);
  SerialBT.begin("Skyber");
  delay(3000);
}