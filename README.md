# Medidor de Condutividade Elétrica (EC)

Projeto desenvolvido com Arduino/ESP32 para medir a condutividade
elétrica (EC) de uma solução e acompanhar a temperatura por meio de um
sensor DS18B20.

O repositório contém duas versões do código: uma versão inicial, que
utiliza uma entrada analógica `A0`, e uma versão adaptada para ESP32,
que realiza duas leituras de EC em entradas diferentes.

## Arquivos do projeto

  -----------------------------------------------------------------------
  Arquivo                             Descrição
  ----------------------------------- -----------------------------------
  `codigo_EC.ino`                     Versão inicial do medidor, com
                                      leitura analógica em `A0` e sensor
                                      de temperatura no pino 12.

  `codigo_EC_ESP32_D33_D34_D35.ino`   Versão para ESP32, com DS18B20 no
                                      GPIO 33 e duas entradas para
                                      leitura de EC: GPIO 34 e GPIO 35.
  -----------------------------------------------------------------------

## Componentes e recursos

-   Placa compatível com o código utilizado (versão inicial) ou ESP32
    (versão adaptada).
-   Sensor de temperatura DS18B20.
-   Circuito de medição de condutividade elétrica.
-   Resistor de referência de 1 kΩ.
-   Bibliotecas `OneWire` e `DallasTemperature`.
-   Monitor Serial para acompanhar os resultados.

## Ligações utilizadas

### Versão inicial --- `codigo_EC.ino`

  Componente               Pino
  ------------------------ ---------
  Sensor DS18B20           Pino 12
  Leitura do circuito EC   `A0`

A versão inicial configura a entrada `A0` como entrada analógica e
utiliza comunicação serial em 9600 baud.

### Versão ESP32 --- `codigo_EC_ESP32_D33_D34_D35.ino`

  Componente            Pino do ESP32
  --------------------- ---------------
  Sensor DS18B20        GPIO 33
  Primeira leitura EC   GPIO 34
  Segunda leitura EC    GPIO 35

A versão ESP32 configura a resolução do conversor analógico para 12 bits
e utiliza o Monitor Serial em 115200 baud.

**Atenção:** confirme que a tensão aplicada às entradas analógicas do
ESP32 está dentro dos limites permitidos pela placa. Não aplique 5 V
diretamente aos GPIOs do ESP32.

## Como o código funciona

1.  Inicializa o sensor DS18B20 e a comunicação serial.
2.  Solicita uma nova leitura de temperatura.
3.  Lê a tensão na entrada analógica do circuito de EC.
4.  Calcula a resistência a partir da tensão medida e do resistor de
    referência de 1 kΩ.
5.  Calcula a condutividade elétrica usando o fator `K`.
6.  Aplica a compensação de temperatura para referência de 25 °C,
    utilizando o coeficiente definido no código.
7.  Exibe os resultados no Monitor Serial e repete o processo após 1,5
    segundo.

Na versão ESP32, o cálculo é realizado separadamente para as entradas
GPIO 34 e GPIO 35. Se a leitura de temperatura falhar, o programa
informa o erro e tenta novamente no próximo ciclo.

## Parâmetros utilizados

Os dois códigos utilizam os seguintes parâmetros:

  Parâmetro                                       Valor
  ------------------------------------------ ----------
  Resistor de referência                        1.000 Ω
  Fator `K`                                       0,996
  Coeficiente de temperatura                      0,015
  Temperatura de referência da compensação        25 °C
  Intervalo entre leituras                     1.500 ms

A versão ESP32 também define `VIN_EC` como 5,0 V e utiliza
`analogReadMilliVolts()` para obter a tensão medida em milivolts.

## Bibliotecas necessárias

Instale no Arduino IDE:

-   **OneWire**
-   **DallasTemperature**

A biblioteca `Wire` também é incluída no código inicial, embora não seja
utilizada diretamente nas operações apresentadas.

## Como utilizar

1.  Abra o arquivo correspondente à placa utilizada no Arduino IDE.
2.  Confira as ligações dos sensores e do circuito de EC.
3.  Instale as bibliotecas necessárias.
4.  Selecione a placa e a porta correta no Arduino IDE.
5.  Envie o código para a placa.
6.  Abra o Monitor Serial e configure a velocidade correspondente:
    -   Versão inicial: **9600 baud**.
    -   Versão ESP32: **115200 baud**.
7.  Observe os valores de temperatura, tensão, resistência calculada e
    condutividade exibidos.

## Observações

-   A versão inicial realiza uma leitura de EC pela entrada `A0`.
-   A versão ESP32 permite acompanhar duas entradas de EC, GPIO 34 e
    GPIO 35.
-   Na versão ESP32, leituras de tensão iguais ou muito próximas de 0 V,
    ou iguais ou superiores a `VIN_EC`, são consideradas inválidas.
-   Os valores exibidos dependem do circuito, dos sensores, das ligações
    e da calibração do sistema.
-   O código apresenta o resultado calculado de condutividade, mas não
    especifica no próprio programa uma unidade de medida para esse
    valor.

## Objetivo

Disponibilizar uma implementação de leitura de condutividade elétrica
com acompanhamento de temperatura, incluindo uma versão adaptada para
ESP32 com duas entradas de medição de EC.
