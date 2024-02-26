/*
NodeMCU v3 pins used:
A0 - analog voltage-ladder based keypad
D1 - I2C SCL
D2 - I2C SDA
D5 - SPI SCK
D6 - SPI MOSI
D7 - SPI MISO
D8 - SPI CS for card reader
*/

// #define DEBUG // comment-out for productions
// #define SKIPSETUP //comment-out for menu

/* Default for config */

#define STEP 60 /* seconds */
#define OFF_CURRENT 0.2 /* minimal of current's aboslute value to stop logging in auto mode */
#define OFF_DELAY 300 /* nuber of seconds before recording is disabled for OFF_CURENT */
#define MIN_6V 5.26 /* 3 cells * 1.75V */
#define MAX_6V 8.4/* 3 cells * 2.8V */
#define MAX_12V 16.8 /* 6 cells *2.8V */

/* NodeMCUv3 wiring setup */
#define KEYBOARD A0 // analog read - buttons resistor-ladder
#define SPI_CS 15 /* D8 HCS pin */
#define LCD_I2C_ADDRESS 0x27
#define INA_I2C_ADDRESS 0x40
 /* treshold ADC values for buttons */
#define ANALOG_UP 150
#define ANALOG_DOWN 450
#define ANALOG_ENTER 824

/* current shunt hardware config */
/* todo - move to config file and/or menu config */
#define SHUNT 3.75 /* mOhms, default 20A 80mV */
#define SHUNT_R_40 /* 40mV shunt range (10A max for 20A 80mV shunt), comment for 80mV */
#define V_RANGE_16 /* Volts - comment for 32V max bus voltage */


#include <Wire.h>
#include "DFRobot_INA219.h"
#include <LiquidCrystal_I2C.h>
#include <SPI.h>
#include <SD.h>
#include <ESP8266WiFi.h>
#include "Adafruit_MQTT.h"
#include "Adafruit_MQTT_Client.h"

DFRobot_INA219_IIC ina219(&Wire, INA_I2C_ADDRESS);
LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS, 20, 4); 
WiFiClient client;
Adafruit_MQTT_Client *mqtt;
Adafruit_MQTT_Publish *measurements;

enum ButtonType {NONE, UP, DOWN, ENTER};

/* globals for data measurements */
float busVoltage = 0.0;
float avgBusVoltage = 0.0;
float current = 0.0;
float avgCurrent = 0.0;
float totalPower = 0.0;
float totalCapacity = 0.0;
float shuntVoltage = 0.0;
float power = 0.0;

/* runtime configs */
bool regWiFi = false;
bool isSDOk = false;
bool reg = true; // start in registering mode
bool regSD = false;
bool continousMode = false;
bool isConfigOK = false;
bool started = false;
bool lowCurrent = false;
unsigned long lowCurrentTime = 0u;
bool isWiFiok = false;
float batteryVoltage = 0.0;
String filename = String("datalog-1.csv");
unsigned long now,last,runningTime,seconds = 0u;
int period,delta,counter = 0;

/* Network config values */
String MQTTendpoint="";
String SSID="";
String WPAKey="";
String MQTTTopic="";
int port = 1883;  //default MQTT plain text

void getBatteryVoltage(float busVoltage){
  if (batteryVoltage == 0.0){
    if (busVoltage > MIN_6V) batteryVoltage=6.0;
    if (busVoltage > MAX_6V) batteryVoltage=12.0;
    if (busVoltage > MAX_12V) batteryVoltage=24.0;
  }
}

void setup_ina219(){
  ina219.reset();
#ifdef V_RANGE_16
  ina219.setBRNG(ina219.eIna219BusVolRange_16V); /* 16V bus range */
#else
  ina219.setBRNG(ina219.eIna219BusVolRange_32V);
#endif
#ifdef SHUNT_R_40
  ina219.setPGA(ina219.eIna219PGABits_1); /* 40mV */
#else
  ina219.setPGA(ina219.eIna219PGABits_2); /* 80mV */
#endif
  ina219.setBADC(ina219.eIna219AdcBits_12, ina219.eIna219AdcSample_32); /* 12 bits ADC, 32 samples */
  ina219.setSADC(ina219.eIna219AdcBits_12, ina219.eIna219AdcSample_32); /* 12 bits ADC, 32 samples */
  ina219.setMode(ina219.eIna219SAndBVolCon); /* continous bus and shunt measurements mode */
}

float getCurrent(float shuntVoltage_mV){
  return shuntVoltage_mV / SHUNT;
}

float getPower(float measured_current, float measured_busVoltage){
  return measured_current*measured_busVoltage;
}


