// ============================================================
//  TORRE HIDROPÔNICA — Firmware ESP32 (versão com Supabase)
// ============================================================
//  O QUE ESTE CÓDIGO FAZ
//    1. Lê os sensores a cada intervalo (padrão: 1 minuto).
//    2. Mostra os valores no display LCD 20x4.
//    3. Grava cada medição num arquivo CSV no cartão SD (backup).
//    4. Envia cada medição para o banco de dados Supabase.
//
//  O Telegram e o site NÃO ficam mais na ESP32: eles leem os dados
//  direto do Supabase.
//
//  FLUXO DOS DADOS
//    ESP32 --(HTTPS POST)--> Supabase --> Telegram e Site
//
//  HARDWARE
//    Plataforma    : ESP32 DevKit V1
//    Sensores      : DHT11    (temperatura e umidade do ar)
//                    DS18B20  (temperatura da água)
//                    DS3231   (relógio RTC + temperatura da caixa)
//    Display       : LCD 20x4 I2C (endereço 0x27)
//    Botão         : push button — toque simples, duplo e longo
//    Cartão SD     : SPI (MOSI=23, MISO=19, SCK=18, CS=5)
//    (Não há sensor de pH neste projeto.)
//
//  BIBLIOTECAS (instalar pelo Gerenciador de Bibliotecas do Arduino IDE)
//    - DHT sensor library  (e a "Adafruit Unified Sensor")
//    - OneWire
//    - DallasTemperature
//    - RTClib
//    - LiquidCrystal_I2C
//    As demais (WiFi, HTTPClient, SD, SPI...) já vêm com o core ESP32.
//    A biblioteca ArduinoJson NÃO é mais necessária.
//
//  COMO CONFIGURAR (só o bloco "CONFIGURAÇÕES", logo abaixo)
//    1. Confira o nome e a senha do Wi-Fi (STA_SSID e STA_PASS).
//    2. Cole a URL do projeto e a chave publishable do Supabase.
//    3. Faça o upload e abra o Monitor Serial a 115200.
// ============================================================


// ============================================================
//  BIBLIOTECAS
// ============================================================
#include <Wire.h>              // comunicação I2C (LCD e RTC)
#include <WiFi.h>              // conexão Wi-Fi
#include <WiFiClientSecure.h>  // conexão HTTPS (o Supabase exige HTTPS)
#include <HTTPClient.h>        // envio do POST para o Supabase
#include <DHT.h>               // sensor DHT11
#include <OneWire.h>           // protocolo do DS18B20
#include <DallasTemperature.h> // sensor DS18B20
#include <RTClib.h>            // relógio DS3231
#include <LiquidCrystal_I2C.h> // display LCD
#include <SD.h>                // cartão SD
#include <SPI.h>               // barramento do cartão SD
#include <time.h>              // NTP (hora pela internet) — já vem no core


// ============================================================
//  CONFIGURAÇÕES — é aqui que você mexe
// ============================================================

// ── Wi-Fi da rede que tem internet (modo STA) ──────────────
const char* STA_SSID = "Caio";       // nome da rede (2.4 GHz)
const char* STA_PASS = "caiocosta";     // senha da rede

// ── Wi-Fi próprio da ESP32 (modo AP) ───────────────────────
// Sem internet, não há envio ao Supabase. Este modo só existe
// como reserva, caso a rede Wi-Fi não conecte.
const char* AP_SSID = "Protótipo-IoT";
const char* AP_PASS = "monitor123";

// ── Supabase ───────────────────────────────────────────────
// No painel do Supabase: botão "Connect" do projeto.
// Use a chave PUBLISHABLE (começa com sb_publishable_).
// NUNCA use a chave secret / service_role aqui.
const char* SUPABASE_URL         = "https://kmmwvpycqybeygdkfvrs.supabase.co";
const char* SUPABASE_KEY         = "sb_publishable_dupmIsr-IHlxox_ypd_D2w_-ODzLESz";
const char* SUPABASE_DISPOSITIVO = "torre-01";  // nome desta torre no banco

// ── Início da ESP32 ────────────────────────────────────────
// true  = liga já conectando no Wi-Fi com internet (modo STA).
//         É o que o envio ao Supabase precisa, sem apertar nada.
// false = liga no modo AP (como era antes). Para enviar ao
//         Supabase, será preciso apertar o botão BOOT (GPIO 0).
const bool INICIAR_EM_STA = true;

