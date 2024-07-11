#ifdef ESP8266 || ESP32
  #define ISR_PREFIX ICACHE_RAM_ATTR
#else
  #define ISR_PREFIX
#endif

#if !(defined(ESP_NAME))
  #define ESP_NAME "midi" 
#endif

#include <Arduino.h>

// WiFi
#include <ESP8266WiFi.h>
char hostname[32] = {0};

// WiFi Manager
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h> 

// I2C
#include <Wire.h>

// MIDI
#include <MIDI.h>
#include <AppleMIDI.h>
#include <WiFiClient.h>
#include <WiFiUdp.h>
APPLEMIDI_CREATE_DEFAULTSESSION_INSTANCE();
int8_t isConnected = 0;

// Display (SSD1306)
#include "SSD1306Ascii.h"
#include "SSD1306AsciiWire.h"
SSD1306AsciiWire oled;
uint8_t col[2]; // Columns for counts.
uint8_t rows;   // Rows per line.

// Port Expander (MCP23008)
#include <Adafruit_MCP23X08.h>
Adafruit_MCP23X08 mcp;

/* Pin State */
#define PIN_COUNT 8
int pins[PIN_COUNT] = {0, 1, 2, 3, 4, 5, 6, 7};
int pinState;
int lastPinState[PIN_COUNT] = {HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH, HIGH};
unsigned long pinTimeout[PIN_COUNT] = {0, 0, 0, 0, 0, 0, 0, 0};
unsigned long activeTimeout[PIN_COUNT] = {0, 0, 0, 0, 0, 0, 0};

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

  oled.println(F("Config Mode"));
  oled.println(WiFi.softAPIP());
  oled.println(myWiFiManager->getConfigPortalSSID());
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
  oled.begin(&Adafruit128x64, 0x3C);  // initialize with the I2C addr 0x3C (for the 128x64)
  oled.setFont(System5x7);
  oled.setScrollMode(SCROLL_MODE_AUTO);
  oled.clear();

  oled.println(F("Setup..."));

  /* WiFi */
  sprintf(hostname, "%s-%06X", ESP_NAME, ESP.getChipId());
  WiFiManager wifiManager;
  wifiManager.setAPCallback(configModeCallback);
  if(!wifiManager.autoConnect(hostname)) {
    Serial.println("WiFi Connect Failed");
    oled.println(F("WiFi Connect Failed"));
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(1000);
  } 

  Serial.println(hostname);
  Serial.print(F("  "));
  Serial.print(WiFi.localIP());
  Serial.print(F("  "));
  Serial.println(WiFi.macAddress());

  oled.println(hostname);
  oled.print(F("  "));
  oled.print(WiFi.localIP());
  oled.print(F("  "));
  oled.println(WiFi.macAddress());

  /* Apple MIDI */
  Serial.println(F("AppleMIDI:"));
  Serial.print(F("  name: "));
  Serial.println(AppleMIDI.getName());
  Serial.print(F("  port: "));
  Serial.println(AppleMIDI.getPort());

  oled.println(F("AppleMIDI:"));
  oled.print(F("  name: "));
  oled.println(AppleMIDI.getName());
  oled.print(F("  port: "));
  oled.println(AppleMIDI.getPort());

  MIDI.begin();

  AppleMIDI.setHandleConnected([](const APPLEMIDI_NAMESPACE::ssrc_t & ssrc, const char* name) {
    isConnected++;
    AM_DBG(F("Connected to session"), ssrc, name);
    Serial.println(F("Connected:"));
    Serial.print(F("  "));
    Serial.println(ssrc);
    Serial.print(F("  "));
    Serial.println(name);
  });
  AppleMIDI.setHandleDisconnected([](const APPLEMIDI_NAMESPACE::ssrc_t & ssrc) {
    isConnected--;
    Serial.println(F("Disconnected:"));
    Serial.print(F("  "));
    Serial.println(ssrc);
  });

  /* Port Expander (MCP23008) */
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
    if (pinState != lastPinState[i]) {
      lastPinState[i] = pinState;
    }
  }

  /* LED */
  digitalWrite(LED_BUILTIN, HIGH);

  oled.setScrollMode(SCROLL_MODE_OFF);
  oled.clear();

  /* Form */
  oled.println("01: 9999   02: 9999");
  oled.println("03: 9999   04: 9999");
  oled.println("05: 9999   06: 9999");
  oled.println("07: 9999   08: 9999");

  col[0] = oled.fieldWidth(strlen("01: "));
  col[1] = oled.fieldWidth(strlen("01: 9999   02: "));
  rows = oled.fontRows();

  /* MIDI */
  MIDI.begin(1);
}

void loop() {
  MIDI.read();

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
      sprintf(text, "%d", i + 1);
      Serial.print(text);
      oled.clearField(col[i%2], rows*(i/2), 4);    
      if (pinState == HIGH) {
        Serial.println(F(" HIGH"));
        oled.print(F("HIGH"));
        if (isConnected > 0) {
          MIDI.sendNoteOn(1 + i, 64, 1);
        }
      } else {
        Serial.println(F(" LOW"));
        oled.print(F("LOW"));
        if (isConnected > 0) {
          MIDI.sendNoteOff(1 + i, 64, 1);
        }
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