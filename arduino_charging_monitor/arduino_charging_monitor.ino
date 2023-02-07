#include <Wire.h>
#include "DFRobot_INA219.h"

#include <SPI.h>
#include <SD.h>

#define SHUNT 0.0012 /* Ohms */
#define SHUNT_R_40 /* 40mV shunt range, comment for 80mV */
#define V_RANGE_16 /* Volts - comment for 32V max bus voltage */
#define STEP 10 /* seconds */
#define SPI_CS D8 /* SPI CS pin */

DFRobot_INA219_IIC ina219(&Wire, INA219_I2C_ADDRESS1);
float busVoltage = 0.0;
float current = 0.0;
float totalPower = 0.0;
float totalCapacity = 0.0;
float shuntVoltage = 0.0;
float power = 0.0;
unsigned long counter = 0u;
bool isSDPresent = false;
File fp;

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
  ina219.setBADC(ina219.eIna219AdcBits_12, ina219.eIna219AdcSample_32); /* 9 bits ADC, 8 samples */
  ina219.setSADC(ina219.eIna219AdcBits_12, ina219.eIna219AdcSample_32); /* 12 bits ADC, 8 samples */
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
  //Wire.begin(D1,D2);
  isSDPresent = setup_SD();
  if (isSDPresent){
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
  if (fp){
    Serial.println("File opened");
    while (fp.available()){
      Serial.print(fp.readString()); 
    }
    fp.close();
  }
  else{
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
  Serial.print(totalPower/12.0, 2);
  Serial.println("Ah");

  Serial.println("");
}

void loop()
{
  delay(1000);
}