void readConfig(){
  File fp = SD.open("config.txt");
  if (fp){
    while (fp.available()){
      String line = fp.readStringUntil('\n');
      line.trim(); // Remove leading/trailing whitespaces
      #ifdef DEBUG
      Serial.print("Config line: ");
      Serial.println(line);
      #endif
      if (line.length() > 0){
        int equalsIndex = line.indexOf('=');
        if (equalsIndex != -1){
          String key = line.substring(0, equalsIndex);
          String value = line.substring(equalsIndex + 1);
          #ifdef DEBUG
          Serial.print("Key: ");
          Serial.println(key);
          Serial.print("Value: ");
          Serial.println(value);
          #endif
          if (key == "SSID") SSID=value;
          else if (key == "WPA2KEY") WPAKey=value;
          else if (key == "ENDPOINT") MQTTendpoint=value;
          else if (key == "PORT") port = value.toInt();
          else if (key == "TOPIC") MQTTTopic=value;
        }
      }
    }
    fp.close();
    isConfigOK = (SSID.length() > 0 && WPAKey.length() > 0 && MQTTendpoint.length() > 0 && MQTTTopic.length() > 0);
    #ifdef DEBUG
    Serial.print(F("SSID:")); Serial.println(SSID);
    Serial.print(F("WPA2KEY:")); Serial.println(WPAKey);
    Serial.print(F("ENDPOINT:")); Serial.println(MQTTendpoint);
    Serial.print(F("PORT:")); Serial.println(port);
    Serial.print(F("TOPIC:")); Serial.println(MQTTTopic);
    #endif
  }
  else{
    isConfigOK = false;
    #ifdef DEBUG
    Serial.println(F("File opening error"));
    #endif
  }
}

void discoverFileNumber()
{
  short index = 1;
  #ifdef DEBUG
  Serial.println("Filename: "+filename);
  #endif
  while (SD.exists(filename)){
    index++;
    filename = String("datalog-");
    filename.concat(String(index));
    filename.concat(String(".csv"));
    #ifdef DEBUG
      Serial.println("Filename: "+filename);
    #endif
  }
}

void saveDataToCSV(){
  File dataFile = SD.open(filename, FILE_WRITE);
  if (dataFile){
    String dataString = (String)(seconds) + "," + (String)avgBusVoltage + "," + (String)avgCurrent;
    dataFile.seek(EOF);
    dataFile.println(dataString);
    dataFile.close();
    #ifdef DEBUG
      Serial.print(F("Data record saved do SD, filename: "));
      Serial.println(filename);
      Serial.println(dataString);
    #endif
  }
  else regSD=false;
}

void measureData(){
  shuntVoltage = ina219.getShuntVoltage_mV();
  busVoltage = ina219.getBusVoltage_V();
  current = getCurrent(shuntVoltage);
  power = getPower(current, busVoltage);
  totalPower = totalPower + (power * delta / 3600000);
  if (batteryVoltage==0.0){
    // have we connected the battery?
    getBatteryVoltage(busVoltage);
  }
  totalCapacity = (batteryVoltage==0.0?0.0:totalPower/batteryVoltage);
}


#ifdef DEBUG
void reportDataViaSerial(){
  Serial.print(F("Bus Voltage:   "));
  Serial.print(avgBusVoltage, 2);
  Serial.println(F("V"));

  Serial.print(F("Shunt Voltage:   "));
  Serial.print(shuntVoltage, 2);
  Serial.println(F("mV"));

  Serial.print(F("Current:      "));
  Serial.print(avgCurrent, 2);
  Serial.println(F("A"));

  Serial.print(F("Total power:        "));
  Serial.print(totalPower, 2);
  Serial.println(F("Wh"));

  Serial.print(F("Capacity:        "));
  Serial.print(totalCapacity, 2);
  Serial.println(F("Ah"));
  Serial.println("");
}
#endif

void displaySavingMessage(){
  lcd.setCursor(0,1);
  lcd.print(F("SAVING DATA"));
}

