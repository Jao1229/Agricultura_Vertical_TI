// ============================================================
//  Proj.Plantacao — Protótipo IoT
//  Plataforma : ESP32 DevKit V1
//  Sensores   : DHT11 (ar), DS18B20 (água), DS3231 (RTC/case)
//  Display    : LCD 20x4 I2C (0x27)
//  Botão      : Push button — toque simples, duplo e longo
//  Rede       : AP próprio ou STA (Wi-Fi externo), alternável
//  Armazenamento: SD Card via SPI (MOSI=23, MISO=19, SCK=18, CS=5)
//  Relatórios : HTML/PDF com gráficos + envio via bot Telegram
// ============================================================

#include <WiFi.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <SD.h>
#include <SPI.h>
#include <ArduinoJson.h>
#include <time.h>       // NTP — já incluso no core ESP32, sem biblioteca extra

// ── Fuso horário ───────────────────────────────────────────
// Brasília: UTC-3 (sem horário de verão)
#define NTP_SERVIDOR  "pool.ntp.org"
#define NTP_FUSO      -3 * 3600   // UTC-3 em segundos
#define NTP_AJUSTE_DS 0           // sem horário de verão

// ── Protótipos ─────────────────────────────────────────────
bool enviarMensagemParaChat(String chatId, String mensagem);
bool enviarMensagemTelegram(String mensagem);
bool enviarRelatorioParaChat(String chatId, String data);
void sincronizarNTP();
void iniciarAP();
void iniciarSTA();
void alternarModo();
void registrarRotas();
String paginaHTML();
String paginaRelatorio();
String paginaHistorico();
void fazerMedicao();
void mostrarTela();
void lerTouch();
void salvarMedicaoSD();
void gerarRelatorioSD();
bool enviarRelatorioTelegram();
String lerCSVdoDia(String data);
struct Estatisticas calcularEstatisticas(String data);

// ── Pinos ──────────────────────────────────────────────────
#define DHTPIN       16
#define DHTTYPE      DHT11
#define DS18PIN      17
#define TOUCH_BTN    32
#define BTN_MODO     0
#define SD_CS        5

// ── Credenciais Wi-Fi ──────────────────────────────────────
const char* STA_SSID  = "Coelho";
const char* STA_PASS  = "cj031331";
const char* AP_SSID   = "Protótipo-IoT";
const char* AP_PASS   = "monitor123";

// ── Telegram ───────────────────────────────────────────────
// *** PREENCHA COM SEUS DADOS (veja o guia TELEGRAM_SETUP.md) ***
const char* TELEGRAM_TOKEN   = "8363296026:AAFkWPUX4W3V4uENy_eiMy3kdrWpW5nnPg4";
const char* TELEGRAM_CHAT_ID = "5826453377";

// ── Objetos ────────────────────────────────────────────────
DHT               dht(DHTPIN, DHTTYPE);
OneWire           ow(DS18PIN);
DallasTemperature ds18(&ow);
RTC_DS3231        rtc;
LiquidCrystal_I2C lcd(0x27, 20, 4);
WebServer         server(80);

// ── Dados atuais ───────────────────────────────────────────
float  tempAr    = 0;
float  umidade   = 0;
float  tempAgua  = 0;
float  tempCase  = 0;
String horaMed   = "--:--:--";
String dataMed   = "0000-00-00";

// ── Estrutura de estatísticas ──────────────────────────────
struct Estatisticas {
  float minTempAr, maxTempAr, medTempAr;
  float minUmidade, maxUmidade, medUmidade;
  float minTempAgua, maxTempAgua, medTempAgua;
  float minTempCase, maxTempCase, medTempCase;
  int   totalMedicoes;
};

// ── Controle ───────────────────────────────────────────────
enum Modo { MODO_AP, MODO_STA };
Modo modoAtual = MODO_AP;

const uint8_t NUM_TELAS = 3;
uint8_t  telaAtual    = 0;
uint32_t ultimaMedMs  = 0;
uint32_t intervaloMs  = 60000;
bool     sdDisponivel = false;

bool btnAnterior = HIGH;

// ── Variáveis do botão ─────────────────────────────────────
bool     estadoAnterior       = LOW;
uint32_t tempoPressionado     = 0;
uint32_t ultimoClique         = 0;
bool     aguardandoSegundoToque = false;

const uint16_t debounceTouch = 60;
const uint16_t tempoLongo    = 800;
const uint16_t tempoDuplo    = 350;

// ── Telegram polling ───────────────────────────────────────
uint32_t ultimoTelegramMs = 0;
const uint32_t intervaloTelegram = 5000;   // verifica a cada 5s
int64_t  ultimoUpdateId  = 0;

// ── Controle de relatório diário ───────────────────────────
uint8_t  diaUltimoRelatorio = 0;

// ── Wi-Fi ────────────────────────────────────────────────
void iniciarAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Modo: AP");
  lcd.setCursor(0, 1); lcd.print("192.168.4.1");
}

void iniciarSTA() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(STA_SSID, STA_PASS);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Conectando...");
  int tentativas = 0;
  while (WiFi.status() != WL_CONNECTED && tentativas < 30) {
    delay(500);
    Serial.print(".");
    tentativas++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("\nSTA IP: ");
    Serial.println(WiFi.localIP());
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Wi-Fi OK!");
    lcd.setCursor(0, 1); lcd.print(WiFi.localIP());
    delay(800);
    sincronizarNTP();  // acerta o RTC automaticamente
  } else {
    Serial.println("\nFalha Wi-Fi. Voltando para AP...");
    modoAtual = MODO_AP;
    iniciarAP();
  }
}

void alternarModo() {
  server.stop();
  if (modoAtual == MODO_AP) {
    modoAtual = MODO_STA;
    iniciarSTA();
  } else {
    modoAtual = MODO_AP;
    iniciarAP();
  }
  delay(500);
  registrarRotas();
  server.begin();
}

// ── NTP ───────────────────────────────────────────────────
void sincronizarNTP() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Sincronizando");
  lcd.setCursor(0, 1); lcd.print("hora (NTP)...");

  configTime(NTP_FUSO, NTP_AJUSTE_DS, NTP_SERVIDOR);

  // Aguarda a hora ser obtida (até 10 segundos)
  struct tm info;
  int tentativas = 0;
  while (!getLocalTime(&info) && tentativas < 20) {
    delay(500);
    tentativas++;
  }

  if (getLocalTime(&info)) {
    // Aplica no RTC DS3231
    rtc.adjust(DateTime(
      info.tm_year + 1900,
      info.tm_mon  + 1,
      info.tm_mday,
      info.tm_hour,
      info.tm_min,
      info.tm_sec
    ));

    char buf[20];
    sprintf(buf, "%02d/%02d/%04d %02d:%02d",
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
    Serial.println("NTP: falha ao obter hora. RTC mantém hora anterior.");
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("NTP falhou.");
    lcd.setCursor(0, 1); lcd.print("Hora anterior.");
    delay(1500);
  }
}

