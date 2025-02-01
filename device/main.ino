#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#include <HTTPClient.h>
#include "bsec.h"
#define RESET_BUTTON 0
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Preferences preferences;
AsyncWebServer server(80);


String ssid = "";
String password = "";


const char* apSSID = "BlinkIA_Config";
const char* apPassword = "12345678";

// Sensor BME680
#define SEALEVELPRESSURE_HPA (1013.25)
Bsec iaqSensor;

bool wifiConnected = false;
bool sensorInitialized = false;

unsigned long lastApiCall = 0;
const unsigned long apiInterval = 5000;


unsigned long lastBlinkTime = 0;
const unsigned long blinkDelay = 4000;
int blinkState = 0;

int leftEyeX = 45, rightEyeX = 80, eyeY = 18;
int eyeWidth = 25, eyeHeight = 30;
int targetOffsetX = 0, targetOffsetY = 0, moveSpeed = 5;
unsigned long moveTime = 0;

String serverName = "";

void setup() {
    Serial.begin(115200);
    checkResetButton();
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("Error al inicializar OLED");
        while (true);
    }

    preferences.begin("config", true);
    ssid = preferences.getString("ssid", "");
    password = preferences.getString("password", "");
    serverName = preferences.getString("api_url", ""); 
    preferences.end();



        Serial.println("Intentando conectar a WiFi guardado...");
        WiFi.begin(ssid.c_str(), password.c_str());

        display.clearDisplay();
        display.setTextSize(1);
        display.setTextColor(WHITE);
        display.setCursor(0, 0);
        display.println("Iniciando...");
        display.display();
        
        unsigned long startAttemptTime = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000) {
            delay(500);
            Serial.print(".");
            
        }

        if (WiFi.status() == WL_CONNECTED) {
            Serial.println("\nWiFi conectado!");
            display.println(WiFi.localIP());
            display.clearDisplay();
            display.println("WiFi conectado!");
            display.println(WiFi.localIP());
            display.display();
            wifiConnected = true;
        } else {
            Serial.println("\nNo se pudo conectar. Iniciando modo AP...");
            startAccessPoint();
        }
        
    iaqSensor.begin(BME68X_I2C_ADDR_HIGH, Wire);
    if (iaqSensor.bsecStatus == BSEC_OK) {
        bsec_virtual_sensor_t sensorList[10] = {
            BSEC_OUTPUT_RAW_TEMPERATURE,
            BSEC_OUTPUT_RAW_PRESSURE,
            BSEC_OUTPUT_RAW_HUMIDITY,
            BSEC_OUTPUT_RAW_GAS,
            BSEC_OUTPUT_IAQ,
            BSEC_OUTPUT_STATIC_IAQ,
            BSEC_OUTPUT_CO2_EQUIVALENT,
            BSEC_OUTPUT_BREATH_VOC_EQUIVALENT,
            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_TEMPERATURE,
            BSEC_OUTPUT_SENSOR_HEAT_COMPENSATED_HUMIDITY,
        };

        iaqSensor.updateSubscription(sensorList, 10, BSEC_SAMPLE_RATE_LP);
        checkIaqSensorStatus();
        sensorInitialized = true;
        
    } else {
        Serial.println("Error al inicializar el sensor BME680");
    }
}

void loop() {
    unsigned long currentTime = millis();

    if (sensorInitialized && WiFi.status() == WL_CONNECTED && currentTime - lastApiCall >= apiInterval) {
        if (iaqSensor.run()) {
            float temperature = iaqSensor.temperature;
            float humidity = iaqSensor.humidity;
            float pressure = iaqSensor.pressure / 100.0;
            float iaq = iaqSensor.iaq;

            sendDataToAPI(temperature, humidity, pressure, iaq);
        }
        lastApiCall = currentTime;
    }else {
      checkIaqSensorStatus();
    }
    drawEyes();
}

