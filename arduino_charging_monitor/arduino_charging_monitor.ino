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

#define DEBUG // comment-out for productions

/* Default for config */

#define SHUNT 3.75 /* mOhms, default 20A 80mV */
// #define SHUNT_R_40 /* 40mV shunt range, comment for 80mV */
#define V_RANGE_16 /* Volts - comment for 32V max bus voltage */
#define STEP 60 /* seconds */
#define OFF_CURRENT 0.2 /* minimal of current's aboslute value to stop logging auto mode */
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
float current = 0.0;
float totalPower = 0.0;
float totalCapacity = 0.0;
float shuntVoltage = 0.0;
float power = 0.0;
unsigned long counter = 0u;

/* runtime configs */
bool regWiFi = false;
bool isSDOk = false;
bool reg = true; // start in registering mode
bool regSD = false;
bool continousMode = false;
bool isConfigOK = false;
bool started = false;
bool lowCurrent = false;
unsigned short lowCurrentTime = 0u;
bool isWiFiok = false;
float batteryVoltage = 0.0;

/* Network config values */
char *MQTTendpoint = (char *)"";
char *SSID = (char *)"";
char *WPAKey = (char *)"";
char *MQTTTopic = (char *)"";
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
  char bufChrPtr[512];
  char *strings[66]; //64 bytes + 2
  char *ptr = NULL;
  char *ptr2 = NULL;
  String buf = "";
  unsigned short index = 0;
  if (fp){
    #ifdef DEBUG
    Serial.println("File opened"); 
    #endif
    while (fp.available()){
      buf.concat(fp.readString());
    }
    fp.close();
    buf.toCharArray(bufChrPtr, 512);
    ptr = strtok(bufChrPtr, "\r\n");
    while(ptr != NULL)
    {
        //split lines
        if (strlen(ptr)>0){
          strings[index] = ptr;
          index++;
          ptr = strtok(NULL, "\r\n");
        }
    }
    // tokenize with =
    for (unsigned short i=0; i<index; i++){
      ptr = strtok(strings[i], (char *)"=");
      // first token - param name
      while(ptr != NULL)
      {
        // string will be empty for windows or mac line ending
        if (strlen(ptr)>0){
            // get next token - param value
           ptr2 = strtok(NULL,  (char *)"=");
          if (strcmp("SSID",ptr)==0) SSID=ptr2;
          if (strcmp("WPA2KEY",ptr)==0) WPAKey=ptr2;
          if (strcmp("ENDPOINT",ptr)==0) MQTTendpoint=ptr2;
          if (strcmp("PORT",ptr)==0) port=(int)ptr2;
          if (strcmp("TOPIC",ptr)==0) MQTTTopic=ptr2;
          // get next token
          ptr = strtok(NULL,  (char *)"=");
        }
      }
    }
    if (strlen(SSID)==0 || strlen(WPAKey)==0 || strlen(MQTTendpoint)==0 || strlen(MQTTTopic)==0)
      isConfigOK = false;
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



void saveDataToCSV(){
  File dataFile = SD.open("datalog.txt", FILE_WRITE);
  if (dataFile){
    String dataString = (String)(counter * STEP) + "," + (String)busVoltage + "," + (String)current + ";";
    dataFile.seek(EOF);
    dataFile.println(dataString);
    dataFile.close();
    #ifdef DEBUG
      Serial.println(F("Data record saved do SD"));
    #endif
  }
  else regSD=false;
}

void measureData(){
  shuntVoltage = ina219.getShuntVoltage_mV();
  busVoltage = ina219.getBusVoltage_V();
  current = getCurrent(shuntVoltage);
  power = getPower(current, busVoltage);
  totalPower = totalPower + (power * STEP / 3600);
  if (batteryVoltage==0.0){
    // have we connected the battery?
    getBatteryVoltage(busVoltage);
  }
  totalCapacity = (batteryVoltage==0.0?0.0:totalPower/batteryVoltage);
  counter++;
}


#ifdef DEBUG
void reportDataViaSerial(){
  Serial.print(F("Bus Voltage:   "));
  Serial.print(busVoltage, 2);
  Serial.println(F("V"));

  Serial.print(F("Shunt Voltage:   "));
  Serial.print(shuntVoltage, 2);
  Serial.println(F("mV"));

  Serial.print(F("Current:      "));
  Serial.print(current, 2);
  Serial.println(F("A"));
  Serial.print(F("Power:        "));
  Serial.print(power, 2);
  Serial.println(F("W"));

  Serial.print(F("Total power:        "));
  Serial.print(totalPower, 2);
  Serial.println(F("Wh"));

  Serial.print(F("Capacity:        "));
  Serial.print(totalCapacity, 2);
  Serial.println(F("Ah"));
  Serial.println("");
}
#endif

void displayData(){
  lcd.clear();
  lcd.print(F("TIME:"));
  lcd.print((int)(counter * STEP / 3600));
  lcd.print(F(":"));
  lcd.print((int)(counter * STEP % 3600 / 60));
  if (!reg) lcd.print(F(" STOP"));
  lcd.setCursor(0,1);
  lcd.print(F("MODE:"));
  lcd.print(continousMode?F("CONT"):F("AUTO"));
  lcd.print(F(" REG:"));
  if (regWiFi && isWiFiok) lcd.print(F("WiFi "));
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
  lcd.print(power,1);
  lcd.print(F("W "));
  lcd.print(F("C="));
  lcd.print(totalCapacity, 1);
  lcd.print(F("Ah")); 
}

void saveDataViaMQTT(){
  if (!measurements->publish(busVoltage,current)){
    regWiFi = false;
    isWiFiok = false;
  }
}

ButtonType readButton(){
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
  mqtt = new Adafruit_MQTT_Client(&client, MQTTendpoint, port);
// anonymous access, change for authentication
  measurements = new Adafruit_MQTT_Publish(mqtt, MQTTTopic);
  // Stop if already connected.
  if (mqtt->connected()) {
    return;
  }
  #ifdef DEBUG
    Serial.print(F("Connecting to MQTT... "));
  #endif

  uint8_t retries = 2;
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
    lcd.clear();
    lcd.print(F("Connecting to WiFi"));
    WIFI_connect();
    lcd.setCursor(0,1);
    lcd.print(F("Connecting to MQTT"));
    MQTT_connect();
    lcd.setCursor(0,2);
    if (!isWiFiok){
      regWiFi = false;
      lcd.print(F("Connection error!"));
      lcd.setCursor(0,3);
      while (readButton() != ENTER){
        lcd.print(F("Press Enter key")); // in loop, so watchdog does not reset the device
      }
    }
  }
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
  setMQTTReg();
  setSDReg();
  setMode();
  configOK();
  if (started){
    lcd.clear();
    lcd.print(F("Measurements started"));
    lcd.setCursor(0,2);
    lcd.print(F("Please wait"));
    lcd.setCursor(0,3);
    lcd.print(F("for first data."));
  }
}

void discoverLowCurrent(){
  if (current > OFF_CURRENT){
      lowCurrent = false;
      lowCurrentTime = 0;
      reg = true;
  }
  else
  {
    if (!lowCurrent){ // first time - start counting
      lowCurrent = true;
      lowCurrentTime = 0;
    }
    else{ // counting - check if we should stop logging
      lowCurrentTime++;
      if (lowCurrentTime * STEP > OFF_DELAY){
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

void loop()
{
  if (started){
    lcd.noBacklight();  //just to know, that the file is opended
    measureData();
    displayData();
    #ifdef DEBUG
    reportDataViaSerial();
    #endif
    if (!continousMode) discoverLowCurrent(); //not continous, check if we should stop logging
    if (reg){ // if we are in registering mode
      if (isWiFiok && regWiFi) saveDataViaMQTT();
      if (regSD) saveDataToCSV();
    }
    lcd.backlight();  //you can safely turn the device off
    delay(STEP * 1000);
  }
  else{
    setConfig();
  }
}