// ── Fuso horário (hora pela internet) ──────────────────────
// Brasília: UTC-3, sem horário de verão.
#define NTP_SERVIDOR  "pool.ntp.org"
#define NTP_FUSO      (-3 * 3600)   // UTC-3 em segundos
#define NTP_AJUSTE_DS 0             // sem horário de verão

// ── Tempo entre medições ───────────────────────────────────
// 60000 ms = 1 minuto. Para 5 minutos, use 300000.
uint32_t intervaloMs = 15000;

// ── Reconexão do Wi-Fi ─────────────────────────────────────
// Se o Wi-Fi cair, tenta reconectar a cada 30 segundos.
const uint32_t INTERVALO_RECONEXAO_MS = 30000;


// ============================================================
//  PINOS
// ============================================================
#define DHTPIN       16    // dados do DHT11
#define DHTTYPE      DHT11 // modelo do sensor de ar
#define DS18PIN      17    // dados do DS18B20 (água)
#define TOUCH_BTN    32    // botão de navegação (push button)
#define BTN_MODO     0     // botão BOOT da placa: alterna AP/STA
#define SD_CS        5     // chip select do cartão SD


// ============================================================
//  OBJETOS (um para cada componente)
// ============================================================
DHT               dht(DHTPIN, DHTTYPE);   // sensor de ar
OneWire           ow(DS18PIN);            // barramento do sensor de água
DallasTemperature ds18(&ow);              // sensor de água
RTC_DS3231        rtc;                    // relógio
LiquidCrystal_I2C lcd(0x27, 20, 4);       // display 20 colunas x 4 linhas


// ============================================================
//  VARIÁVEIS GLOBAIS
// ============================================================

// ── Última medição (aparece no LCD e é gravada no SD) ──────
float  tempAr   = 0;            // temperatura do ar (°C)
float  umidade  = 0;            // umidade do ar (%)
float  tempAgua = 0;            // temperatura da água (°C)
float  tempCase = 0;            // temperatura dentro da caixa (°C)
String horaMed  = "--:--:--";   // hora da última medição
String dataMed  = "0000-00-00"; // data da última medição

// ── Modo de rede ───────────────────────────────────────────
enum Modo { MODO_AP, MODO_STA };
Modo modoAtual = MODO_AP;       // modo em que a ESP32 está agora

// ── Controle geral ─────────────────────────────────────────
const uint8_t NUM_TELAS = 3;    // quantidade de telas do LCD
uint8_t  telaAtual     = 0;     // tela que está sendo exibida
uint32_t ultimaMedMs   = 0;     // quando foi a última medição (millis)
bool     sdDisponivel  = false; // true se o cartão SD foi encontrado
int8_t   statusBanco   = -1;    // último envio ao Supabase:
                                //   -1 = ainda não enviou / sem Wi-Fi
                                //    0 = falhou
                                //    1 = enviado com sucesso
uint32_t ultimaTentativaWiFiMs = 0; // controle da reconexão do Wi-Fi

bool btnAnterior = HIGH;        // estado anterior do botão BOOT

// ── Controle do botão de navegação ─────────────────────────
bool     estadoAnterior         = LOW;   // estado anterior do botão
uint32_t tempoPressionado       = 0;     // quando foi apertado
uint32_t ultimoClique           = 0;     // quando foi o último toque
bool     aguardandoSegundoToque = false; // esperando um possível 2º toque

const uint16_t debounceTouch = 60;  // ignora ruído do botão (ms)
const uint16_t tempoLongo    = 800; // acima disso é toque longo (ms)
const uint16_t tempoDuplo    = 350; // janela para o toque duplo (ms)


// ============================================================
//  PROTÓTIPOS (avisam o compilador que estas funções existem)
// ============================================================
void iniciarAP();
void iniciarSTA();
void alternarModo();
void garantirWiFi();
void sincronizarNTP();
void garantirDiretorio(String dir);
void salvarMedicaoSD();
bool enviarParaSupabase(bool okAr, bool okUm, bool okAgua);
void fazerMedicao();
void mostrarTela();
void lerTouch();


// ============================================================
//  WI-FI
// ============================================================

// Modo AP: a ESP32 cria sua própria rede Wi-Fi.
// Neste modo NÃO há internet, então nada é enviado ao Supabase.
void iniciarAP() {
  WiFi.mode(WIFI_AP);                 // coloca o Wi-Fi em modo AP
  WiFi.softAP(AP_SSID, AP_PASS);      // cria a rede
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Modo: AP");
  lcd.setCursor(0, 1); lcd.print("192.168.4.1");
}