// ── SD Card ──────────────────────────────────────────────

// Garante que o diretório existe
void garantirDiretorio(String dir) {
  if (!SD.exists(dir)) {
    SD.mkdir(dir);
  }
}

// Salva linha CSV: /dados/YYYY-MM-DD.csv
void salvarMedicaoSD() {
  if (!sdDisponivel) return;
  garantirDiretorio("/dados");

  String arquivo = "/dados/" + dataMed + ".csv";
  bool novo = !SD.exists(arquivo);

  File f = SD.open(arquivo, FILE_APPEND);
  if (!f) {
    Serial.println("Erro ao abrir CSV para escrita");
    return;
  }

  if (novo) {
    f.println("hora,tempAr,umidade,tempAgua,tempCase");
  }

  f.print(horaMed);   f.print(",");
  f.print(tempAr, 1); f.print(",");
  f.print(umidade, 1); f.print(",");
  f.print(tempAgua, 1); f.print(",");
  f.println(tempCase, 1);
  f.close();

  Serial.println("Medição salva no SD: " + arquivo);
}

// Lê CSV do dia e retorna como String
String lerCSVdoDia(String data) {
  if (!sdDisponivel) return "";
  String arquivo = "/dados/" + data + ".csv";
  if (!SD.exists(arquivo)) return "";

  File f = SD.open(arquivo, FILE_READ);
  if (!f) return "";

  String conteudo = "";
  while (f.available()) {
    conteudo += (char)f.read();
  }
  f.close();
  return conteudo;
}

// Calcula estatísticas a partir do CSV
struct Estatisticas calcularEstatisticas(String data) {
  struct Estatisticas e = {999,-999,0, 999,-999,0, 999,-999,0, 999,-999,0, 0};

  String csv = lerCSVdoDia(data);
  if (csv.length() == 0) return e;

  int linha = 0;
  int pos = 0;
  float somaTar=0, somaUm=0, somaTw=0, somaTc=0;

  while (pos < (int)csv.length()) {
    int fim = csv.indexOf('\n', pos);
    if (fim == -1) fim = csv.length();
    String row = csv.substring(pos, fim);
    row.trim();
    pos = fim + 1;

    if (linha == 0 || row.length() == 0) { linha++; continue; } // pula header

    // parse CSV: hora,tempAr,umidade,tempAgua,tempCase
    int c1 = row.indexOf(',');
    int c2 = row.indexOf(',', c1+1);
    int c3 = row.indexOf(',', c2+1);
    int c4 = row.indexOf(',', c3+1);
    if (c1<0||c2<0||c3<0||c4<0) { linha++; continue; }

    float ta  = row.substring(c1+1, c2).toFloat();
    float um  = row.substring(c2+1, c3).toFloat();
    float tw  = row.substring(c3+1, c4).toFloat();
    float tc  = row.substring(c4+1).toFloat();

    e.minTempAr   = min(e.minTempAr,   ta);
    e.maxTempAr   = max(e.maxTempAr,   ta);
    e.minUmidade  = min(e.minUmidade,  um);
    e.maxUmidade  = max(e.maxUmidade,  um);
    e.minTempAgua = min(e.minTempAgua, tw);
    e.maxTempAgua = max(e.maxTempAgua, tw);
    e.minTempCase = min(e.minTempCase, tc);
    e.maxTempCase = max(e.maxTempCase, tc);

    somaTar += ta; somaUm += um; somaTw += tw; somaTc += tc;
    e.totalMedicoes++;
    linha++;
  }

  if (e.totalMedicoes > 0) {
    e.medTempAr   = somaTar / e.totalMedicoes;
    e.medUmidade  = somaUm  / e.totalMedicoes;
    e.medTempAgua = somaTw  / e.totalMedicoes;
    e.medTempCase = somaTc  / e.totalMedicoes;
  }

  return e;
}

// Salva relatório HTML no SD: /relatorios/YYYY-MM-DD.html
void gerarRelatorioSD() {
  if (!sdDisponivel) return;
  garantirDiretorio("/relatorios");
  String arquivo = "/relatorios/" + dataMed + ".html";
  File f = SD.open(arquivo, FILE_WRITE);
  if (!f) {
    Serial.println("Erro ao criar relatório no SD");
    return;
  }
  f.print(paginaRelatorio());
  f.close();
  Serial.println("Relatório salvo: " + arquivo);
}

// ── Telegram ─────────────────────────────────────────────

// Envia mensagem para um chat_id específico
bool enviarMensagemParaChat(String chatId, String mensagem) {
  if (WiFi.status() != WL_CONNECTED) return false;

  HTTPClient http;
  String url = "https://api.telegram.org/bot";
  url += TELEGRAM_TOKEN;
  url += "/sendMessage";

  http.begin(url);
  http.addHeader("Content-Type", "application/json");

  DynamicJsonDocument doc(1024);
  doc["chat_id"]    = chatId;
  doc["text"]       = mensagem;
  doc["parse_mode"] = "Markdown";

  String payload;
  serializeJson(doc, payload);

  int code = http.POST(payload);
  http.end();
  return (code == 200);
}

// Envia mensagem para o chat_id fixo (notificações automáticas)
bool enviarMensagemTelegram(String mensagem) {
  return enviarMensagemParaChat(String(TELEGRAM_CHAT_ID), mensagem);
}