void startAccessPoint() {
    WiFi.softAP(apSSID, apPassword);
    IPAddress IP = WiFi.softAPIP();
    Serial.print("AP Iniciado, IP: ");
    Serial.println(IP);

    display.clearDisplay();
    display.println("Modo Configuración");
    display.println("Conéctate a:");
    display.println(apSSID);
    display.println("192.168.4.1");
    display.display();

    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    String htmlPage = 
        "<!DOCTYPE html><html><head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width, initial-scale=1'>"
        "<style>"
        "body { font-family: Arial, sans-serif; text-align: center; background-color: #f4f4f4; }"
        "h1 { color: #333; }"
        "form { max-width: 300px; margin: 20px auto; padding: 20px; background: #fff; border-radius: 10px; box-shadow: 0px 0px 10px rgba(0, 0, 0, 0.1); }"
        "input { width: 90%; padding: 10px; margin: 10px 0; border: 1px solid #ddd; border-radius: 5px; }"
        "input[type='submit'] { background: #28a745; color: white; font-size: 16px; border: none; cursor: pointer; }"
        "input[type='submit']:hover { background: #218838; }"
        "</style>"
        "</head><body>"
        "<h1>Configurar WiFi y API</h1>"
        "<form action='/connect' method='POST'>"
        "SSID: <input type='text' name='ssid' placeholder='Ingresa el SSID'><br>"
        "Contraseña: <input type='password' name='password' placeholder='Ingresa la contraseña'><br>"
        "API URL: <input type='text' name='api_url' placeholder='http://xxx.xxx.xxx.xxx:puerto/datos'><br>"
        "<input type='submit' value='Guardar y Conectar'>"
        "</form>"
        "</body></html>";
    
    request->send(200, "text/html", htmlPage);
});



     server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->hasParam("ssid", true) && request->hasParam("password", true) && request->hasParam("api_url", true)) {
        ssid = request->getParam("ssid", true)->value();
        password = request->getParam("password", true)->value();
        serverName = request->getParam("api_url", true)->value();

        preferences.begin("config", false);
        preferences.putString("ssid", ssid);
        preferences.putString("password", password);
        preferences.putString("api_url", serverName);
        preferences.end();

        request->send(200, "text/plain", "Configuración guardada. Reiniciando...");
        delay(1000);
        ESP.restart();
    } else {
        request->send(400, "text/plain", "Error: Faltan datos.");
    }
});


    server.begin();
    Serial.println("Servidor web iniciado...");
}



void sendDataToAPI(float temperature, float humidity, float pressure, float iaq) {
    if (WiFi.status() == WL_CONNECTED) {


        // No comentar para visualizar datos de sensor en terminal
        /*
        if (serverName == "" || serverName.length() < 10) {
            Serial.println("Error: No se ha configurado una dirección válida para el API.");
            return;
        }

        if (isnan(temperature) || isnan(humidity) || isnan(pressure) || isnan(iaq)) {
            Serial.println("Error: Datos inválidos del sensor, no enviados.");
            return;
        }
      */

        HTTPClient http;
        http.setTimeout(5000);

        String jsonData = "{";
        jsonData += "\"id\": \"00001\",";
        jsonData += "\"temperature\":" + String(temperature, 2) + ",";
        jsonData += "\"humidity\":" + String(humidity, 2) + ",";
        jsonData += "\"pressure\":" + String(pressure, 2) + ",";
        jsonData += "\"iaq\":" + String(iaq, 2);
        jsonData += "}";


        //No comentar para ver datos enviados a API
       /*
        Serial.println("JSON enviado:");
        Serial.println(jsonData);
       */

        http.begin(serverName);
        http.addHeader("Content-Type", "application/json");
        int httpResponseCode = http.POST(jsonData);


        //No comentar para ver respuesta de servidor API
        /*
        if (httpResponseCode > 0) {
            Serial.printf("Respuesta API: %d, %s\n", httpResponseCode, http.getString().c_str());
        } else {
            Serial.printf("Error en solicitud HTTP: %s\n", http.errorToString(httpResponseCode).c_str());
        }
        */
        http.end();
    }
}



void drawEyes() {
    unsigned long currentTime = millis();

    if (currentTime - lastBlinkTime > blinkDelay && blinkState == 0) {
        blinkState = 1;
        lastBlinkTime = currentTime;
    } else if (currentTime - lastBlinkTime > 150 && blinkState == 1) {
        blinkState = 0;
        lastBlinkTime = currentTime;
    }

    if (currentTime - moveTime > random(1500, 3000) && blinkState == 0) {
        targetOffsetX = random(-10, 10);
        targetOffsetY = random(-8, 8);
        moveTime = currentTime;
    }

    static int offsetX = 0, offsetY = 0;
    offsetX += (targetOffsetX - offsetX) / moveSpeed;
    offsetY += (targetOffsetY - offsetY) / moveSpeed;

    display.clearDisplay();
    if (blinkState == 0) {
        display.fillRoundRect(leftEyeX + offsetX, eyeY + offsetY, eyeWidth, eyeHeight, 5, WHITE);
        display.fillRoundRect(rightEyeX + offsetX, eyeY + offsetY, eyeWidth, eyeHeight, 5, WHITE);
    } else {
        display.fillRect(leftEyeX + offsetX, eyeY + offsetY + eyeHeight / 2 - 2, eyeWidth, 4, WHITE);
        display.fillRect(rightEyeX + offsetX, eyeY + offsetY + eyeHeight / 2 - 2, eyeWidth, 4, WHITE);
    }

    display.display();
}

void checkResetButton() {
    pinMode(RESET_BUTTON, INPUT_PULLUP);  

    Serial.println("Verificando si se presionó el botón de reset...");
    delay(500);  

    if (digitalRead(RESET_BUTTON) == LOW) {  
        Serial.println("Botón presionado, borrando configuración...");
        preferences.begin("config", false);
        preferences.clear();
        preferences.end();

        Serial.println("Configuración eliminada. Reiniciando...");
        delay(3000);
        ESP.restart();
    }
}

void checkIaqSensorStatus() {
  if (iaqSensor.bsecStatus != BSEC_OK) {
    Serial.println("Error BSEC: " + String(iaqSensor.bsecStatus));
  }
}