// Modo STA: a ESP32 conecta numa rede Wi-Fi que tem internet.
// É o modo necessário para enviar dados ao Supabase.
void iniciarSTA() {
  WiFi.mode(WIFI_STA);                // coloca o Wi-Fi em modo estação
  WiFi.begin(STA_SSID, STA_PASS);     // começa a conectar
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Conectando...");

  // Espera até 15 segundos (30 tentativas de 500 ms)
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    // Conectou: mostra o IP e acerta o relógio pela internet
    Serial.print("\nSTA IP: ");
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Wi-Fi OK!");
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP());
    delay(800);
    sincronizarNTP();                 // acerta o RTC automaticamente
  } else {
    // Não conectou: volta para o modo AP
    Serial.println("\nFalha Wi-Fi. Voltando para AP...");
    modoAtual = MODO_AP;
    iniciarAP();
  }
}

// Alterna entre AP e STA (chamada pelo botão BOOT).
void alternarModo() {
  if (modoAtual == MODO_AP) {
    modoAtual = MODO_STA;
    iniciarSTA();
  } else {
    modoAtual = MODO_AP;
    iniciarAP();
  }
  delay(500);
}

// Se estiver em modo STA e o Wi-Fi cair, tenta reconectar a cada
// 30 segundos, sem travar o resto do programa.
void garantirWiFi() {
  if (modoAtual != MODO_STA) return;                  // só vale para STA
  if (WiFi.status() == WL_CONNECTED) return;          // está tudo certo
  if (millis() - ultimaTentativaWiFiMs < INTERVALO_RECONEXAO_MS) return;

  ultimaTentativaWiFiMs = millis();
  Serial.println("Wi-Fi caiu. Tentando reconectar...");
  WiFi.disconnect();
  WiFi.begin(STA_SSID, STA_PASS);
}


// ============================================================
//  HORA PELA INTERNET (NTP)
// ============================================================

// Busca a hora na internet e acerta o relógio DS3231 com ela.
void sincronizarNTP() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Sincronizando");
  lcd.setCursor(0, 1); lcd.print("hora (NTP)...");

  configTime(NTP_FUSO, NTP_AJUSTE_DS, NTP_SERVIDOR);

  // Espera a hora chegar (até 10 segundos)
  struct tm info;
  int tentativas = 0;
  while (!getLocalTime(&info) && tentativas < 20) {
    delay(500);
    tentativas++;
  }

  if (getLocalTime(&info)) {
    // Deu certo: grava a hora no RTC DS3231
    rtc.adjust(DateTime(
      info.tm_year + 1900,   // tm_year conta a partir de 1900
      info.tm_mon  + 1,      // tm_mon vai de 0 a 11
      info.tm_mday,
      info.tm_hour,
      info.tm_min,
      info.tm_sec
    ));

    char buf[32];
    snprintf(buf, sizeof(buf), "%02d/%02d/%04d %02d:%02d",
             info.tm_mday, info.tm_mon + 1, info.tm_year + 1900,
             info.tm_hour, info.tm_min);

    Serial.print("RTC sincronizado via NTP: ");
    Serial.println(buf);

    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Hora OK!");
    lcd.setCursor(0, 1); lcd.print(String(info.tm_hour) + ":" +
                                   (info.tm_min < 10 ? "0" : "") +
                                   String(info.tm_min));
    delay(1500);
  } else {
    // Não deu: o RTC continua com a hora que já tinha
    Serial.println("NTP: falha ao obter hora. RTC mantém hora anterior.");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("NTP falhou.");
    lcd.setCursor(0, 1); lcd.print("Hora anterior.");
    delay(1500);
  }
}


// ============================================================
//  CARTÃO SD (backup local das medições)
// ============================================================

// Cria a pasta se ela ainda não existir.
void garantirDiretorio(String dir) {
  if (!SD.exists(dir)) {
    SD.mkdir(dir);
  }
}

