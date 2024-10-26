#ifdef ESP8266 || ESP32
  #define ISR_PREFIX ICACHE_RAM_ATTR
#else
  #define ISR_PREFIX
#endif

#if !(defined(ESP_NAME))
  #define ESP_NAME "rack" 
#endif

#include <Arduino.h>

/* WiFi */
#include <ESP8266WiFi.h> // WIFI support
#include <ArduinoOTA.h> // Updates over the air
char hostname[32] = {0};

/* mDNS */
#include <ESP8266mDNS.h>

/* WiFi Manager */
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 

/* I2C */
#include <Wire.h>

/* MQTT */
#include <PubSubClient.h>
char topic[40] = {0};
WiFiClient wifiClient;
PubSubClient client(wifiClient);

/* Display (SSD1306) */
#include "SSD1306Ascii.h"
#include "SSD1306AsciiWire.h"
SSD1306AsciiWire display;
uint8_t col[2]; // Columns for counts.
uint8_t rows;   // Rows per line.

/* Port Expander (MCP23008) */
#include <Adafruit_MCP23X08.h>
Adafruit_MCP23X08 mcp;

/* Pin State */
#define PIN_COUNT 8
int pins[PIN_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7};
int pinState;
int initPinState[PIN_COUNT] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
int lastPinState[PIN_COUNT] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
int pinTrigger[PIN_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
unsigned long pinTimeout[PIN_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
unsigned long activeTimeout[PIN_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};

/* LED State */
bool stateChanged = false;
int ledState = HIGH;
unsigned long ledTimeout = 0;

/* Serial */
char text[40] = {0};

void configModeCallback (WiFiManager *myWiFiManager) {
  Serial.println(F("Config Mode"));
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());

  display.println(F("Config Mode"));
  display.println(WiFi.softAPIP());
  display.println(myWiFiManager->getConfigPortalSSID());
}

void reconnect() {
  Serial.print("MQTT Connecting");
  display.print("MQTT Connecting");
  while (!client.connected()) {
    if (client.connect(hostname)) {
      Serial.println();
      Serial.println("MQTT connected");

      display.println();
      display.println("MQTT connected");
    } else {
      Serial.print(".");
      display.print(".");
      delay(1000);
    }
  }
}

void setup() {
  /* LED */
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  /* Serial and I2C */
  Serial.begin(9600);
  Wire.begin(D1, D2); // join i2c bus with SDA=D1 and SCL=D2 of NodeMCU

  delay(1000);

  /* Display */
  display.begin(&Adafruit128x64, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x64)
  display.setFont(System5x7);
  display.setScrollMode(SCROLL_MODE_AUTO);
  display.clear();

  Serial.println(F("Setup..."));
  display.println(F("Setup..."));

  /* WiFi */
  sprintf(hostname, "%s-%06X", ESP_NAME, ESP.getChipId());
  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  if(!wifiManager.autoConnect(hostname)) {
    Serial.println(F("WiFi Connect Failed"));
    display.println(F("WiFi Connect Failed"));
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  } 

  Serial.println(hostname);
  Serial.print(F("  "));
  Serial.println(WiFi.localIP());
  Serial.print(F("  "));
  Serial.println(WiFi.macAddress());

  display.println(hostname);
  display.print(F("  "));
  display.println(WiFi.localIP());
  display.print(F("  "));
  display.println(WiFi.macAddress());

  delay(5000);

  /* OTA */
  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_SPIFFS
      type = "filesystem";
    }

    // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    Serial.println("Start updating " + type);
    display.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println(F("\nEnd"));
    display.println(F("\nEnd"));
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    display.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    display.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) { 
      Serial.println(F("Auth Failed")); 
      display.println(F("Auth Failed"));
    }
    else if (error == OTA_BEGIN_ERROR) { 
      Serial.println(F("Begin Failed")); 
      display.println(F("Begin Failed")); 
    }
    else if (error == OTA_CONNECT_ERROR) { 
      Serial.println(F("Connect Failed")); 
      display.println(F("Connect Failed"));
    }
    else if (error == OTA_RECEIVE_ERROR) { 
      Serial.println(F("Receive Failed"));
      display.println(F("Receive Failed"));
    } 
    else if (error == OTA_END_ERROR) { 
      Serial.println(F("End Failed")); 
      display.println(F("End Failed")); 
    }
  });
  ArduinoOTA.begin();

  // Handled by ArduinoOTA
  // /* mDNS */
  // while (!MDNS.begin(hostname)) {
  //   delay(1000);
  //   Serial.println(F("mDNS Responder Failed"));
  //   display.println(F("mDNS Responder Failed"));
  //   ArduinoOTA.handle();
  // }
  // Serial.println(F("mDNS Responder Started"));
  // display.println(F("mDNS Responder Started"));

  // // Wait for mDNS responder to initialize
  // delay(1000);

  /* MQTT */

  // Discover MQTT broker via mDNS
  Serial.print(F("Finding MQTT Server"));
  display.print(F("Finding MQTT Server"));
  while (MDNS.queryService("mqtt", "tcp") == 0) {
    delay(1000);
    Serial.print(F("."));
    display.print(F("."));
    ArduinoOTA.handle();
  }
  Serial.println();
  display.println();

  Serial.println(F("MQTT: "));
  Serial.print(F("  "));
  Serial.print(MDNS.IP(0));
  Serial.print(F(":"));
  Serial.println(MDNS.port(0));

  display.println(F("MQTT: "));
  display.print(F("  "));
  display.print(MDNS.IP(0));
  display.print(F(":"));
  display.println(MDNS.port(0));

  client.setServer(MDNS.IP(0), MDNS.port(0));
  client.setKeepAlive(60);

  if (!client.connected())
  {
    reconnect();
  }

  delay(5000);

  /* Pins */
  if (!mcp.begin_I2C()) {
    Serial.println("MCP I2C Error.");
    while (1);
  }

  mcp.pinMode(0, INPUT_PULLUP);
  mcp.pinMode(1, INPUT_PULLUP);
  mcp.pinMode(2, INPUT_PULLUP);
  mcp.pinMode(3, INPUT_PULLUP);
  mcp.pinMode(4, INPUT_PULLUP);
  mcp.pinMode(5, INPUT_PULLUP);
  mcp.pinMode(6, INPUT_PULLUP);
  mcp.pinMode(7, INPUT_PULLUP);

  // Set initial pin state
  for(int i = 0; i < PIN_COUNT; i++) {
    pinState = mcp.digitalRead(pins[i]);
    initPinState[i] = pinState;
    lastPinState[i] = pinState;
  }

  /* Form */
  display.setScrollMode(SCROLL_MODE_OFF);
  display.clear();

  display.println(" 1:    0    2:    0");
  display.println(" 3:    0    4:    0");
  display.println(" 5:    0    6:    0");
  display.println(" 7:    0    8:    0");

  col[0] = display.fieldWidth(strlen("00: "));
  col[1] = display.fieldWidth(strlen("00: 0000   00: "));
  rows = display.fontRows();

  /* LED */
  digitalWrite(LED_BUILTIN, HIGH);
}