// Envia relatório para um chat_id específico
bool enviarRelatorioParaChat(String chatId, String data) {
  if (WiFi.status() != WL_CONNECTED) return false;

  String ip = (modoAtual == MODO_AP)
              ? WiFi.softAPIP().toString()
              : WiFi.localIP().toString();

  String arquivo = "/dados/" + data + ".csv";
  bool temCSV = sdDisponivel && SD.exists(arquivo);

  String msg = "📊 *Relatório Protótipo IoT*\n";
  msg += "📅 Data: " + data + "\n";
  msg += "🕐 Gerado: " + horaMed + "\n";

  if (temCSV) {
    struct Estatisticas e = calcularEstatisticas(data);
    msg += "📏 Medições: " + String(e.totalMedicoes) + "\n\n";

    msg += "🌡️ *Temperatura do Ar*\n";
    msg += "  Mín: " + String(e.minTempAr, 1) + "°C | ";
    msg += "Méd: " + String(e.medTempAr, 1) + "°C | ";
    msg += "Máx: " + String(e.maxTempAr, 1) + "°C\n\n";

    msg += "💧 *Umidade*\n";
    msg += "  Mín: " + String(e.minUmidade, 1) + "% | ";
    msg += "Méd: " + String(e.medUmidade, 1) + "% | ";
    msg += "Máx: " + String(e.maxUmidade, 1) + "%\n\n";

    msg += "🌊 *Temperatura da Água*\n";
    msg += "  Mín: " + String(e.minTempAgua, 1) + "°C | ";
    msg += "Méd: " + String(e.medTempAgua, 1) + "°C | ";
    msg += "Máx: " + String(e.maxTempAgua, 1) + "°C\n\n";

    msg += "🖥️ *Temperatura do Case*\n";
    msg += "  Mín: " + String(e.minTempCase, 1) + "°C | ";
    msg += "Méd: " + String(e.medTempCase, 1) + "°C | ";
    msg += "Máx: " + String(e.maxTempCase, 1) + "°C\n\n";
  } else {
    msg += "⚠️ _SD indisponível — exibindo última medição_\n\n";
    msg += "🌡️ *Temp. Ar:* " + String(tempAr, 1) + "°C\n";
    msg += "💧 *Umidade:* " + String(umidade, 1) + "%\n";
    msg += "🌊 *Temp. Água:* " + String(tempAgua, 1) + "°C\n";
    msg += "🖥️ *Temp. Case:* " + String(tempCase, 1) + "°C\n\n";
  }

  msg += "🌐 PDF: http://" + ip + "/relatorio\n";
  msg += "📁 Histórico: http://" + ip + "/historico";

  return enviarMensagemParaChat(chatId, msg);
}

// Envia o CSV do dia como documento via Telegram
// Verifica novas mensagens no Telegram (polling)
void verificarTelegram() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  String url = "https://api.telegram.org/bot";
  url += TELEGRAM_TOKEN;
  url += "/getUpdates?timeout=1&offset=";
  url += String(ultimoUpdateId + 1);

  http.begin(url);
  int code = http.GET();

  if (code != 200) {
    http.end();
    return;
  }

  String resposta = http.getString();
  http.end();

  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc, resposta);
  if (err) return;

  JsonArray updates = doc["result"].as<JsonArray>();

  for (JsonObject upd : updates) {
    int64_t updateId = upd["update_id"].as<int64_t>();
    if (updateId <= ultimoUpdateId) continue;
    ultimoUpdateId = updateId;

    String chatId = upd["message"]["chat"]["id"].as<String>();
    String texto  = upd["message"]["text"].as<String>();
    texto.toLowerCase();
    texto.trim();

    Serial.println("Telegram: [" + chatId + "] " + texto);

    if (texto == "/relatorio" || texto == "/report") {
      enviarMensagemParaChat(chatId, "⏳ Gerando relatório, aguarde...");
      gerarRelatorioSD();
      enviarRelatorioParaChat(chatId, dataMed);
    }
    else if (texto == "/medicao" || texto == "/agora") {
      fazerMedicao();
      String msg = "📡 *Medição Atual*\n";
      msg += "🕐 " + horaMed + " | 📅 " + dataMed + "\n\n";
      msg += "🌡️ Ar: " + String(tempAr, 1) + "°C\n";
      msg += "💧 Umidade: " + String(umidade, 1) + "%\n";
      msg += "🌊 Água: " + String(tempAgua, 1) + "°C\n";
      msg += "🖥️ Case: " + String(tempCase, 1) + "°C";
      enviarMensagemParaChat(chatId, msg);
    }
    else if (texto == "/ajuda" || texto == "/help" || texto == "/start") {
      String msg = "🤖 *Torre Hidroponia — Comandos*\n\n";
      msg += "/relatorio — Gera e envia o relatório do dia\n";
      msg += "/medicao — Mede agora e envia os valores\n";
      msg += "/ajuda — Exibe esta mensagem";
      enviarMensagemParaChat(chatId, msg);
    }
    else {
      enviarMensagemParaChat(chatId, "❓ Comando não reconhecido. Use /ajuda para ver os comandos.");
    }
  }
}