// Grava uma linha no arquivo CSV do dia: /dados/AAAA-MM-DD.csv
// Formato: hora,tempAr,umidade,tempAgua,tempCase
void salvarMedicaoSD() {
  if (!sdDisponivel) return;          // sem cartão, não faz nada
  garantirDiretorio("/dados");

  String arquivo = "/dados/" + dataMed + ".csv";
  bool novo = !SD.exists(arquivo);    // é o primeiro registro do dia?

  File f = SD.open(arquivo, FILE_APPEND);
  if (!f) {
    Serial.println("Erro ao abrir CSV para escrita");
    return;
  }

  // Arquivo novo: escreve primeiro o cabeçalho das colunas
  if (novo) {
    f.println("hora,tempAr,umidade,tempAgua,tempCase");
  }

  f.print(horaMed);    f.print(",");
  f.print(tempAr, 1);  f.print(",");
  f.print(umidade, 1); f.print(",");
  f.print(tempAgua, 1); f.print(",");
  f.println(tempCase, 1);
  f.close();

  Serial.println("Medição salva no SD: " + arquivo);
}


// ============================================================
//  SUPABASE (envio das medições para o banco de dados)
// ============================================================

// Envia a medição atual para a tabela "leituras" do Supabase.
//
// Os parâmetros dizem se cada sensor teve leitura VÁLIDA nesta
// medição. O código guarda o último valor bom quando um sensor
// falha; sem esse cuidado, um valor velho seria enviado ao banco
// como se fosse novo. Por isso só vão os campos válidos, e os
// demais ficam vazios (NULL) no banco.
//
// A hora NÃO é enviada: o próprio banco grava a hora do envio.
//
// Devolve true se o banco confirmou o recebimento.
bool enviarParaSupabase(bool okAr, bool okUm, bool okAgua) {
  // Sem Wi-Fi (ou em modo AP) não há como enviar
  if (WiFi.status() != WL_CONNECTED) return false;

  // Monta o JSON com os nomes das colunas da tabela "leituras":
  // {"dispositivo":"torre-01","temp_ar":25.3,"umidade_ar":61.0,...}
  String json = "{\"dispositivo\":\"" + String(SUPABASE_DISPOSITIVO) + "\"";
  if (okAr)   json += ",\"temp_ar\":"    + String(tempAr, 1);
  if (okUm)   json += ",\"umidade_ar\":" + String(umidade, 1);
  if (okAgua) json += ",\"temp_agua\":"  + String(tempAgua, 1);
  json += ",\"temp_case\":" + String(tempCase, 1);  // sempre vem do RTC
  json += "}";

  // Conexão HTTPS. setInsecure() não confere o certificado do
  // servidor: serve para o protótipo, mas não para produção.
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  String endereco = String(SUPABASE_URL) + "/rest/v1/leituras";
  if (!http.begin(client, endereco)) {
    Serial.println("Supabase: falha ao iniciar a conexão HTTP");
    return false;
  }

  http.setTimeout(8000);  // desiste se o servidor não responder em 8 s

  // Cabeçalhos exigidos pela API do Supabase
  http.addHeader("Content-Type", "application/json");
  http.addHeader("apikey", SUPABASE_KEY);
  // Chave nova (sb_publishable_...) vai só em "apikey". Se usarem a
  // chave antiga "anon" (começa com eyJ), ela vai também em Authorization.
  if (!String(SUPABASE_KEY).startsWith("sb_")) {
    http.addHeader("Authorization", String("Bearer ") + SUPABASE_KEY);
  }
  // Pede ao banco uma resposta vazia (a ESP32 só precisa gravar)
  http.addHeader("Prefer", "return=minimal");

  Serial.println("Supabase POST: " + json);
  int codigo = http.POST(json);          // envia e recebe o código HTTP

  // Códigos 200 a 299 significam sucesso (o Supabase devolve 201)
  bool ok = (codigo >= 200 && codigo < 300);

  if (ok) {
    Serial.println("Supabase OK (" + String(codigo) + ")");
  } else {
    Serial.println("Supabase FALHOU (" + String(codigo) + ")");
    if (codigo > 0) {
      // O banco explica o motivo do erro; mostra no Monitor Serial
      Serial.println(http.getString());
    } else {
      // Código negativo = problema de rede
      Serial.println(HTTPClient::errorToString(codigo));
    }
  }

  http.end();   // libera a conexão
  return ok;
}


// ============================================================
//  MEDIÇÃO
// ============================================================

