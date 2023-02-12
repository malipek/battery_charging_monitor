#include <Wire.h>
#include "DFRobot_INA219.h"
#include "LiquidCrystal_I2C.h" //https://github.com/fmalpartida/New-LiquidCrystal

#include <SPI.h>
#include <SD.h>

#define SHUNT 0.0012 /* Ohms */
#define SHUNT_R_40 /* 40mV shunt range, comment for 80mV */
#define V_RANGE_16 /* Volts - comment for 32V max bus voltage */
#define STEP 300 /* seconds */
#define SPI_CS 15 /* D8 HCS pin */
#define LCD_I2C_ADDRESS 0x27
#define INA_I2C_ADDRESS 0x40
#define OFF_CURRENT 0.5 /* minimal of current's aboslute value to stop logging second mode
#define OFF_DELAY 300 /* nuber of seconds befor OFF_CURRENT is checked */
#define MAX_URL_LENGTH 100
#define MIN_6V 5.26 /* 3 cells * 1.75V) */
#define MAX_6V 8.4/* 3 cells * 2.8V) */
#define MAX_12V 16.8 /* 6 cells *2.8V) */


DFRobot_INA219_IIC ina219(&Wire, INA_I2C_ADDRESS);

LiquidCrystal_I2C lcd(LCD_I2C_ADDRESS,16,2);


float busVoltage = 0.0;
float current = 0.0;
float totalPower = 0.0;
float totalCapacity = 0.0;
float shuntVoltage = 0.0;
float power = 0.0;
unsigned long counter = 0u;
bool isSDOk = false;
File fp;
bool continousMode = false;
bool isConfigOK = false;

bool isWiFiok = false;
unsigned long runningTime = 0;
char *url = (char *)"";
char *SSID = (char *)"";
char *WPAKey = (char *)"";
char *SSLFingerpring = (char *)""; /* URL's SSL public key fingerprint */
float batteryVoltage = 0.0;

void getBatteryVoltage(float busVoltage){
  if (batteryVoltage == 0.0){
    if (busVoltage > MIN_6V) batteryVoltage=6.0;
    if (busVoltage > MAX_6V) batteryVoltage=12.0;
    if (busVoltage > MAX_12V) batteryVoltage=24.0;
  }
}

void I2CScanner(){
  byte error, address;
  int nDevices;
  Serial.println("Scanning...");

  nDevices = 0;
  for(address = 1; address < 254; address++ )
  {
    delay(100);
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0)
    {
      Serial.print("I2C device found at address 0x");
      if (address < 16)
        Serial.print("0");

      Serial.print(address,HEX);
      Serial.println("  !");

      nDevices++;
    }
    else if (error==4)
    {
      Serial.print("Unknown error at address 0x");
      if (address < 16)
        Serial.print("0");

      Serial.println(address,HEX);
    }
    else{
      Serial.print("No device at address 0x");
      if (address < 16)
        Serial.print("0");

      Serial.println(address,HEX);
    }
  }

  if (nDevices == 0)
    Serial.println("No I2C devices found");
  else
    Serial.println("done");
}


void setup_LCD(){
  lcd.begin(); 
  lcd.setCursor(0, 0);
  lcd.print("Czesc!");
  lcd.setCursor(0,1);
  lcd.print("Test kontrastu!");
}

bool setup_SD(){
  return SD.begin(SPI_CS);
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
  return shuntVoltage_mV * SHUNT * 1000.0;
}

float getPower(float current, float busVoltage){
  return current*busVoltage;
}

void setup(){
  Serial.begin(9600);
//  Wire.begin(D1,D2);
//  I2CScanner();
//  setup_LCD();

  isSDOk = setup_SD();
  if (isSDOk){
    Serial.println("SD Present");
    readConfig();
  }
  else {
    Serial.println("SD not ready");
  }
  // setup_ina219();
  
}

void readConfig(){
  fp = SD.open("config.txt");
  char bufChrPtr[512];
  char *strings[MAX_URL_LENGTH+2];
  char *ptr = NULL;
  char *ptr2 = NULL;
  String buf="";
  unsigned short index = 0;
  if (fp){
    Serial.println("File opened"); 
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
          if (strcmp("ENDPOINT",ptr)==0) url=ptr2;
          if (strcmp("CONTINOUS",ptr)==0) continousMode=(bool)ptr2;
          if (strcmp("CERT_FINGERPRINT",ptr)==0) SSLFingerpring=ptr2;
          // get next token
          ptr = strtok(NULL,  (char *)"=");
        }
      }
    }
    // check if there's a fingerprint for https certificate
    if (strlent(url)>0 && strlen(SSLFingerpring)==0) isConfigOK=false;
    else isConfigOK = true;
    Serial.print("SSID:"); Serial.println(SSID);
    Serial.print("WPA2KEY:"); Serial.println(WPAKey);
    Serial.print("ENDPOINT:"); Serial.println(url);
    Serial.print("CONTINOUS:"); Serial.println(continousMode?"true":"false");
    Serial.print("CERT_FINGERPRINT:"); Serial.println(SSLFingerpring);
  }
  else{
    isConfigOK = false;
    Serial.println("File opening error");
  }
}




void reporDataViaSerial(){
  counter++;
  delay(STEP * 1000);
  shuntVoltage = ina219.getShuntVoltage_mV();
  busVoltage = ina219.getBusVoltage_V();
  current = getCurrent(shuntVoltage);
  power = getPower(current, busVoltage);
  totalPower = totalPower + (power * STEP / 3600);
  Serial.print("Bus Voltage:   ");
  Serial.print(busVoltage, 2);
  Serial.println("V");

  Serial.print("Shunt Voltage:   ");
  Serial.print(shuntVoltage, 1);
  Serial.println("mV");

  Serial.print("Current:      ");
  Serial.print(current, 1);
  Serial.println("A");

  Serial.print("Power:        ");
  Serial.print(power, 1);
  Serial.println("W");

  Serial.print("Total power:        ");
  Serial.print(totalPower, 1);
  Serial.println("Wh");

  Serial.print("Capacity:        ");
  Serial.print((batteryVoltage==0.0?0.0:totalPower/batteryVoltage), 2);
  Serial.println("Ah");

  Serial.println("");
}

void loop()
{
  delay(1000);
}
