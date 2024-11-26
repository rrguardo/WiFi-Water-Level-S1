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
// Arduino Framework version: 2.3.2
// Arduino Board Module: ESP32C3 Dev Module
// Board: ESP32-C3-MINI-1U-H4
// Board Manager URL: https://arduino.esp8266.com/stable/package_esp8266com_index.json

// Libraries used:
// WiFiManager by tzapu version: 2.0.16-rc.2


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



// #define DEBUG  // Comment this line to disable serial prints


// DEV MODE allow app update remote
const bool DEV_MODE = true;
const char* firmwareUrl = "https://waterlevel.pro/static/fw";
int FIRMW_VER = 18;

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

char* api_key =  "-";  //  PRIVATE KEY HERE // "-" none-null-api-key
// generate new key in developer zone at https://waterlevel.pro/settings   https://waterlevel.pro/add_sensor

const char* WIFI_NAME = "WaterLevelProSetup";
const char* WIFI_PASSW = "1122334455";

WiFiManager wm;
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

  #ifdef DEBUG
    Serial.println("Setup Started!");
  #else
    delay(10);
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
            delay(10);
          #endif


  } else {
    #ifdef DEBUG
      Serial.println("Done opening nvs handler");
    #else
      delay(10);
    #endif
  }

  load_private_key();

  load_settings();

  if(CurrentStatus == WIFI_SETUP){
    SetupResetWifi();
  }

  // Set the trigger pin as an output and the echo pin as an input
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // Configuramos el pin del botón como entrada
  pinMode(pinBoton, INPUT_PULLUP);
  // Asociamos la función botonPresionado() al pin del botón
  attachInterrupt(pinBoton, botonPresionado, FALLING);

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

  size_t required_size;
  nvs_get_str(my_nvs_handle, "PrivateKey", NULL, &required_size);
  if (required_size != 0) {
    char* tmp_key = (char*)malloc(required_size);
    nvs_get_str(my_nvs_handle, "PrivateKey", tmp_key, &required_size);
    api_key = tmp_key;

    #ifdef DEBUG
      Serial.print("Private Key loaded from memory: ");
      Serial.println(api_key);
    #else
       delay(10);
    #endif

  }else if(api_key != "-"){
    nvs_set_str(my_nvs_handle, "PrivateKey", api_key);
    nvs_commit(my_nvs_handle);

    #ifdef DEBUG
      Serial.print("Private Key loaded from code: ");
      Serial.println(api_key);
    #else
       delay(10);
    #endif
  }

  if(myStrlen(api_key) < 10){
    #ifdef DEBUG
      Serial.print("ERROR: Short API KEY: ");
      Serial.println(api_key);
    #else
       delay(10);
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
       delay(10);
    #endif

  if(voltaje <= LowVoltage){
    Serial.println("Under Voltage Detected");
    if(tryNum < 3){
      delay(delay_time);
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
  HttpSendInfo(last_distance, LastVoltage);

  wm.disconnect();
  WiFi.mode(WIFI_OFF);

  #ifdef DEBUG
    Serial.println("sleeping to save energy");
  #else
       delay(10);
    #endif

  SleepSave(WIFI_POOL_TIME);
  delay(100);

}


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
       delay(10);
    #endif

  return distance;
}


// Función que se ejecutará cuando el botón se presione
ICACHE_RAM_ATTR void botonPresionado() {

  if(millis() < 1000) return;

  #ifdef DEBUG
    Serial.println("El botón está presionado");
  #else
       delay(10);
    #endif

  nvs_set_i32(my_nvs_handle, "0-status", (int32_t)WIFI_SETUP);
  nvs_commit(my_nvs_handle);

  CurrentStatus = WIFI_SETUP;
  wm.resetSettings();

  delay(500);
  esp_system_abort("restart_after_wakeup");

}


void SleepSave(int seconds){
  // IMPORTANT: Connect D0(GPIO-16) to RST after programing. This will reset the device after waking up (easy wifi/device restart)
  #ifdef DEBUG
    Serial.println("Will sleep now");
  #else
       delay(10);
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



bool HttpSendInfo(int distance, float LastVoltage){

  // Check for successful connection
  if (WiFi.status() == WL_CONNECTED) {
    #ifdef DEBUG
      Serial.println("Connected to WiFi");
    #else
       delay(10);
    #endif

    long timeout = 15000;

    HTTPClient https;
    https.setTimeout(timeout);
    https.setConnectTimeout(timeout);

    #ifdef DEBUG
      Serial.print("[HTTPS] begin...\n");
    #else
       delay(10);
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
       delay(10);
    #endif

        // file found at server
        if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {

          String payload = https.getString();

          #ifdef DEBUG
            Serial.print("response payload: ");
            Serial.println(payload);
          #else
       delay(10);
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
       delay(10);
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
       delay(10);
    #endif
        https.end();
        return false;
      }

    } else {
      #ifdef DEBUG
        Serial.printf("[HTTPS] Unable to connect\n");
      #else
       delay(10);
    #endif
      return false;
    }

  } else {
    #ifdef DEBUG
      Serial.println("Failed to connect to WiFi");
    #else
       delay(10);
    #endif
  }
  return false;
}

bool SetupResetWifi(){

  //WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.mode(WIFI_AP); // explicitly set mode
  //WiFi.setTxPower(WIFI_POWER_8_5dBm);
  delay(250);

  //reset settings
  // wm.resetSettings();

  wm.setCaptivePortalEnable(true);

  std::vector<const char *> menu = {"wifi","restart","exit"};
  wm.setMenu(menu);

  // set configportal timeout 10 min
  wm.setConfigPortalTimeout(3000);

  if (!wm.startConfigPortal(WIFI_NAME, WIFI_PASSW)) {

    #ifdef DEBUG
      Serial.println("failed to connect and hit timeout");
    #else
       delay(10);
    #endif

    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    esp_system_abort("restart_after_wakeup");
    delay(5000);
  }

  #ifdef DEBUG
    Serial.println("connected success!");
  #else
       delay(10);
    #endif

  nvs_set_i32(my_nvs_handle, "0-status", (int32_t)WIFI);
  nvs_commit(my_nvs_handle);
  CurrentStatus = WIFI;

  delay(3000);
  esp_system_abort("restart_after_wakeup");

}

bool ConnectWifi(){

    //WiFi.forceSleepWake();
    // Wake up from sleep mode (if applicable)
    //WiFi.setTxPower(WIFI_POWER_8_5dBm);
    WiFi.mode(WIFI_STA); // explicitly set mode
    //WiFi.setTxPower(WIFI_POWER_8_5dBm);
    //wifi_station_connect();
    delay(250);

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
       delay(10);
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
       delay(10);
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
       delay(10);
    #endif


  if(status >= 0 && status <= 4){
    CurrentStatus = (ConfigStatus)status;
  }else{
    #ifdef DEBUG
      Serial.println("EEPROM DEF!");
    #else
       delay(10);
    #endif
    nvs_set_i32(my_nvs_handle, "0-status", (int32_t)WIFI_SETUP);
    nvs_commit(my_nvs_handle);
    CurrentStatus = WIFI_SETUP;
  }

  #ifdef DEBUG
    Serial.print("Status Loaded = ");
    Serial.println(CurrentStatus);
  #else
       delay(10);
    #endif

}


void updateFirmware(int new_fw_vers) {

  HTTPClient http;
  #ifdef DEBUG
    Serial.print("Conectando al servidor para descargar el firmware...");
  #else
       delay(10);
    #endif

  char urlout[255];
  sprintf(urlout, "%s/sensor%d.bin", firmwareUrl, new_fw_vers);

  if (http.begin(urlout)) {
    int httpResponseCode = http.GET();

    if (httpResponseCode == HTTP_CODE_OK) {
      #ifdef DEBUG
        Serial.println("Firmware encontrado. Iniciando actualización...");
      #else
       delay(10);
    #endif

      size_t updateSize = http.getSize();

      if (Update.begin(updateSize)) {
        WiFiClient& stream = http.getStream();

        if (Update.writeStream(stream)) {
          if (Update.end()) {
            #ifdef DEBUG
              Serial.println("Actualización exitosa. Reiniciando...");
            #else
       delay(10);
    #endif
            esp_system_abort("restart_after_wakeup");
          } else {
            #ifdef DEBUG
              Serial.println("Error al finalizar la actualización.");
            #else
       delay(10);
    #endif
          }
        } else {
          #ifdef DEBUG
            Serial.println("Error al escribir en el firmware.");
          #else
       delay(10);
    #endif
        }
      } else {
        #ifdef DEBUG
          Serial.println("Error al iniciar la actualización.");
        #else
       delay(10);
    #endif
      }
    } else {
      #ifdef DEBUG
        Serial.println("Firmware no encontrado. Código de error HTTP: " + String(httpResponseCode));
      #else
       delay(10);
    #endif
    }

    http.end();
  } else {
    #ifdef DEBUG
      Serial.println("Error al conectarse al servidor.");
    #else
       delay(10);
    #endif
  }
}