// ── Página principal ─────────────────────────────────────
String paginaHTML() {
  String ip   = (modoAtual == MODO_AP)
                ? WiFi.softAPIP().toString()
                : WiFi.localIP().toString();

  String modo = (modoAtual == MODO_AP) ? "AP — rede própria" : "STA — Wi-Fi externo";
  uint32_t minutos = intervaloMs / 60000;

  String h = "";
  h += "<!DOCTYPE html><html lang='pt-BR'><head>";
  h += "<meta charset='UTF-8'>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<meta http-equiv='refresh' content='30'>";
  h += "<title>Protótipo IoT</title>";
  h += "<style>";
  h += ":root{";
  h += "--bg:#0b0f18;--sur:#131929;--bdr:#1e2a40;";
  h += "--txt:#c8d4e8;--dim:#5a6a84;";
  h += "--blue:#4a9eff;--teal:#2cb67d;--orange:#e8923a;--red:#e05555;--purple:#9b7fe8;";
  h += "}";
  h += "*{box-sizing:border-box;margin:0;padding:0}";
  h += "body{background:var(--bg);color:var(--txt);font-family:sans-serif;min-height:100vh;padding:24px 16px 48px}";
  h += "h1{text-align:center;font-size:1.4rem;letter-spacing:3px;text-transform:uppercase;color:#f0f4ff;margin-bottom:4px}";
  h += ".sub{text-align:center;font-size:.72rem;color:var(--dim);margin-bottom:6px;letter-spacing:1px}";
  h += ".ip{text-align:center;font-size:.72rem;color:var(--teal);margin-bottom:24px;font-family:monospace}";
  h += ".grid{display:grid;grid-template-columns:repeat(2,1fr);gap:12px;max-width:500px;margin:0 auto 24px}";
  h += ".card{background:var(--sur);border:1px solid var(--bdr);border-radius:14px;padding:20px 12px;text-align:center}";
  h += ".card-label{font-size:.65rem;color:var(--dim);text-transform:uppercase;letter-spacing:1px;margin-bottom:8px}";
  h += ".card-val{font-size:2rem;font-weight:700;line-height:1}";
  h += ".card-unit{font-size:.78rem;color:var(--dim);margin-top:6px}";
  h += ".c-ar{border-top:3px solid var(--blue)}.c-ar .card-val{color:var(--blue)}";
  h += ".c-um{border-top:3px solid var(--teal)}.c-um .card-val{color:var(--teal)}";
  h += ".c-ag{border-top:3px solid var(--orange)}.c-ag .card-val{color:var(--orange)}";
  h += ".c-cs{border-top:3px solid var(--purple)}.c-cs .card-val{color:var(--purple)}";
  h += ".panel{background:var(--sur);border:1px solid var(--bdr);border-radius:14px;padding:18px 20px;max-width:500px;margin:0 auto 14px}";
  h += ".panel h2{font-size:.72rem;color:var(--dim);text-transform:uppercase;letter-spacing:1px;margin-bottom:14px}";
  h += ".row{display:flex;align-items:center;gap:10px;flex-wrap:wrap}";
  h += "input[type=number]{background:var(--bg);border:1px solid var(--bdr);color:var(--txt);border-radius:8px;padding:8px 12px;font-size:.95rem;width:80px}";
  h += ".btn{border:none;border-radius:8px;padding:9px 18px;font-size:.88rem;cursor:pointer;font-weight:600}";
  h += ".btn-save{background:#185fa5;color:#fff}";
  h += ".btn-save:hover{background:var(--blue)}";
  h += ".btn-modo{background:#1e1430;color:var(--purple);border:1px solid var(--purple)}";
  h += ".btn-modo:hover{background:var(--purple);color:#fff}";
  h += ".btn-report{background:#0d2a1a;color:var(--teal);border:1px solid var(--teal);display:flex;align-items:center;gap:6px}";
  h += ".btn-report:hover{background:var(--teal);color:#fff}";
  h += ".btn-hist{background:#1a1520;color:var(--purple);border:1px solid var(--purple)}";
  h += ".btn-hist:hover{background:var(--purple);color:#fff}";
  h += ".badge{display:inline-block;padding:3px 10px;border-radius:20px;font-size:.68rem;font-weight:600;letter-spacing:1px;text-transform:uppercase}";
  h += ".badge-ap{background:#0d2210;color:var(--teal);border:1px solid var(--teal)}";
  h += ".badge-sta{background:#0e1320;color:var(--blue);border:1px solid var(--blue)}";
  h += ".footer{text-align:center;font-size:.68rem;color:var(--dim);margin-top:20px}";
  h += ".sd-status{text-align:center;font-size:.68rem;margin-bottom:8px}";
  h += "</style></head><body>";

  h += "<h1>Protótipo IoT</h1>";
  h += "<p class='sub'>ESP32 DevKit V1 &nbsp;·&nbsp; atualiza a cada 30s</p>";
  h += "<p class='ip'>" + ip + "</p>";

  // Status do SD
  h += "<p class='sd-status' style='color:";
  h += sdDisponivel ? "#2cb67d" : "#e05555";
  h += "'>";
  h += sdDisponivel ? "● SD Card OK" : "● SD Card não detectado";
  h += "</p>";

  h += "<div class='grid'>";
  h += "<div class='card c-ar'><div class='card-label'>Temp. Ar</div><div class='card-val'>" + String(tempAr, 1) + "</div><div class='card-unit'>°C</div></div>";
  h += "<div class='card c-um'><div class='card-label'>Umidade</div><div class='card-val'>" + String(umidade, 1) + "</div><div class='card-unit'>%</div></div>";
  h += "<div class='card c-ag'><div class='card-label'>Temp. Água</div><div class='card-val'>" + String(tempAgua, 1) + "</div><div class='card-unit'>°C</div></div>";
  h += "<div class='card c-cs'><div class='card-label'>Temp. Case</div><div class='card-val'>" + String(tempCase, 1) + "</div><div class='card-unit'>°C</div></div>";
  h += "</div>";

  // Painel Relatório
  h += "<div class='panel'><h2>Relatórios</h2>";
  h += "<div class='row'>";
  h += "<a href='/relatorio' style='text-decoration:none'><button class='btn btn-report'>📄 Gerar Relatório do Dia</button></a>";
  h += "<a href='/historico' style='text-decoration:none'><button class='btn btn-hist'>🗂 Histórico</button></a>";
  h += "</div></div>";

  // Painel intervalo
  h += "<div class='panel'><h2>Intervalo de medição</h2>";
  h += "<form method='GET' action='/intervalo' class='row'>";
  h += "<input type='number' name='min' min='1' max='1440' value='" + String(minutos) + "'>";
  h += "<span style='font-size:.8rem;color:var(--dim)'>minutos</span>";
  h += "<button class='btn btn-save' type='submit'>Salvar</button>";
  h += "</form></div>";

  // Painel acerto de hora
  h += "<div class='panel'><h2>Acerto de hora (RTC)</h2>";
  h += "<p style='font-size:.75rem;color:var(--dim);margin-bottom:10px'>Hora atual no RTC: <span style='color:var(--teal);font-family:monospace'>" + horaMed + " &nbsp; " + dataMed + "</span></p>";
  h += "<form method='GET' action='/acertarhora' class='row'>";
  h += "<input type='datetime-local' name='dt' style='background:var(--bg);border:1px solid var(--bdr);color:var(--txt);border-radius:8px;padding:8px 10px;font-size:.85rem;'>";
  h += "<button class='btn btn-save' type='submit'>Aplicar</button>";
  h += "</form></div>";

  // Painel modo
  h += "<div class='panel'><h2>Modo de rede</h2>";
  h += "<div class='row'>";
  h += "<span class='badge " + String(modoAtual == MODO_AP ? "badge-ap" : "badge-sta") + "'>" + modo + "</span>";
  h += "<form method='GET' action='/modo'>";
  h += "<button class='btn btn-modo' type='submit'>Alternar modo</button>";
  h += "</form></div></div>";

  h += "<p class='footer'>Última medição: " + horaMed + " &nbsp;|&nbsp; " + dataMed + "</p>";
  h += "</body></html>";

  return h;
}

