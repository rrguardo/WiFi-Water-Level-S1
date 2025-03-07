/*
 * This file is part of WiFi Water Level S1 project.
 *
 * WiFi Water Level S1 is free software and hardware: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

// IDE and Board
// Arduino Framework version: 2.3.4
// Arduino Board Module: ESP32C3 Dev Module
// Board: ESP32-C3-MINI-1U-H4
// Board Manager URL: https://arduino.esp8266.com/stable/package_esp8266com_index.json

// Libraries used:
// WiFiManager by tzapu version: 2.0.16-rc.2

// Usage DOC
//
// * To Disable live updates set  DEV_MODE = false
// * To enable UART (A02YYUW) sensor uncomment >> #define UART_SENSOR   this will disable HCSR04 family sensors
//


#include <Arduino.h>

#include <WiFiManager.h>
#include <HTTPClient.h>
//#include "certs.h"

#include <nvs_flash.h>

#include <String.h>
#include "driver/adc.h"
#include "esp_sleep.h"

#include <Update.h>

#include "esp_wifi.h"


// uncomment line below to use UART sensors like A02YYUW (This functionality has not been tested yet.)
//#define UART_SENSOR


#ifdef UART_SENSOR
  #include <HardwareSerial.h>

  // Defining the UART pins for the A02YYUW sensor
  #define A02YYUW_RX_PIN 20  // GPIO20 connected to the sensor TX
  #define A02YYUW_TX_PIN 21  // GPIO21 connected to the sensor RX
  // Initialize the HardwareSerial port
  HardwareSerial A02YYUW_Serial(0);
#endif



//#define DEBUG  // Comment this line to disable serial prints

#define EMAIL_MAX_LENGTH 128  // Nuevo tamaño para el email


// DEV MODE allow app update remote
const bool DEV_MODE = true;
const char* firmwareUrl = "https://waterlevel.pro/static/fw";
int FIRMW_VER = 22;

int32_t rssi = 0;


const int pinVoltageInput = 2;  // Using ADC1_CH2 en GPIO2

// Define the pins for the HCSR04 sensor
const int trigPin = 20;
const int echoPin = 21;

// Button Pin (FLASH button)
const int pinBoton = 10;

enum ConfigStatus {
  SCREEN,
  WIFI,
  SCREEN_WIFI,
  WIFI_SETUP,
  MIN_LEVEL,
  SELECT_MODE
};

ConfigStatus CurrentStatus = WIFI_SETUP;
int last_distance = 0; // used to set min level

int WIFI_POOL_TIME = 30; // wifi usage pool time in seconds
int SONIC_POOL_TIME = 3; // sonic usage pool time in seconds

int last_wifi_usage = 0;
int last_sonic_usage = 0;
unsigned long tiempoArranque;
unsigned long tiempoBateria = 0;


// If will use a custom private server, replace this with the URL you want to send the sensor info. See **demo_server.py** for a free open source server sample in python-flask
// Waterlevel.pro offers free server services with a sensor info update time of up to 120 seconds.
const char* host_url = "https://api.waterlevel.pro/update";


const char* host_link_url = "https://api.waterlevel.pro/link";

char api_key[128] = "-";  //  PRIVATE KEY HERE // "-" none-null-api-key
// use value "-" for automatic api key setup
// or manually generate and use new key in developer zone at https://waterlevel.pro/settings   https://waterlevel.pro/add_sensor

const char* WIFI_NAME = "WaterLevelProSetup";
const char* WIFI_PASSW = "1122334455";
char email[EMAIL_MAX_LENGTH] = "-";  // Variable to store email
 
WiFiManager wm;
// Set custom email parameter
WiFiManagerParameter custom_email("email", "Enter Email", email, EMAIL_MAX_LENGTH, "type='email' required placeholder='example@example.com'");


nvs_handle_t my_nvs_handle;

void setup() {
  // put your setup code here, to run once:

  // ADC init
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(ADC1_CHANNEL_2, ADC_ATTEN_DB_11);  // Configurar atenuación del canal
  analogReadResolution(12);  // Establecer resolución del ADC a 12 bits
  adcAttachPin(pinVoltageInput);  // Adjuntar el pin al canal ADC
  UnderVoltageProt(true, 0); //check undervoltage

  Serial.begin(115200);

  #ifdef UART_SENSOR
    // Iniciar la comunicación UART con el sensor
    A02YYUW_Serial.begin(9600, SERIAL_8N1, A02YYUW_RX_PIN, A02YYUW_TX_PIN);
  #endif

  #ifdef DEBUG
    Serial.println("Setup Started!");
  #else
    delay(5);
  #endif

  tiempoArranque = millis();
  tiempoBateria = 0;


  // Init memory flash
  esp_err_t err = nvs_flash_init();

  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      // NVS partition was truncated and needs to be erased
      // Retry nvs_flash_init
      ESP_ERROR_CHECK(nvs_flash_erase());
      err = nvs_flash_init();
  }
  ESP_ERROR_CHECK( err );
  err = nvs_open("storage", NVS_READWRITE, &my_nvs_handle);
  if (err != ESP_OK) {
    
    #ifdef DEBUG
      Serial.println("Error opening nvs handler");
    #else
      delay(5);
    #endif

  } else {
    #ifdef DEBUG
      Serial.println("Done opening nvs handler");
    #else
      delay(5);
    #endif
  }

  //load_email();

 
  // Add parameter to WiFiManager
  wm.addParameter(&custom_email);
  // Set config save callback
  wm.setSaveConfigCallback(saveConfigCallback);

  load_private_key();
  load_settings();
  

  if(CurrentStatus == WIFI_SETUP){
    SetupResetWifi();
  }

  #ifndef UART_SENSOR
    // Set the trigger pin as an output and the echo pin as an input
    pinMode(trigPin, OUTPUT);
    pinMode(echoPin, INPUT);
  #endif

  // Configuramos el pin del botón como entrada
  pinMode(pinBoton, INPUT_PULLUP);
  // Asociamos la función botonPresionado() al pin del botón
  attachInterrupt(pinBoton, botonPresionado, FALLING);

}

void saveConfigCallback() {
  Serial.println("Enter saveConfigCallback");
  // Placeholder for additional code to handle saving to persistent storage, if needed

  // Guardar el email en EEPROM
  const char *newEmail = custom_email.getValue();
  strncpy(email, newEmail, sizeof(email) - 1); // Copiar el nuevo valor
  email[sizeof(email) - 1] = '\0'; // Asegurar que termina con '\0'


  if(myStrlen(email) >= 7 ){

    #ifdef DEBUG
      Serial.print("Email to linked: ");
      Serial.println(email);
    #else
       delay(5);
    #endif
  }else{
    Serial.print("Invalid email: ");
    Serial.println(email);
    strcpy(email, "-");
  }

}

int myStrlen(const char* str) {
  int len = 0;
  while (*str != '\0') {
    len++;
    str++;
  }
  return len;
}


void load_private_key(){

  size_t required_size = 0;
  nvs_get_str(my_nvs_handle, "PrivateKey", NULL, &required_size);
  if (required_size != 0 and required_size > 10) {
    char* tmp_key = (char*)malloc(required_size);
    nvs_get_str(my_nvs_handle, "PrivateKey", tmp_key, &required_size);
    strcpy(api_key, tmp_key);  
    free(tmp_key);

    #ifdef DEBUG
      Serial.print("Private Key loaded from memory: ");
      Serial.println(api_key);
    #else
       delay(5);
    #endif

  }else if(strcmp(api_key, "-") != 0){
    nvs_set_str(my_nvs_handle, "PrivateKey", api_key);
    nvs_commit(my_nvs_handle);

    #ifdef DEBUG
      Serial.print("Private Key loaded from code: ");
      Serial.println(api_key);
    #else
       delay(5);
    #endif
  }

  if(myStrlen(api_key) < 10 && CurrentStatus != WIFI_SETUP){
    #ifdef DEBUG
      Serial.print("ERROR: Short API KEY: ");
      Serial.println(api_key);
    #else
       delay(5);
    #endif
    esp_system_abort("restart_after_wakeup");
  }


}


void restart_after_wakeup()
{
  // Hacer un restart
  //esp_system_abort("restart_after_wakeup");
  esp_system_abort("restart_after_wakeup");
}

float UnderVoltageProt(bool is_setup, int tryNum){
  const float referenciaADC = 8.39;  // Ajustar según la referencia del ADC
  float LowVoltage = 3.5; // This will trigger protection

  // Realizar una medición ADC
  int lectura_adc = analogRead(pinVoltageInput);

  // Convertir el valor del ADC a voltaje
  float voltaje = (((float)lectura_adc + 40.2) / 474.0) +0.16;
  if(lectura_adc <= 400){
    voltaje = 5.0;
  }

  int delay_time = 1000;
  if(is_setup){
    delay_time = 10;
    //LowVoltage = LowVoltage + 0.08; //setup stage earn +0.7v while wifi-off
  }

  // Imprimir el resultado
  #ifdef DEBUG

  Serial.print("Valor ADC: ");
  Serial.print(lectura_adc);

  Serial.print(", Voltaje: ");
  Serial.print(voltaje, 2);  // Mostrar solo dos decimales
  Serial.println(" V");

  #else
       delay(5);
    #endif

  if(voltaje <= LowVoltage){
    Serial.println("Under Voltage Detected");
    if(tryNum < 3){
      non_lock_delay(delay_time);
      return UnderVoltageProt(is_setup, tryNum + 1);
    }

    if(is_setup){
      // Establecer la función de inicio de sueño profundo
      esp_set_deep_sleep_wake_stub(restart_after_wakeup);
      SleepSave(5*60); // 5 min-sleep-on-low-voltage
    }else{
      esp_system_abort("restart_after_wakeup");
    }
  }

  return voltaje;
}


void loop() {
  // put your main code here, to run repeatedly:

  // restart ESP after 30 minutes of continue usage to preven overflows, locks, ...++
  if(tiempoArranque > 30*60*1000){
    esp_system_abort("restart_after_wakeup");
  }

  float LastVoltage = UnderVoltageProt(false, 0); //check undervoltage

  last_distance = getDistance();

  ConnectWifi();
  for (int i2 = 0; i2 < 3; i2++) {
    if (HttpSendInfo(last_distance, LastVoltage)){
      break;
    }
    non_lock_delay(5000); // wait 5 seconds before next try
    WIFI_POOL_TIME = 300; // sleep 5 min. if not internet
  }
  

  wm.disconnect();
  WiFi.mode(WIFI_OFF);

  #ifdef DEBUG
    Serial.println("sleeping to save energy");
  #else
       delay(5);
    #endif

  SleepSave(WIFI_POOL_TIME);
  delay(5);

}


#ifndef UART_SENSOR
  // non UART distance function
  int getDistance() {
    // Trigger the sensor to send a pulse
    digitalWrite(trigPin, LOW);
    delayMicroseconds(5);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(15);
    digitalWrite(trigPin, LOW);

    // Measure the duration of the pulse on the echo pin
    long duration = pulseIn(echoPin, HIGH);

    // Calculate the distance based on the speed of sound (343 meters/second)
    // and the duration of the pulse
    int distance = duration * 0.0343 / 2;

    #ifdef DEBUG
      Serial.print("Distance: ");
      Serial.println(distance);
    #else
        delay(5);
      #endif

    return distance;
  }
#else
  // UART distance function
  int getDistance() {
    // Verificar si hay datos disponibles del sensor
    if (A02YYUW_Serial.available() >= 4) {
      // Leer los 4 bytes del marco de datos
      uint8_t data[4];
      for (int i = 0; i < 4; i++) {
        data[i] = A02YYUW_Serial.read();
      }
      // Verificar el byte de inicio
      if (data[0] == 0xFF) {
        // Calcular el checksum
        uint8_t checksum = (data[0] + data[1] + data[2]) & 0xFF;
        // Verificar el checksum
        if (checksum == data[3]) {
          // Calcular la distancia en centímetros
          int distance = ((data[1] << 8) | data[2]) / 10;
          return distance;
        }
      }
    }
    // Retornar 0 en caso de error
    return 0;
  }
#endif


// Función que se ejecutará cuando el botón se presione
ICACHE_RAM_ATTR void botonPresionado() {

  if(millis() < 1000) return;

  #ifdef DEBUG
    Serial.println("El botón está presionado");
  #else
       delay(5);
    #endif

  nvs_set_i32(my_nvs_handle, "0-status", (int32_t)WIFI_SETUP);
  nvs_commit(my_nvs_handle);

  CurrentStatus = WIFI_SETUP;
  wm.resetSettings();

  delay(5);
  esp_system_abort("restart_after_wakeup");

}


void SleepSave(int seconds){
  // IMPORTANT: Connect D0(GPIO-16) to RST after programing. This will reset the device after waking up (easy wifi/device restart)
  #ifdef DEBUG
    Serial.println("Will sleep now");
  #else
       delay(5);
    #endif
  //Deep-sleep for 9 seconds, and then wake up
  esp_set_deep_sleep_wake_stub(restart_after_wakeup);

  WiFi.disconnect(true);
  adc_power_off();

  esp_wifi_stop();

  //ESP.deepSleep(seconds*1000000);
  esp_deep_sleep(seconds*1000000);
  //system_deep_sleep_instant();
  // chip will not perform RF calibration after waking up
  // 1 -calib  ## 2 - no-calib
  //system_deep_sleep_set_option(2);

}


void non_lock_delay(unsigned long mili_seconds) {
  static unsigned long previousMillis = 0;
  static bool isWaiting = false;

  unsigned long currentMillis = millis();

  // Inicio de espera
  if (!isWaiting) {
    previousMillis = currentMillis;
    isWaiting = true;
  }

  while (isWaiting) {
    currentMillis = millis();
    
    // Verificar si el tiempo ha pasado
    if (currentMillis - previousMillis >= mili_seconds) {
      isWaiting = false; // Reiniciar la espera

    }

    // Permitir que el sistema operativo maneje tareas críticas
    delay(1); // Mantener el sistema operativo funcionando mientras se espera
  }
}


void splitString(const String &dataString, int *resultArray, int arraySize) {
  // Utilizar strtok para dividir la cadena en tokens basados en el delimitador '|'
  char *token = strtok(const_cast<char*>(dataString.c_str()), "|");

  // Iterar a través de los tokens y convertirlos en enteros
  int i = 0;
  while (token != NULL && i < arraySize) {
    // Utilizar atoi para convertir el token en un entero
    resultArray[i] = atoi(token);

    // Obtener el siguiente token
    token = strtok(NULL, "|");

    // Incrementar el índice
    i++;
  }
}


String urlEncode(String str) {
  String encoded = "";
  char c;
  char hex[4]; // Para almacenar el formato %XX

  for (size_t i = 0; i < str.length(); i++) {
    c = str.charAt(i);

    // Codificar caracteres especiales según el estándar
    if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
      encoded += c; // Dejar caracteres seguros sin cambios
    } else {
      sprintf(hex, "%%%02X", c); // Convertir a %XX
      encoded += hex;
    }
  }

  return encoded;
}


bool HttpRegDevice(){

  // Check for successful connection
  if (WiFi.status() == WL_CONNECTED) {
    #ifdef DEBUG
      Serial.println("Connected to WiFi");
    #else
       delay(5);
    #endif

    long timeout = 15000;

    HTTPClient https;
    https.setTimeout(timeout);
    https.setConnectTimeout(timeout);

    #ifdef DEBUG
      Serial.print("[HTTPS] begin...\n");
    #else
       delay(5);
    #endif


    char urlout[355];

    String email_encoded = urlEncode(email);
    // we are going to start here
    strcpy(urlout, host_link_url);
    strcat(urlout, "?key=");
    strcat(urlout, api_key);
    strcat(urlout, "&email=");
    strcat(urlout, email_encoded.c_str());



    if (https.begin(urlout)) {  // HTTPS

      //https.setVerify .setVerify(true);
      https.setReuse(false);

      // Add a custom HTTP header (replace "Your-Header" and "Header-Value" with your actual header and value)
      https.addHeader("FW-Version", (String)FIRMW_VER);
      https.addHeader("RSSI", (String)rssi);

      https.setTimeout(timeout);

      // prepare to read response headers
      const char *headerKeys[] = {"fw-version", "wpl-key"}; // Agrega todas las claves de los encabezados que deseas recopilar
      const size_t headerKeysCount = sizeof(headerKeys) / sizeof(headerKeys[0]);
      // Recopila las cabeceras HTTP especificadas
      https.collectHeaders(headerKeys, headerKeysCount);

      // start connection and send HTTP header
      int httpCode = https.GET();

      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled

        #ifdef DEBUG
          Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
        #else
          delay(5);
        #endif

        // file found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {

          String payload = https.getString();

          #ifdef DEBUG
            Serial.print("response payload: ");
            Serial.println(payload);
          #else
            delay(5);
          #endif

          if(payload == "OK"){

            String latest_relay_fw = https.header("fw-version");
            String wlp_key = https.header("wpl-key");

            #ifdef DEBUG
              Serial.print("assigned wpl-key: ");
              Serial.println(wlp_key);
              Serial.print("Old api_key: ");
              Serial.println(api_key);
            #endif

            if(wlp_key.length() >= 7)
            {
                strcpy(api_key, wlp_key.c_str());
                nvs_set_str(my_nvs_handle, "PrivateKey", api_key);
                nvs_commit(my_nvs_handle);
                delay(5);
            }
            #ifdef DEBUG
              else{
                Serial.println("Invalid key status");
              }
            #endif
            

          }

          https.end();
          return true;

        }
      } else {
        #ifdef DEBUG
          Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        #else
          delay(5);
        #endif
        https.end();
        return false;
      }

    } else {
      #ifdef DEBUG
        Serial.printf("[HTTPS] Unable to connect\n");
      #else
       delay(5);
    #endif
      return false;
    }

  } else {
    #ifdef DEBUG
      Serial.println("Failed to connect to WiFi");
    #else
       delay(5);
    #endif
  }
  return false;
}


bool HttpSendInfo(int distance, float LastVoltage){

  // Check for successful connection
  if (WiFi.status() == WL_CONNECTED) {
    #ifdef DEBUG
      Serial.println("Connected to WiFi");
    #else
       delay(5);
    #endif

    long timeout = 15000;

    HTTPClient https;
    https.setTimeout(timeout);
    https.setConnectTimeout(timeout);

    #ifdef DEBUG
      Serial.print("[HTTPS] begin...\n");
    #else
       delay(5);
    #endif

    char dist_str[10];
    itoa(distance, dist_str, 10);

    char volt_str[10];
    int reg_volt = round(LastVoltage*100);
    itoa(reg_volt, volt_str, 10);


    char urlout[255];
    // we are going to start here
    strcpy(urlout, host_url);
    strcat(urlout, "?key=");
    strcat(urlout, api_key);
    strcat(urlout, "&distance=");
    strcat(urlout, dist_str);
    strcat(urlout, "&voltage=");
    strcat(urlout, volt_str);


    if (https.begin(urlout)) {  // HTTPS

      //https.setVerify .setVerify(true);
      https.setReuse(false);

      // Add a custom HTTP header (replace "Your-Header" and "Header-Value" with your actual header and value)
      https.addHeader("FW-Version", (String)FIRMW_VER);
      https.addHeader("RSSI", (String)rssi);

      https.setTimeout(timeout);

      // prepare to read response headers
      const char *headerKeys[] = {"fw-version", "wpl"}; // Agrega todas las claves de los encabezados que deseas recopilar
      const size_t headerKeysCount = sizeof(headerKeys) / sizeof(headerKeys[0]);
      // Recopila las cabeceras HTTP especificadas
      https.collectHeaders(headerKeys, headerKeysCount);

      // start connection and send HTTP header
      int httpCode = https.GET();

      // httpCode will be negative on error
      if (httpCode > 0) {
        // HTTP header has been send and Server response header has been handled

        #ifdef DEBUG
          Serial.printf("[HTTPS] GET... code: %d\n", httpCode);
        #else
          delay(5);
        #endif

        // file found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {

          String payload = https.getString();

          #ifdef DEBUG
            Serial.print("response payload: ");
            Serial.println(payload);
          #else
            delay(5);
          #endif

          // Tamaño del array para almacenar los valores
          const int arraySize = 5;
          int values[arraySize];
          // Llamar a la función para dividir la cadena y obtener los valores
          splitString(payload, values, arraySize);

          if(payload == "OK"){

            String latest_relay_fw = https.header("fw-version");
            String wlp_info = https.header("wpl");
            WIFI_POOL_TIME = wlp_info.toInt();

            int fw_ver = latest_relay_fw.toInt();
            if(DEV_MODE && FIRMW_VER < fw_ver){
                #ifdef DEBUG
                  Serial.println("new FW release");
                #else
                  delay(5);
                #endif
                updateFirmware(fw_ver);
            }

          }

          https.end();
          return true;

        }
      } else {
        #ifdef DEBUG
          Serial.printf("[HTTPS] GET... failed, error: %s\n", https.errorToString(httpCode).c_str());
        #else
          delay(5);
        #endif
        https.end();
        return false;
      }

    } else {
      #ifdef DEBUG
        Serial.printf("[HTTPS] Unable to connect\n");
      #else
       delay(5);
      #endif
      return false;
    }

  } else {
    #ifdef DEBUG
      Serial.println("Failed to connect to WiFi");
    #else
       delay(5);
    #endif
  }
  return false;
}

bool SetupResetWifi(){

  //WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.mode(WIFI_AP); // explicitly set mode
  //WiFi.setTxPower(WIFI_POWER_8_5dBm);
  delay(5);

  //reset settings
  // wm.resetSettings();

  wm.setCaptivePortalEnable(true);

  std::vector<const char *> menu = {"wifi","restart","exit"};
  wm.setMenu(menu);

  // set configportal timeout 50 min
  wm.setConfigPortalTimeout(3000);
  wm.setSaveConnectTimeout(20);

  if (!wm.startConfigPortal(WIFI_NAME, WIFI_PASSW)) {

    #ifdef DEBUG
      Serial.println("failed to connect and hit timeout");
    #else
       delay(5);
    #endif

    nvs_set_i32(my_nvs_handle, "0-status", (int32_t)WIFI_SETUP);
    nvs_commit(my_nvs_handle);
    CurrentStatus = WIFI_SETUP;
    wm.resetSettings();

    delay(5);
    //reset and try again, or maybe put it to deep sleep
    esp_system_abort("restart_after_wakeup");
    delay(5);
  }

  #ifdef DEBUG
    Serial.println("connected success!");
  #else
       delay(5);
  #endif

  if(myStrlen(email) > 7){
    HttpRegDevice();
  }
  
  nvs_set_i32(my_nvs_handle, "0-status", (int32_t)WIFI);
  nvs_commit(my_nvs_handle);
  CurrentStatus = WIFI;

  delay(5);
  esp_system_abort("restart_after_wakeup");

}

bool ConnectWifi(){

    //WiFi.forceSleepWake();
    // Wake up from sleep mode (if applicable)
    //WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.mode(WIFI_STA); // explicitly set mode
    //WiFi.setTxPower(WIFI_POWER_8_5dBm);
    //wifi_station_connect();
    delay(5);

    wm.setConnectRetries(3);
    wm.setConnectTimeout(15);
    wm.setTimeout(15);

    std::vector<const char *> menu = {"wifi","restart","exit"};
    wm.setMenu(menu);

    // set configportal timeout 10 sec
    wm.setConfigPortalTimeout(7); // !IMPORTANT 4 BATTERY
    // TODO, remove preload + Setup Connections

    if (!wm.autoConnect(WIFI_NAME, WIFI_PASSW)) {
      #ifdef DEBUG
        Serial.println("failed to connect and hit timeout");
      #else
       delay(5);
    #endif
      return false;
    }

    Serial.println("WiFi Connected!");

    rssi = WiFi.RSSI();
    #ifdef DEBUG
      Serial.print("Signal strength (RSSI): ");
      Serial.print(rssi);
      Serial.println(" dBm");
    #else
       delay(5);
    #endif

    return true;
}

void load_settings(){
  // this function load the current status
  int32_t status;
  nvs_get_i32(my_nvs_handle, "0-status", &status);

  #ifdef DEBUG
    Serial.print("Loaded Status: ");
    Serial.println(status);
  #else
       delay(5);
    #endif


  if(status == 1 || status == 3){
    CurrentStatus = (ConfigStatus)status;
  }else{
    #ifdef DEBUG
      Serial.println("EEPROM DEF!");
    #else
       delay(5);
    #endif
    nvs_set_i32(my_nvs_handle, "0-status", (int32_t)WIFI_SETUP);
    nvs_commit(my_nvs_handle);
    CurrentStatus = WIFI_SETUP;
  }

  #ifdef DEBUG
    Serial.print("Status Loaded = ");
    Serial.println(CurrentStatus);
  #else
       delay(5);
    #endif

}


void updateFirmware(int new_fw_vers) {

  HTTPClient http;
  #ifdef DEBUG
    Serial.print("Conectando al servidor para descargar el firmware...");
  #else
       delay(5);
    #endif

  char urlout[255];
  sprintf(urlout, "%s/sensor%d.bin", firmwareUrl, new_fw_vers);

  if (http.begin(urlout)) {
    int httpResponseCode = http.GET();

    if (httpResponseCode == HTTP_CODE_OK) {
      #ifdef DEBUG
        Serial.println("Firmware encontrado. Iniciando actualización...");
      #else
       delay(5);
    #endif

      size_t updateSize = http.getSize();

      if (Update.begin(updateSize)) {
        WiFiClient& stream = http.getStream();

        if (Update.writeStream(stream)) {
          if (Update.end()) {
            #ifdef DEBUG
              Serial.println("Actualización exitosa. Reiniciando...");
            #else
       delay(5);
    #endif
            esp_system_abort("restart_after_wakeup");
          } else {
            #ifdef DEBUG
              Serial.println("Error al finalizar la actualización.");
            #else
       delay(5);
    #endif
          }
        } else {
          #ifdef DEBUG
            Serial.println("Error al escribir en el firmware.");
          #else
       delay(5);
    #endif
        }
      } else {
        #ifdef DEBUG
          Serial.println("Error al iniciar la actualización.");
        #else
       delay(5);
    #endif
      }
    } else {
      #ifdef DEBUG
        Serial.println("Firmware no encontrado. Código de error HTTP: " + String(httpResponseCode));
      #else
       delay(5);
    #endif
    }

    http.end();
  } else {
    #ifdef DEBUG
      Serial.println("Error al conectarse al servidor.");
    #else
       delay(5);
    #endif
  }
}

