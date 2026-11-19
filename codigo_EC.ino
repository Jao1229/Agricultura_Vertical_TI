#include <OneWire.h>
#include <Wire.h>
#include <DallasTemperature.h>

#define ONE_WIRE_BUS 12
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensor(&oneWire); 

#define vin 5
#define K 0.996
#define T_coeficiente 0.015

void setup()
{
  sensor.begin();
  Serial.begin(9600);
  pinMode(A0, INPUT);
}

void loop()
{
  sensor.requestTemperatures();

  float vout = analogRead(A0);   //le o medidor 

  vout = (float)vout / 1023 * 5;       //tranforma para volts

  float temperatura = sensor.getTempCByIndex(0);
  float resistencia = 1000 * (vout / (vin - vout));  //transforma de volts para resistencia

  float condutividade = 1000/(resistencia * K);           //tranfrorma para condutividade
  condutividade = condutividade * ( 1/(1 + T_coeficiente *(temperatura - 25 )));
  Serial.println(condutividade);
  Serial.println(temperatura);
  delay(1500);
}