// ── Página de relatório (HTML/PDF) ───────────────────────
String paginaRelatorio() {
  struct Estatisticas e = calcularEstatisticas(dataMed);
  String csv = lerCSVdoDia(dataMed);

  // Prepara dados para gráficos (arrays JS a partir do CSV)
  String labels     = "";
  String datasTar   = "";
  String datasUm    = "";
  String datasTw    = "";
  String datasTc    = "";

  int pos = 0, linha = 0;
  while (pos < (int)csv.length()) {
    int fim = csv.indexOf('\n', pos);
    if (fim == -1) fim = csv.length();
    String row = csv.substring(pos, fim);
    row.trim();
    pos = fim + 1;
    if (linha == 0 || row.length() == 0) { linha++; continue; }

    int c1 = row.indexOf(',');
    int c2 = row.indexOf(',', c1+1);
    int c3 = row.indexOf(',', c2+1);
    int c4 = row.indexOf(',', c3+1);
    if (c1<0||c2<0||c3<0||c4<0) { linha++; continue; }

    String hora = row.substring(0, c1);
    hora = hora.substring(0, 5); // HH:MM

    if (labels.length() > 0) { labels += ","; datasTar += ","; datasUm += ","; datasTw += ","; datasTc += ","; }
    labels   += "'" + hora + "'";
    datasTar  += row.substring(c1+1, c2);
    datasUm   += row.substring(c2+1, c3);
    datasTw   += row.substring(c3+1, c4);
    datasTc   += row.substring(c4+1);
    linha++;
  }

  String h = "<!DOCTYPE html><html lang='pt-BR'><head>";
  h += "<meta charset='UTF-8'>";
  h += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>Relatório — " + dataMed + "</title>";
  h += "<script src='https://cdn.jsdelivr.net/npm/chart.js@4.4.0/dist/chart.umd.min.js'></script>";
  h += "<style>";
  h += "@import url('https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;600&family=IBM+Plex+Sans:wght@400;600;700&display=swap');";
  h += ":root{--bg:#f4f6fa;--sur:#ffffff;--bdr:#dde3ef;--txt:#1a2035;--dim:#6b7a99;";
  h += "--blue:#1a6fd4;--teal:#12a068;--orange:#c96f10;--purple:#6b3dc9;--red:#c0392b;}";
  h += "*{box-sizing:border-box;margin:0;padding:0}";
  h += "body{background:var(--bg);color:var(--txt);font-family:'IBM Plex Sans',sans-serif;padding:32px 24px 60px;max-width:900px;margin:0 auto}";
  h += ".header{display:flex;justify-content:space-between;align-items:flex-start;margin-bottom:28px;padding-bottom:20px;border-bottom:2px solid var(--bdr)}";
  h += ".header-left h1{font-size:1.6rem;font-weight:700;letter-spacing:1px;color:var(--txt)}";
  h += ".header-left p{font-size:.8rem;color:var(--dim);margin-top:4px;font-family:'IBM Plex Mono',monospace}";
  h += ".header-right{text-align:right;font-size:.75rem;color:var(--dim);font-family:'IBM Plex Mono',monospace}";
  h += ".badge-pdf{display:inline-block;background:#1a6fd4;color:#fff;font-size:.65rem;font-weight:700;letter-spacing:2px;padding:3px 10px;border-radius:4px;text-transform:uppercase;margin-bottom:4px}";
  h += ".stats-grid{display:grid;grid-template-columns:repeat(4,1fr);gap:14px;margin-bottom:28px}";
  h += ".stat-card{background:var(--sur);border:1px solid var(--bdr);border-radius:10px;padding:16px 12px;text-align:center;border-top:3px solid}";
  h += ".stat-label{font-size:.62rem;text-transform:uppercase;letter-spacing:1px;color:var(--dim);margin-bottom:10px}";
  h += ".stat-row{display:flex;justify-content:space-between;font-size:.72rem;margin-bottom:4px}";
  h += ".stat-key{color:var(--dim)}";
  h += ".stat-val{font-weight:600;font-family:'IBM Plex Mono',monospace}";
  h += ".stat-avg{font-size:1.5rem;font-weight:700;font-family:'IBM Plex Mono',monospace;margin:6px 0}";
  h += ".c-ar{border-top-color:var(--blue)}.c-ar .stat-avg{color:var(--blue)}";
  h += ".c-um{border-top-color:var(--teal)}.c-um .stat-avg{color:var(--teal)}";
  h += ".c-ag{border-top-color:var(--orange)}.c-ag .stat-avg{color:var(--orange)}";
  h += ".c-cs{border-top-color:var(--purple)}.c-cs .stat-avg{color:var(--purple)}";
  h += ".chart-section{background:var(--sur);border:1px solid var(--bdr);border-radius:10px;padding:20px;margin-bottom:18px}";
  h += ".chart-title{font-size:.75rem;font-weight:700;text-transform:uppercase;letter-spacing:1px;color:var(--dim);margin-bottom:16px}";
  h += ".table-section{background:var(--sur);border:1px solid var(--bdr);border-radius:10px;padding:20px;margin-bottom:18px}";
  h += "table{width:100%;border-collapse:collapse;font-size:.78rem}";
  h += "th{font-size:.65rem;text-transform:uppercase;letter-spacing:1px;color:var(--dim);padding:8px 12px;text-align:left;border-bottom:2px solid var(--bdr)}";
  h += "td{padding:7px 12px;border-bottom:1px solid var(--bdr);font-family:'IBM Plex Mono',monospace;font-size:.8rem}";
  h += "tr:last-child td{border-bottom:none}";
  h += "tr:hover td{background:#f0f4fc}";
  h += ".footer-rep{font-size:.68rem;color:var(--dim);text-align:center;margin-top:16px}";
  h += ".btn-back{display:inline-block;padding:9px 18px;background:#1a2035;color:#fff;border-radius:8px;font-size:.82rem;text-decoration:none;font-weight:600;margin-bottom:20px}";
  h += "@media print{.btn-back,.no-print{display:none!important}.chart-section,.table-section,.stats-grid,.stat-card{break-inside:avoid}}";
  h += "</style></head><body>";

  h += "<a href='/' class='btn-back no-print'>← Voltar</a>";

  // Cabeçalho
  h += "<div class='header'>";
  h += "<div class='header-left'><span class='badge-pdf'>Relatório</span><h1>Protótipo IoT</h1>";
  h += "<p>ESP32 DevKit V1 · Gerado em " + horaMed + " · " + dataMed + "</p></div>";
  h += "<div class='header-right'>";
  h += "<p>Total de medições</p>";
  h += "<p style='font-size:2rem;font-weight:700;color:var(--txt)'>" + String(e.totalMedicoes) + "</p>";
  h += "<p>no dia " + dataMed + "</p>";
  h += "</div></div>";

  // Cards de estatísticas
  h += "<div class='stats-grid'>";

  // Temp Ar
  h += "<div class='stat-card c-ar'><div class='stat-label'>Temp. Ar</div>";
  h += "<div class='stat-avg'>" + String(e.medTempAr, 1) + "°C</div>";
  h += "<div class='stat-row'><span class='stat-key'>Mín</span><span class='stat-val'>" + String(e.minTempAr, 1) + "°C</span></div>";
  h += "<div class='stat-row'><span class='stat-key'>Máx</span><span class='stat-val'>" + String(e.maxTempAr, 1) + "°C</span></div></div>";

  // Umidade
  h += "<div class='stat-card c-um'><div class='stat-label'>Umidade</div>";
  h += "<div class='stat-avg'>" + String(e.medUmidade, 1) + "%</div>";
  h += "<div class='stat-row'><span class='stat-key'>Mín</span><span class='stat-val'>" + String(e.minUmidade, 1) + "%</span></div>";
  h += "<div class='stat-row'><span class='stat-key'>Máx</span><span class='stat-val'>" + String(e.maxUmidade, 1) + "%</span></div></div>";

  // Temp Água
  h += "<div class='stat-card c-ag'><div class='stat-label'>Temp. Água</div>";
  h += "<div class='stat-avg'>" + String(e.medTempAgua, 1) + "°C</div>";
  h += "<div class='stat-row'><span class='stat-key'>Mín</span><span class='stat-val'>" + String(e.minTempAgua, 1) + "°C</span></div>";
  h += "<div class='stat-row'><span class='stat-key'>Máx</span><span class='stat-val'>" + String(e.maxTempAgua, 1) + "°C</span></div></div>";

  // Temp Case
  h += "<div class='stat-card c-cs'><div class='stat-label'>Temp. Case</div>";
  h += "<div class='stat-avg'>" + String(e.medTempCase, 1) + "°C</div>";
  h += "<div class='stat-row'><span class='stat-key'>Mín</span><span class='stat-val'>" + String(e.minTempCase, 1) + "°C</span></div>";
  h += "<div class='stat-row'><span class='stat-key'>Máx</span><span class='stat-val'>" + String(e.maxTempCase, 1) + "°C</span></div></div>";

  h += "</div>"; // stats-grid

  // ── Gráfico: Temperaturas ─────────────────────────────
  h += "<div class='chart-section'>";
  h += "<p class='chart-title'>Temperaturas ao longo do dia</p>";
  h += "<canvas id='chartTemp' height='90'></canvas></div>";

  // ── Gráfico: Umidade ─────────────────────────────────
  h += "<div class='chart-section'>";
  h += "<p class='chart-title'>Umidade relativa do ar</p>";
  h += "<canvas id='chartUm' height='70'></canvas></div>";

  // ── Tabela de medições ───────────────────────────────
  h += "<div class='table-section'>";
  h += "<p class='chart-title' style='margin-bottom:14px'>Todas as medições</p>";
  h += "<table><thead><tr><th>Horário</th><th>Temp. Ar</th><th>Umidade</th><th>Temp. Água</th><th>Temp. Case</th></tr></thead><tbody>";

  // Preenche tabela a partir do CSV
  pos = 0; linha = 0;
  while (pos < (int)csv.length()) {
    int fim = csv.indexOf('\n', pos);
    if (fim == -1) fim = csv.length();
    String row = csv.substring(pos, fim);
    row.trim();
    pos = fim + 1;
    if (linha == 0 || row.length() == 0) { linha++; continue; }

    int c1 = row.indexOf(',');
    int c2 = row.indexOf(',', c1+1);
    int c3 = row.indexOf(',', c2+1);
    int c4 = row.indexOf(',', c3+1);
    if (c1<0||c2<0||c3<0||c4<0) { linha++; continue; }

    h += "<tr>";
    h += "<td>" + row.substring(0, c1) + "</td>";
    h += "<td>" + row.substring(c1+1, c2) + " °C</td>";
    h += "<td>" + row.substring(c2+1, c3) + " %</td>";
    h += "<td>" + row.substring(c3+1, c4) + " °C</td>";
    h += "<td>" + row.substring(c4+1)      + " °C</td>";
    h += "</tr>";
    linha++;
  }

  h += "</tbody></table></div>";

  // Rodapé
  h += "<p class='footer-rep'>Protótipo IoT · ESP32 DevKit V1 · " + dataMed + " · ";
  h += String(e.totalMedicoes) + " medições registradas</p>";

  // ── Scripts dos gráficos ─────────────────────────────
  h += "<script>";
  h += "const labels=[" + labels + "];";
  h += "const tar=[" + datasTar + "];";
  h += "const um=[" + datasUm + "];";
  h += "const tw=[" + datasTw + "];";
  h += "const tc=[" + datasTc + "];";

  // Gráfico temperaturas (linha)
  h += "new Chart(document.getElementById('chartTemp'),{type:'line',data:{labels,datasets:[";
  h += "{label:'Temp. Ar (°C)',data:tar,borderColor:'#1a6fd4',backgroundColor:'rgba(26,111,212,0.08)',tension:0.3,pointRadius:2},";
  h += "{label:'Temp. Água (°C)',data:tw,borderColor:'#c96f10',backgroundColor:'rgba(201,111,16,0.08)',tension:0.3,pointRadius:2},";
  h += "{label:'Temp. Case (°C)',data:tc,borderColor:'#6b3dc9',backgroundColor:'rgba(107,61,201,0.08)',tension:0.3,pointRadius:2}";
  h += "]},options:{responsive:true,plugins:{legend:{position:'bottom'}},scales:{y:{grid:{color:'#e8edf5'},ticks:{color:'#6b7a99'}},x:{ticks:{color:'#6b7a99',maxTicksLimit:12},grid:{color:'#e8edf5'}}}}});";

  // Gráfico umidade (área)
  h += "new Chart(document.getElementById('chartUm'),{type:'line',data:{labels,datasets:[";
  h += "{label:'Umidade (%)',data:um,borderColor:'#12a068',backgroundColor:'rgba(18,160,104,0.12)',fill:true,tension:0.3,pointRadius:2}";
  h += "]},options:{responsive:true,plugins:{legend:{position:'bottom'}},scales:{y:{min:0,max:100,grid:{color:'#e8edf5'},ticks:{color:'#6b7a99'}},x:{ticks:{color:'#6b7a99',maxTicksLimit:12},grid:{color:'#e8edf5'}}}}});";

  h += "</script>";
  h += "</body></html>";

  return h;
}