void displayData(){
  seconds = runningTime / 1000;
  int tmpVal = 0;
  lcd.clear();
  lcd.print(F("TIME:"));
  tmpVal = (int)seconds / 3600;
  if (tmpVal<10){
    lcd.print(F("0"));
  }
  lcd.print(tmpVal);
  lcd.print(F(":"));
  tmpVal = (int)(seconds % 3600) / 60;
  if (tmpVal<10){
    lcd.print(F("0"));
  }
  lcd.print(tmpVal);
  lcd.print(F(":"));
  tmpVal = (int)(seconds % 3600 ) % 60;
  if (tmpVal<10){
    lcd.print(F("0"));
  }
  lcd.print(tmpVal);
  if (!reg) lcd.print(F(" STOP"));
  lcd.setCursor(0,1);
  lcd.print(F("MODE:"));
  lcd.print(continousMode?F("CONT"):F("AUTO"));
  lcd.print(F(" REG:"));
  if (regWiFi && isWiFiok) lcd.print(F("Wi "));
  if (regSD && isSDOk) lcd.print(F("SD "));
  lcd.setCursor (0,2);
  lcd.print(F("U="));
  lcd.print(busVoltage, 2);
  lcd.print(F("V "));
  lcd.print(F("I="));
  lcd.print(current, 2);
  lcd.print(F("A "));
  lcd.setCursor(0,3 );
  lcd.print(F("P="));
  lcd.print(power,2);
  lcd.print(F("W "));
  lcd.print(F("C="));
  lcd.print(totalCapacity, 2);
  lcd.print(F("Ah")); 
}

void saveDataViaMQTT(){
  String SC = String(avgCurrent,2);
  String SV = String(avgBusVoltage,2);
  #ifdef DEBUG
      Serial.println("MQTT value: "+SV+","+SC);
  #endif
  if (!measurements->publish((char *)(SV+","+SC).c_str())){
    MQTTDisconnect();
    #ifdef DEBUG
      Serial.println((char *)(SV+","+SC).c_str());
      Serial.print(F("MQTT topic: "));
      Serial.println(MQTTTopic);
      Serial.println(F("MQTT error!"));
    #endif
  }
  else{
    #ifdef DEBUG
    Serial.println(F("MQTT published"));
    #endif
  }
}

ButtonType readButton(){
  #ifdef SKIPSETUP
  return ENTER;
  #endif
  short voltage = analogRead(KEYBOARD);
  delay(200);
  if (voltage < ANALOG_UP) return NONE;
  if (voltage < ANALOG_DOWN) return UP;
  if (voltage < ANALOG_ENTER) return DOWN;
  return ENTER;
}

void WIFI_connect() {
  WiFi.begin(SSID, WPAKey);
  #ifdef DEBUG
    Serial.print(F("Connecting to WiFi... "));
  #endif
  uint8_t retries = 5;
  while (WiFi.status() != WL_CONNECTED) {
       #ifdef DEBUG
        Serial.println(F("Retrying WIFI connection in 5 seconds..."));
       #endif
       delay(5000);  // wait 5 seconds
       retries--;
       if (retries == 0) {
        isWiFiok = false;
        return;
       }
  }
  isWiFiok = true;
#ifdef DEBUG  
  Serial.println(F("WIFI Connected!"));
#endif
}

void MQTT_connect() {
  mqtt = new Adafruit_MQTT_Client(&client, MQTTendpoint.c_str(), port);
// anonymous access, change for authentication
  measurements = new Adafruit_MQTT_Publish(mqtt, MQTTTopic.c_str());
  // Stop if already connected.
  if (mqtt->connected()) {
    return;
  }
  #ifdef DEBUG
    Serial.print(F("Connecting to MQTT... "));
  #endif

  uint8_t retries = 5;
  while (mqtt->connect() != 0) { // connect will return 0 for connected
    #ifdef DEBUG
      Serial.println(F("Retrying MQTT connection in 5 seconds..."));
    #endif
    mqtt->disconnect();
    delay(5000);  // wait 5 seconds
    retries--;
    if (retries == 0) {
      isWiFiok = false;
      return;
    }
  }
  #ifdef DEBUG
    Serial.println("MQTT Connected!");
  #endif
}

void setMQTTReg(){
  ButtonType button;
  lcd.clear();
  button = NONE;
  lcd.print(F("Register measurement"));
  lcd.setCursor(0,1);
  lcd.print(F("via MQTT? "));
  while (button != ENTER){
    if (!isConfigOK){
      lcd.setCursor(0,2);
      lcd.print(F("WiFi config error!"));
      lcd.setCursor(0,3);
      lcd.print(F("Press Enter key"));
      regWiFi = false;
      isWiFiok = false;
    }
    else{
      lcd.setCursor(0,3);
      lcd.print(regWiFi?F("YES"):F("NO "));
    }
    button = readButton();
    if (button == UP || button == DOWN) regWiFi = !regWiFi;
  }
  if (regWiFi){
    WiFi.mode(WIFI_STA);
    lcd.clear();
    lcd.print(F("Connecting to WiFi"));
    WIFI_connect();
    lcd.setCursor(0,1);
    if (isWiFiok){
      lcd.print(F("Connecting to MQTT"));
      MQTT_connect();
    }
    lcd.setCursor(0,2);
    if (!isWiFiok){
      WiFi.mode(WIFI_OFF);
      regWiFi = false;
      lcd.print(F("Connection error!"));
      while (readButton() != ENTER){
        lcd.setCursor(0,3);
        lcd.print(F("Press Enter key")); // in loop, so watchdog does not reset the device
      }
    }
  }
  else WiFi.mode(WIFI_OFF);
}

