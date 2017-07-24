#include <ESP8266WiFi.h>
#include <WiFiUdp.h>
#include "SSD1306.h"
#include <ArduinoJson.h>
#include "FS.h"

#define CONN_LED D0
#define SCL D1
#define SDA D2
#define PWM_RED D5
#define PWM_GREEN D6
#define PWM_BLUE D7
#define DMX_pin D4

const char fileName[] = "/wlan.json";
String fileBuffer = "";
StaticJsonBuffer<200> jsonBuffer;

SSD1306 display(0x3c, SDA, SCL);

unsigned long l_blinktime = 0;
unsigned long l_displayTime = 0;
unsigned long l_fpsTime = 0;
int i_fpsCounter = 0;
int i_fps = 0;

WiFiUDP artnet;
int i_port = 6454;
int i_packetSize = 0;
unsigned char data[572] = {};
unsigned char dmx[513] = {};
boolean b_connected = false;
boolean b_ledState = false;

int i_dmxLength = 512;
int pwm[3] = {1023, 1023, 1023};
int i_pwmAddress = 172;


void setup() {
  // put your setup code here, to run once: 
  pinMode(DMX_pin, OUTPUT);
  pinMode(CONN_LED, OUTPUT);
  digitalWrite(CONN_LED, HIGH);
  pinMode(PWM_RED, OUTPUT);
  analogWrite(PWM_RED, pwm[0]);
  pinMode(PWM_GREEN, OUTPUT);
  analogWrite(PWM_GREEN, pwm[1]);
  pinMode(PWM_BLUE, OUTPUT);
  analogWrite(PWM_BLUE, pwm[2]);
  //Serial.begin(256000);
  //Serial.println();
  //Serial.println("---startup---");
  display.init();
  display.clear();
  display.flipScreenVertically();
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);

  if (SPIFFS.begin()) {
    if (SPIFFS.exists(fileName))
    {
        File f = SPIFFS.open(fileName, "r");
        fileBuffer = f.readString();
        f.close();
    }
  }
  JsonObject& root = jsonBuffer.parseObject(fileBuffer.c_str());
  JsonArray& json_ssid = root["ssid"].asArray();
  JsonArray& json_wpa = root["wpa"].asArray();
  int wlanCount = 0;
  if (json_ssid.size() <= json_wpa.size()) {
    wlanCount = json_ssid.size();
  }
  else {
    wlanCount = json_wpa.size();
  }
  
  String s_connecting = "";
  for (int i=0; i<wlanCount; i++) {
    unsigned long l_connStart = millis();
    unsigned long l_connTime = 0;
    WiFi.begin(json_ssid.get<String>(i).c_str(), json_wpa.get<String>(i).c_str());
    while (WiFi.status() != WL_CONNECTED) {
      l_connTime = millis() - l_connStart;
      if (l_connTime > 10000) break;
      display.clear();
      display.setTextAlignment(TEXT_ALIGN_CENTER);
      display.setFont(ArialMT_Plain_10);
      display.drawStringMaxWidth(64, 0, 128, "connecting to");
      display.setFont(ArialMT_Plain_16);
      display.drawStringMaxWidth(64, 10, 128, json_ssid.get<String>(i));
      display.drawProgressBar(0, 30, 120, 10, l_connTime / 100);
      display.display();
      delay(100);
    }
    b_connected = (WiFi.status() == WL_CONNECTED);
    if (b_connected) break;
  }
  if (b_connected) {
    display.clear();
    setDisplayHeader();
    display.display();
    artnet.begin(i_port);
  }
  else {
    display.clear();
    display.drawString(0, 0, "Error: could not connect to any network");
    display.display();
  }
}

void loop() {
  // put your main code here, to run repeatedly:
  fpsCounter();
  b_ledState = (millis() > l_blinktime + 100) && b_connected;
  i_packetSize = artnet.parsePacket();
  if (i_packetSize > 0) {
    if (b_ledState) l_blinktime = millis();
    //Serial.printf("Received %d bytes from %s\n", i_packetSize, artnet.remoteIP().toString().c_str());
    memset(data, 0, sizeof(data));
    int len = artnet.read(data, 572);
    if (len > 17) {
      char s_headerID[8];
      for (int i=0; i<8; i++) {
        s_headerID[i] = data[i];
      }
      if (String(s_headerID) == "Art-Net") {
        //Serial.println("Art-Net header ID found");
        i_dmxLength = data[16];
        i_dmxLength = i_dmxLength << 8;
        i_dmxLength |= data[17];
        //Serial.printf("DMX data length: %d\n", i_dmxLength);
        if (i_dmxLength > 512) i_dmxLength = 512;
        if (len > 17 + i_dmxLength) {
          for (int i=1;i<=i_dmxLength;i++) {
            dmx[i] = data[17 + i];
          }
          PWMout(i_pwmAddress);
        }
      }
      /*else {
        Serial.println("Error: Art-Net header ID not found");
      }*/
    }
  }
  digitalWrite(CONN_LED, !b_ledState);
  DMXout(i_dmxLength);
  refreshDisplay();
}


void refreshDisplay() {
  if (millis() > l_displayTime + 250) {
    display.clear();
    setDisplayHeader();
    setDisplayPWM();
    display.display();
    l_displayTime = millis();
  }
}


void setDisplayHeader() {
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setFont(ArialMT_Plain_10);
    display.drawString(0, 0, "connected to: " + WiFi.SSID());
    display.drawString(0, 10, "IP address: " + WiFi.localIP().toString());
}


void setDisplayPWM() {
  display.setTextAlignment(TEXT_ALIGN_LEFT);
  display.setFont(ArialMT_Plain_10);
  display.drawString(0, 20, "Frames per second: " + String(i_fps));
  display.drawString(0, 30, "PWM address: " + String (i_pwmAddress));
  display.drawString(0, 40, "input: " + String(dmx[i_pwmAddress+0]) + " " + String(dmx[i_pwmAddress+1]) + " " + String(dmx[i_pwmAddress+2]) + " " + String(dmx[i_pwmAddress+3]));
  display.drawString(0, 50, "output: " + String(pwm[0]) + " " + String(pwm[1]) + " " + String(pwm[2]) + " ");
}


void PWMout(int startAddress) {
  pwm[0] = 1023 - (int)(1023 * (dmx[startAddress+0] / (float)255) * (dmx[startAddress+3] / (float)255));
  pwm[1] = 1023 - (int)(1023 * (dmx[startAddress+1] / (float)255) * (dmx[startAddress+3] / (float)255));
  pwm[2] = 1023 - (int)(1023 * (dmx[startAddress+2] / (float)255) * (dmx[startAddress+3] / (float)255));
  analogWrite(PWM_RED, pwm[0]);
  analogWrite(PWM_GREEN, pwm[1]);
  analogWrite(PWM_BLUE, pwm[2]);
}


void DMXout(int DMXsize) {
  //send break
  pinMode(DMX_pin, OUTPUT);
  digitalWrite(DMX_pin, LOW);
  delayMicroseconds(88);
  digitalWrite(DMX_pin, HIGH);
  //send data
  Serial1.begin(250000, SERIAL_8N2);
  Serial1.write(dmx, DMXsize + 1);
  Serial1.flush();
  delay(1);
  Serial1.end();
}


void fpsCounter() {
  if ((i_fpsCounter > 0) && (millis() > l_fpsTime + 1000)) {
    i_fps = i_fpsCounter;
    l_fpsTime = millis();
    i_fpsCounter = 0;
  }
  i_fpsCounter++;
}