// ── Página de histórico ──────────────────────────────────
String paginaHistorico() {
  String h = "<!DOCTYPE html><html lang='pt-BR'><head>";
  h += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width,initial-scale=1'>";
  h += "<title>Histórico — Protótipo IoT</title>";
  h += "<style>";
  h += ":root{--bg:#0b0f18;--sur:#131929;--bdr:#1e2a40;--txt:#c8d4e8;--dim:#5a6a84;--teal:#2cb67d;--blue:#4a9eff;}";
  h += "*{box-sizing:border-box;margin:0;padding:0}";
  h += "body{background:var(--bg);color:var(--txt);font-family:sans-serif;padding:24px 16px 48px;max-width:500px;margin:0 auto}";
  h += "h1{font-size:1.2rem;letter-spacing:2px;text-transform:uppercase;color:#f0f4ff;margin-bottom:20px;text-align:center}";
  h += ".btn-back{display:inline-block;padding:8px 14px;background:var(--sur);color:var(--txt);border-radius:8px;font-size:.8rem;text-decoration:none;margin-bottom:18px;border:1px solid var(--bdr)}";
  h += ".item{background:var(--sur);border:1px solid var(--bdr);border-radius:10px;padding:14px 16px;display:flex;justify-content:space-between;align-items:center;margin-bottom:10px}";
  h += ".item-date{font-family:monospace;font-size:.9rem;color:var(--teal)}";
  h += ".item-link{font-size:.78rem;color:var(--blue);text-decoration:none}";
  h += ".empty{text-align:center;color:var(--dim);margin-top:40px;font-size:.85rem}";
  h += "</style></head><body>";

  h += "<a href='/' class='btn-back'>← Voltar</a>";
  h += "<h1>Histórico</h1>";

  if (!sdDisponivel) {
    h += "<p class='empty'>SD Card não disponível.</p>";
    h += "</body></html>";
    return h;
  }

  // Lista arquivos em /relatorios
  File dir = SD.open("/relatorios");
  bool achouAlgum = false;

  if (dir) {
    while (true) {
      File entry = dir.openNextFile();
      if (!entry) break;
      String nome = String(entry.name());
      entry.close();
      if (nome.endsWith(".html")) {
        String data = nome.substring(0, nome.length() - 5);
        // remove path prefix se houver
        int slash = data.lastIndexOf('/');
        if (slash >= 0) data = data.substring(slash + 1);
        h += "<div class='item'>";
        h += "<span class='item-date'>" + data + "</span>";
        h += "<a href='/relatorio?data=" + data + "' class='item-link'>Ver relatório →</a>";
        h += "</div>";
        achouAlgum = true;
      }
    }
    dir.close();
  }

  if (!achouAlgum) {
    h += "<p class='empty'>Nenhum relatório salvo ainda.<br>Gere o primeiro pelo botão na página principal.</p>";
  }

  h += "</body></html>";
  return h;
}