void setSDReg(){
  ButtonType button;
  lcd.clear();
  button = NONE;
  lcd.print(F("Register measurement"));
  lcd.setCursor(0,1);
  lcd.print(F("data on SD? "));
  while (button != ENTER){
    if (!isSDOk){
      lcd.setCursor(0,2);
      lcd.print(F("SD card error!"));
      lcd.setCursor(0,3);
      lcd.print(F("Press Enter key"));
      regSD = false;
    }
    else{
      lcd.setCursor(0,3);
      lcd.print(regSD?F("YES"):F("NO "));
    }
    button = readButton();
    if (button == UP || button == DOWN) regSD = !regSD;
  }
  if (regSD) discoverFileNumber();
}

void setMode(){
  ButtonType button;
  lcd.clear();
  button = NONE;
  lcd.print(F("Set data"));
  lcd.setCursor(0,1);
  lcd.print(F("registering mode"));
  while (button != ENTER){
    lcd.setCursor(0,3);
    lcd.print(continousMode?F("CONTINOUS        "):F("CURRENT DETECTION"));
    button = readButton();
    if (button == UP || button == DOWN) continousMode = !continousMode;
  }
}

void configOK(){
  ButtonType button;
  lcd.clear();
  button = NONE;
  lcd.print(F("Is config finished?"));
  lcd.setCursor(0,1);
  lcd.print(F("'YES' starts device"));
  while (button != ENTER){
    lcd.setCursor(0,3);
    lcd.print(started?F("YES"):F("NO "));
    button = readButton();
    if (button == UP || button == DOWN) started = !started;
  }
}

void setConfig(){
  #ifdef SKIPSETUP
  started = true;
  regSD = true;
  regWiFi = true;
  #endif
  setMQTTReg();
  setSDReg();
  setMode();
  configOK();
  if (started){
    now = millis();
  }
}

void discoverLowCurrent(){
  if (abs(current) > OFF_CURRENT){
      lowCurrent = false;
      lowCurrentTime = seconds;
      reg = true;
  }
  else
  {
    if (!lowCurrent){ // first time - start counting
      lowCurrent = true;
      lowCurrentTime = seconds;
    }
    else{ // counting - check if we should stop logging
      if (seconds - lowCurrentTime > OFF_DELAY){
        reg = false;
      }
    }
  }
}

void setup(){
  delay(1000); // time for discover USB connection by PC
  #ifdef DEBUG
  Serial.begin(9600);
  #endif
  Wire.begin(D2,D1);

  isSDOk = SD.begin(SPI_CS);
  if (isSDOk){
    #ifdef DEBUG
    Serial.println(F("SD Present"));
    #endif
    readConfig();
  }
  #ifdef DEBUG
  else {
    Serial.println(F("SD not ready"));
  }
  #endif
  setup_ina219();
  lcd.begin();
}

void MQTTDisconnect(){
  regWiFi = false;
    isWiFiok = false;
    mqtt->disconnect();
    WiFi.mode(WIFI_OFF);
}

void loop()
{
  if (started){
    measureData();
    displayData();
    avgBusVoltage = avgBusVoltage + busVoltage;
    avgCurrent = avgCurrent + current;
    counter++;
    if (counter % 30 == 0){
      if(! mqtt->ping()) {
        #ifdef DEBUG
        Serial.println("MQTT ping failed!");
        #endif
        MQTTDisconnect();
      }
    }
    delay(1000);
    delta = millis()-now;
    runningTime = runningTime + delta;
    now = millis();
    period = (int)((now - last)/1000);
    if (!continousMode) discoverLowCurrent(); //not continous, check if we should stop logging
    if (period > STEP){
      displaySavingMessage();
      last = now;
      // calcuate average of measurements to log
      avgCurrent = avgCurrent / counter;
      avgBusVoltage = avgBusVoltage / counter;
      #ifdef DEBUG
      reportDataViaSerial();
      #endif
      if (reg){ // if we are in registering mode
        if (isWiFiok && regWiFi) saveDataViaMQTT();
        if (regSD) saveDataToCSV();
      }
      avgCurrent = 0.0;
      avgBusVoltage = 0.0;
      counter = 0;
    }
  }
  else{
    setConfig();
  }
}