// Lê todos os sensores, guarda no SD, mostra no LCD e envia ao banco.
void fazerMedicao() {
  // ── Sensor de ar (DHT11) ─────────────────────────────────
  float t = dht.readTemperature();   // °C
  float h = dht.readHumidity();      // %

  // Se a leitura falhar (NaN), mantém o valor anterior
  if (!isnan(t)) tempAr  = t;
  if (!isnan(h)) umidade = h;

  // ── Sensor de água (DS18B20) ─────────────────────────────
  ds18.requestTemperatures();
  float tw = ds18.getTempCByIndex(0);
  // DEVICE_DISCONNECTED_C = sensor desconectado
  if (tw != DEVICE_DISCONNECTED_C) tempAgua = tw;

  // ── Quais leituras DESTA medição foram válidas ───────────
  // Usado só no envio ao banco (ver enviarParaSupabase).
  // 85.0 °C é o valor de reset do DS18B20, não uma temperatura real.
  bool okAr   = !isnan(t);
  bool okUm   = !isnan(h);
  bool okAgua = (tw != DEVICE_DISCONNECTED_C && tw != 85.0f);

  // ── Temperatura dentro da caixa (sensor do relógio) ──────
  tempCase = rtc.getTemperature();

  // ── Data e hora atuais, vindas do RTC ────────────────────
  DateTime now = rtc.now();
  char bufH[9], bufD[11];
  sprintf(bufH, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  sprintf(bufD, "%04d-%02d-%02d", now.year(), now.month(), now.day());
  horaMed = String(bufH);
  dataMed = String(bufD);

  ultimaMedMs = millis();   // marca o instante da medição

  // ── Backup no SD e atualização do LCD ────────────────────
  salvarMedicaoSD();
  mostrarTela();            // mostra já os valores (o envio pode demorar)

  // ── Envio ao Supabase (só com Wi-Fi em modo STA) ─────────
  if (modoAtual == MODO_STA && WiFi.status() == WL_CONNECTED) {
    statusBanco = enviarParaSupabase(okAr, okUm, okAgua) ? 1 : 0;
  } else {
    statusBanco = -1;       // sem internet: não tentou enviar
  }
  mostrarTela();            // atualiza o status do banco no LCD
}


// ============================================================
//  DISPLAY LCD
// ============================================================

// Desenha no LCD a tela atual (0, 1 ou 2).
void mostrarTela() {
  lcd.clear();

  // Indicador de tela no canto superior direito: [1/3]
  lcd.setCursor(15, 0);
  lcd.print("[");
  lcd.print(telaAtual + 1);
  lcd.print("/3]");

  switch (telaAtual) {
    case 0:   // Ar: temperatura e umidade
      lcd.setCursor(0, 0); lcd.print("Temp. Ar:");
      lcd.setCursor(0, 1); lcd.print(tempAr, 1); lcd.print(" C");
      lcd.setCursor(0, 2); lcd.print("Umidade:");
      lcd.setCursor(0, 3); lcd.print(umidade, 1); lcd.print(" %");
      break;

    case 1:   // Água e caixa
      lcd.setCursor(0, 0); lcd.print("Temp. Agua:");
      lcd.setCursor(0, 1); lcd.print(tempAgua, 1); lcd.print(" C");
      lcd.setCursor(0, 2); lcd.print("Temp. Case:");
      lcd.setCursor(0, 3); lcd.print(tempCase, 1); lcd.print(" C");
      break;

    case 2:   // Status do sistema
      lcd.setCursor(0, 0); lcd.print("Ult. medicao:");
      lcd.setCursor(0, 1); lcd.print(horaMed);
      lcd.setCursor(0, 2);
      lcd.print("SD:");
      lcd.print(sdDisponivel ? "OK" : "FALHA");
      lcd.print(" WiFi:");
      lcd.print(WiFi.status() == WL_CONNECTED ? "OK" : "--");
      lcd.setCursor(0, 3);
      lcd.print("Banco: ");
      lcd.print(statusBanco < 0 ? "--" : (statusBanco ? "OK" : "FALHA"));
      break;
  }
}


// ============================================================
//  BOTÃO DE NAVEGAÇÃO
//    Toque simples : próxima tela
//    Toque longo   : tela anterior
//    Toque duplo   : mede agora
// ============================================================
void lerTouch() {
  static uint32_t ultimoDebounce = 0;

  // Pull-up: apertado = LOW, por isso invertemos (true = apertado)
  bool estadoAtual = !digitalRead(TOUCH_BTN);
  uint32_t agora = millis();

  // Ignora mudanças muito rápidas (ruído do botão)
  if (agora - ultimoDebounce < debounceTouch) return;

  // Momento em que o botão foi APERTADO: guarda a hora
  if (estadoAnterior == LOW && estadoAtual == HIGH) {
    tempoPressionado = agora;
    ultimoDebounce = agora;
  }

  // Momento em que o botão foi SOLTO: decide qual toque foi
  if (estadoAnterior == HIGH && estadoAtual == LOW) {
    uint32_t duracao = agora - tempoPressionado;

    if (duracao >= tempoLongo) {
      // Toque longo: volta uma tela
      telaAtual = (telaAtual + NUM_TELAS - 1) % NUM_TELAS;
      mostrarTela();
      aguardandoSegundoToque = false;
    } else {
      if (aguardandoSegundoToque && (agora - ultimoClique <= tempoDuplo)) {
        // Segundo toque dentro da janela: toque duplo, mede agora
        fazerMedicao();
        aguardandoSegundoToque = false;
      } else {
        // Primeiro toque: espera para ver se vem um segundo
        aguardandoSegundoToque = true;
        ultimoClique = agora;
      }
    }
    ultimoDebounce = agora;
  }

  // Passou o tempo do toque duplo sem 2º toque: era toque simples
  if (aguardandoSegundoToque && (agora - ultimoClique > tempoDuplo)) {
    telaAtual = (telaAtual + 1) % NUM_TELAS;
    mostrarTela();
    aguardandoSegundoToque = false;
  }

  estadoAnterior = estadoAtual;
}


// ============================================================
//  SETUP (roda uma vez, quando a ESP32 liga)
// ============================================================
void setup() {
  Serial.begin(115200);   // Monitor Serial a 115200

  // Botões com resistor interno: solto = HIGH, apertado = LOW
  pinMode(BTN_MODO, INPUT_PULLUP);
  pinMode(TOUCH_BTN, INPUT_PULLUP);

  Wire.begin(21, 22);     // I2C: SDA = 21, SCL = 22

  // ── Display ──────────────────────────────────────────────
  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("  Protótipo IoT  ");
  lcd.setCursor(0, 1); lcd.print(" Iniciando...  ");

  // ── Sensores ─────────────────────────────────────────────
  dht.begin();
  ds18.begin();

  // ── Relógio RTC ──────────────────────────────────────────
  if (!rtc.begin()) {
    Serial.println("RTC nao encontrado!");
  }
  // Se o relógio ficou sem bateria, usa a hora da compilação
  // até a internet acertar (NTP).
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // ── Cartão SD (SCK=18, MISO=19, MOSI=23, CS=5) ───────────
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Iniciando SD...");
  SPI.begin(18, 19, 23, SD_CS);
  if (!SD.begin(SD_CS, SPI, 1000000)) {
    Serial.println("SD Card: falha ou nao presente.");
    sdDisponivel = false;
    lcd.setCursor(0, 1); lcd.print("SD: FALHA");
  } else {
    Serial.println("SD Card OK.");
    sdDisponivel = true;
    garantirDiretorio("/dados");
    lcd.setCursor(0, 1); lcd.print("SD: OK");
  }
  delay(800);

  delay(1500);

  // ── Rede ─────────────────────────────────────────────────
  if (INICIAR_EM_STA) {
    modoAtual = MODO_STA;
    iniciarSTA();   // se não conectar, volta sozinha para o modo AP
  } else {
    modoAtual = MODO_AP;
    iniciarAP();
  }

  // Primeira medição já na inicialização
  fazerMedicao();
}


// ============================================================
//  LOOP (roda sem parar)
// ============================================================
void loop() {
  // Botão BOOT da placa: alterna entre modo AP e modo STA
  bool btnAtual = digitalRead(BTN_MODO);
  if (btnAnterior == HIGH && btnAtual == LOW) {
    delay(50);                          // espera passar o ruído
    if (digitalRead(BTN_MODO) == LOW) { // confirma que continua apertado
      alternarModo();
    }
  }
  btnAnterior = btnAtual;

  // Reconecta o Wi-Fi sozinho se ele cair (modo STA)
  garantirWiFi();

  // Medição periódica: quando passou o intervalo, mede e envia
  if (millis() - ultimaMedMs >= intervaloMs) {
    fazerMedicao();
  }

  // Botão de navegação (troca de tela e medição manual)
  lerTouch();
}