// ── Rotas HTTP ───────────────────────────────────────────
void registrarRotas() {

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", paginaHTML());
  });

  server.on("/intervalo", HTTP_GET, []() {
    if (server.hasArg("min")) {
      uint32_t novoMin = server.arg("min").toInt();
      if (novoMin >= 1 && novoMin <= 1440) {
        intervaloMs = novoMin * 60000UL;
      }
    }
    server.sendHeader("Location", "/");
    server.send(303);
  });

  server.on("/modo", HTTP_GET, []() {
    server.send(200, "text/html",
      "<html><body style='background:#0b0f18;color:#c8d4e8;font-family:sans-serif;display:flex;"
      "align-items:center;justify-content:center;height:100vh;'>"
      "<p>Alternando modo... aguarde 5 segundos e reconecte.</p></body></html>");
    delay(500);
    alternarModo();
  });

  // Relatório: /relatorio ou /relatorio?data=YYYY-MM-DD
  server.on("/relatorio", HTTP_GET, []() {
    String data = server.hasArg("data") ? server.arg("data") : dataMed;

    // Tenta ler do SD se não for o dia atual
    if (data != dataMed && sdDisponivel) {
      String arquivo = "/relatorios/" + data + ".html";
      if (SD.exists(arquivo)) {
        File f = SD.open(arquivo, FILE_READ);
        if (f) {
          String conteudo = "";
          while (f.available()) conteudo += (char)f.read();
          f.close();
          server.send(200, "text/html", conteudo);
          return;
        }
      }
    }

    // Gera relatório atual, salva e serve
    gerarRelatorioSD();
    server.send(200, "text/html", paginaRelatorio());
  });

  server.on("/acertarhora", HTTP_GET, []() {
    // Espera: dt=YYYY-MM-DDTHH:MM  (formato datetime-local do HTML)
    if (server.hasArg("dt")) {
      String dt = server.arg("dt"); // ex: "2025-01-15T14:30"
      if (dt.length() >= 16) {
        int ano  = dt.substring(0, 4).toInt();
        int mes  = dt.substring(5, 7).toInt();
        int dia  = dt.substring(8, 10).toInt();
        int hora = dt.substring(11, 13).toInt();
        int min  = dt.substring(14, 16).toInt();
        if (ano >= 2024 && mes >= 1 && mes <= 12 && dia >= 1 && dia <= 31) {
          rtc.adjust(DateTime(ano, mes, dia, hora, min, 0));
          Serial.println("RTC ajustado para: " + dt);
        }
      }
    }
    server.sendHeader("Location", "/");
    server.send(303);
  });

  // Histórico de relatórios
  server.on("/historico", HTTP_GET, []() {
    server.send(200, "text/html", paginaHistorico());
  });

  // Download CSV do dia
  server.on("/csv", HTTP_GET, []() {
    String data = server.hasArg("data") ? server.arg("data") : dataMed;
    String csv = lerCSVdoDia(data);
    if (csv.length() == 0) {
      server.send(404, "text/plain", "Nenhum dado para essa data.");
      return;
    }
    server.sendHeader("Content-Disposition", "attachment; filename=\"medicoes-" + data + ".csv\"");
    server.send(200, "text/csv", csv);
  });

  server.on("/dados", HTTP_GET, []() {
    String j = "{";
    j += "\"tempAr\":"   + String(tempAr, 1) + ",";
    j += "\"umidade\":"  + String(umidade, 1) + ",";
    j += "\"tempAgua\":" + String(tempAgua, 1) + ",";
    j += "\"tempCase\":" + String(tempCase, 1) + ",";
    j += "\"horaMed\":\"" + horaMed + "\",";
    j += "\"dataMed\":\"" + dataMed + "\",";
    j += "\"modo\":\""   + String(modoAtual == MODO_AP ? "AP" : "STA") + "\",";
    j += "\"sdOk\":"     + String(sdDisponivel ? "true" : "false");
    j += "}";
    server.send(200, "application/json", j);
  });

  server.onNotFound([]() {
    server.sendHeader("Location", "/");
    server.send(303);
  });
}