void loop() {
  /* OTA */
  ArduinoOTA.handle();

  /* MQTT */
  if (!client.connected())
  {
    reconnect();
  }
  client.loop();

  /* mDNS */
  MDNS.update();

  unsigned long currentMillis = millis();

  // Pins
  stateChanged = false;
  for(int i = 0; i < PIN_COUNT; i++) {
    pinState = mcp.digitalRead(pins[i]);

    // Noise filter
    //if (pinState == HIGH) { pinTimeout[i] = currentMillis + 5000; }
    //else if (pinTimeout[i] > currentMillis) { pinState = HIGH; }

    // Toggle pin state
    if (pinState != lastPinState[i]) {
      lastPinState[i] = pinState;
      stateChanged = true;
      sprintf(topic, "%s/sensor%02d", hostname, i + 1);
      Serial.print(topic);
      
      char buffer [4];
      display.clearField(col[i%2], rows*(i/2), 4);

      if (pinState == initPinState[i]) {
        client.publish(topic, "0");

        sprintf (buffer, "%4d", pinTrigger[i]);
        display.print(buffer);
      } else {
        client.publish(topic, "1");

        pinTrigger[i]++;
        sprintf (buffer, "%4d", pinTrigger[i]);
        display.setInvertMode(true);
        display.print(buffer);
        display.setInvertMode(false);
      }

      if (pinState == HIGH) {
        Serial.println(F(" HIGH"));
      } else {
        Serial.println(F(" LOW"));
      }
    }
  }

  // LED
  if (stateChanged == true)
  {
    ledTimeout = currentMillis + 10;
    if (ledState == HIGH) {
      digitalWrite(LED_BUILTIN, LOW);
      ledState = LOW;
    }
  } 
  else if (ledTimeout < currentMillis && ledState == LOW) 
  {
    digitalWrite(LED_BUILTIN, HIGH);
    ledState = HIGH;
  }
}