#include <OneWire.h>
#include <DallasTemperature.h>

// ---------------- PINOS DO ESP32 ----------------
#define ONE_WIRE_BUS 33   // DS18B20 no D33 / GPIO 33
#define EC_PIN_1 34       // Primeira saida do circuito EC
#define EC_PIN_2 35       // Segunda saida do circuito EC

// ---------------- PARAMETROS DO CIRCUITO ----------------
const float VIN_EC = 5.0;       // Tensao que alimenta o divisor/resistores
const float RESISTOR_REFERENCIA = 1000.0; // Resistor de 1 kOhm
const float K = 0.996;
const float T_COEFICIENTE = 0.015;

OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensor(&oneWire);

// Le a tensao de um pino e calcula a condutividade pela
// mesma formula utilizada no codigo original.
bool calcularEC(int pino, float &tensao, float &resistencia, float &ec) {
  tensao = analogReadMilliVolts(pino) / 1000.0;

  // A leitura precisa estar entre 0 V e a tensao de alimentacao.
  if (tensao <= 0.001 || tensao >= VIN_EC) {
    return false;
  }

  resistencia = RESISTOR_REFERENCIA * (tensao / (VIN_EC - tensao));
  ec = 1000.0 / (resistencia * K);
  return true;
}

void setup() {
  Serial.begin(115200);

  // ADC do ESP32
  analogReadResolution(12); // 0 a 4095
  analogSetPinAttenuation(EC_PIN_1, ADC_11db);
  analogSetPinAttenuation(EC_PIN_2, ADC_11db);

  sensor.begin();

  Serial.println();
  Serial.println("Medidor EC com ESP32");
  Serial.println("DS18B20: GPIO 33 | EC: GPIO 34 e GPIO 35");
}

void loop() {
  sensor.requestTemperatures();
  float temperatura = sensor.getTempCByIndex(0);

  if (temperatura == DEVICE_DISCONNECTED_C) {
    Serial.println("ERRO: DS18B20 nao encontrado. Verifique o fio no GPIO 33.");
    delay(1500);
    return;
  }

  float tensao1, resistencia1, ec1;
  float tensao2, resistencia2, ec2;

  bool leitura1 = calcularEC(EC_PIN_1, tensao1, resistencia1, ec1);
  bool leitura2 = calcularEC(EC_PIN_2, tensao2, resistencia2, ec2);

  // Compensacao de temperatura para 25 graus Celsius
  float fatorTemperatura =
      1.0 / (1.0 + T_COEFICIENTE * (temperatura - 25.0));

  Serial.println("--------------------------------");

  Serial.print("Temperatura: ");
  Serial.print(temperatura, 2);
  Serial.println(" C");

  if (leitura1) {
    ec1 *= fatorTemperatura;

    Serial.println("Leitura EC no GPIO 34:");
    Serial.print("Tensao: ");
    Serial.print(tensao1, 3);
    Serial.println(" V");
    Serial.print("Resistencia calculada: ");
    Serial.print(resistencia1, 2);
    Serial.println(" ohms");
    Serial.print("Condutividade: ");
    Serial.println(ec1, 4);
  } else {
    Serial.println("GPIO 34: tensao fora da faixa valida.");
  }

  if (leitura2) {
    ec2 *= fatorTemperatura;

    Serial.println("Leitura EC no GPIO 35:");
    Serial.print("Tensao: ");
    Serial.print(tensao2, 3);
    Serial.println(" V");
    Serial.print("Resistencia calculada: ");
    Serial.print(resistencia2, 2);
    Serial.println(" ohms");
    Serial.print("Condutividade: ");
    Serial.println(ec2, 4);
  } else {
    Serial.println("GPIO 35: tensao fora da faixa valida.");
  }

  delay(1500);
}