// ── Medição ──────────────────────────────────────────────
void fazerMedicao() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();

  if (!isnan(t)) tempAr  = t;
  if (!isnan(h)) umidade = h;

  ds18.requestTemperatures();
  float tw = ds18.getTempCByIndex(0);
  if (tw != DEVICE_DISCONNECTED_C) tempAgua = tw;

  tempCase = rtc.getTemperature();

  DateTime now = rtc.now();
  char bufH[9], bufD[11];
  sprintf(bufH, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
  sprintf(bufD, "%04d-%02d-%02d", now.year(), now.month(), now.day());
  horaMed = String(bufH);
  dataMed = String(bufD);

  ultimaMedMs = millis();

  // Relatório automático ao virar o dia (meia-noite)
  if (diaUltimoRelatorio != now.day() && now.hour() == 0 && now.minute() == 0) {
    // Gera relatorio do dia que acabou (ontem)
    gerarRelatorioSD();
    diaUltimoRelatorio = now.day();
    if (modoAtual == MODO_STA && WiFi.status() == WL_CONNECTED) {
      // Notifica Telegram sobre relatório diário
      enviarMensagemTelegram("🌙 *Relatório diário automático gerado!*\nUse /relatorio para receber.");
    }
  }

  salvarMedicaoSD();
  mostrarTela();
}

// ── LCD ──────────────────────────────────────────────────
void mostrarTela() {
  lcd.clear();

  lcd.setCursor(15, 0);
  lcd.print("[");
  lcd.print(telaAtual + 1);
  lcd.print("/3]");

  switch (telaAtual) {
    case 0:
      lcd.setCursor(0, 0); lcd.print("Temp. Ar:");
      lcd.setCursor(0, 1); lcd.print(tempAr, 1); lcd.print(" C");
      lcd.setCursor(0, 2); lcd.print("Umidade:");
      lcd.setCursor(0, 3); lcd.print(umidade, 1); lcd.print(" %");
      break;
    case 1:
      lcd.setCursor(0, 0); lcd.print("Temp. Agua:");
      lcd.setCursor(0, 1); lcd.print(tempAgua, 1); lcd.print(" C");
      lcd.setCursor(0, 2); lcd.print("Temp. Case:");
      lcd.setCursor(0, 3); lcd.print(tempCase, 1); lcd.print(" C");
      break;
    case 2:
      lcd.setCursor(0, 0); lcd.print("Ult. medicao:");
      lcd.setCursor(0, 1); lcd.print(horaMed);
      lcd.setCursor(0, 2); lcd.print("Status SD:");
      lcd.setCursor(0, 3); lcd.print(sdDisponivel ? "OK" : "FALHA");
      break;
  }
}

// ── Botão (push button) ──────────────────────────────────
void lerTouch() {
  static uint32_t ultimoDebounce = 0;

  bool estadoAtual = !digitalRead(TOUCH_BTN);   // pull-up: pressionado = LOW, então inverte
  uint32_t agora = millis();

  if (agora - ultimoDebounce < debounceTouch) return;

  if (estadoAnterior == LOW && estadoAtual == HIGH) {
    tempoPressionado = agora;
    ultimoDebounce = agora;
  }

  if (estadoAnterior == HIGH && estadoAtual == LOW) {
    uint32_t duracao = agora - tempoPressionado;

    if (duracao >= tempoLongo) {
      telaAtual = (telaAtual + NUM_TELAS - 1) % NUM_TELAS;
      mostrarTela();
      aguardandoSegundoToque = false;
    } else {
      if (aguardandoSegundoToque && (agora - ultimoClique <= tempoDuplo)) {
        fazerMedicao();
        mostrarTela();
        aguardandoSegundoToque = false;
      } else {
        aguardandoSegundoToque = true;
        ultimoClique = agora;
      }
    }
    ultimoDebounce = agora;
  }

  if (aguardandoSegundoToque && (agora - ultimoClique > tempoDuplo)) {
    telaAtual = (telaAtual + 1) % NUM_TELAS;
    mostrarTela();
    aguardandoSegundoToque = false;
  }

  estadoAnterior = estadoAtual;
}

// ── Setup ────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);

  pinMode(BTN_MODO, INPUT_PULLUP);
  pinMode(TOUCH_BTN, INPUT_PULLUP);

  Wire.begin(21, 22);

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("  Protótipo IoT  ");
  lcd.setCursor(0, 1); lcd.print(" Iniciando...  ");

  dht.begin();
  ds18.begin();

  if (!rtc.begin()) {
    Serial.println("RTC nao encontrado!");
  }
  if (rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  // Inicia SD Card (SCK=18, MISO=19, MOSI=23, CS=5)
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Iniciando SD...");
  SPI.begin(18, 19, 23, SD_CS);   // SCK, MISO, MOSI, CS
  if (!SD.begin(SD_CS, SPI, 1000000)) {
    Serial.println("SD Card: falha ou nao presente.");
    sdDisponivel = false;
    lcd.setCursor(0, 1); lcd.print("SD: FALHA");
  } else {
    Serial.println("SD Card OK.");
    sdDisponivel = true;
    garantirDiretorio("/dados");
    garantirDiretorio("/relatorios");
    lcd.setCursor(0, 1); lcd.print("SD: OK");
  }
  delay(800);

  delay(1500);

  iniciarAP();
  registrarRotas();
  server.begin();

  Serial.println("Servidor HTTP iniciado!");

  fazerMedicao();
}

// ── Loop ─────────────────────────────────────────────────
void loop() {
  server.handleClient();

  // Botão físico modo
  bool btnAtual = digitalRead(BTN_MODO);
  if (btnAnterior == HIGH && btnAtual == LOW) {
    delay(50);
    if (digitalRead(BTN_MODO) == LOW) {
      alternarModo();
    }
  }
  btnAnterior = btnAtual;

  // Medição periódica
  if (millis() - ultimaMedMs >= intervaloMs) {
    fazerMedicao();
  }

  // Botão de navegação
  lerTouch();

  // Polling Telegram (apenas em modo STA com Wi-Fi)
  if (modoAtual == MODO_STA && WiFi.status() == WL_CONNECTED) {
    if (millis() - ultimoTelegramMs >= intervaloTelegram) {
      ultimoTelegramMs = millis();
      verificarTelegram();
    }
  }
}
