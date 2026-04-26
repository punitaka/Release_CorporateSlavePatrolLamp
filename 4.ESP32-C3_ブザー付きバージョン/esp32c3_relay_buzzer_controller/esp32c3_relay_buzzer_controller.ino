/*
  Exchange Online Alert Relay & Buzzer Controller for ESP32-C3
  Grove - Relay & Grove - Passive Buzzer制御プログラム

  機能：
  - Wi-Fi接続
  - Exchange Online REST APIをポーリング
  - 前回のチェック時刻以降に新たに受信したメールをチェック
  - リレーをON（3分間継続）
  - ブザーを0.5秒ON/1.5秒OFFで60秒継続
  - 自動的にOFF
  - メールを既読にしない（複数台の機材で同じメールを処理可能）

  対応ボード：Seeed Studio XIAO ESP32-C3
  Grove Shield：Seeeduino XIAO用Grove シールド
  リレー接続：A0ポート（GPIO 5）
  ブザー接続：A1ポート（GPIO 3）

  必要なライブラリ：
  - WiFi.h（ESP32標準）
  - HTTPClient.h（ESP32標準）
  - ArduinoJson.h（Arduino JSON Library）
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// ===== 設定 =====
const char* SSID = "★ここにWifiのSSIDを設定★";              // Wi-Fi SSID（設定してください）
const char* PASSWORD = "★ここにWifiのパスワードを設定★";          // Wi-Fi パスワード（設定してください）

// Microsoft Entra ID認証情報
const char* TENANT_ID = "★ここにExchangeのテナントIDを設定★";         // テナントID（設定してください）
const char* CLIENT_ID = "★ここにExchangeのクライアントIDを設定★";         // クライアントID（設定してください）
const char* CLIENT_SECRET = "★ここにExchangeのクライアントシークレットを設定★";     // クライアントシークレット（設定してください）
const char* MAILBOX_ADDRESS = "xxxxx@XXXXX.XXXXX"; // 監視対象メールアドレス（設定してください）

// リレー・ブザー設定
const int RELAY_PIN = 2;           // A0ポート（GPIO 2）- Grove - Relay接続
const int BUZZER_PIN = 3;          // A1ポート（GPIO 3）- Grove - Passive Buzzer接続
const int CHECK_INTERVAL = 300000;  // メール受信チェック間隔（ミリ秒）：5分 = 300000ms
const int RELAY_ON_DURATION = 180000; // リレーON継続時間（ミリ秒）：3分 = 180000ms
const int BUZZER_DURATION = 60000;   // ブザー鳴動時間（ミリ秒）：60秒 = 60000ms
const int BUZZER_ON_TIME = 500;      // ブザーON時間（ミリ秒）：0.5秒 = 500ms
const int BUZZER_OFF_TIME = 1500;    // ブザーOFF時間（ミリ秒）：1.5秒 = 1500ms
const int BUZZER_FREQUENCY = 2700;   // ブザー周波数（Hz）：共振周波数
const char* ALERT_KEYWORD = ""; // 検出キーワード
const int STARTUP_DELAY = 20000;    // 起動直後のスリープ時間（ミリ秒）：20秒 = 20000ms

// ===== グローバル変数 =====
unsigned long relay_on_time = 0;    // リレーON開始時刻
bool relay_active = false;          // リレーアクティブフラグ
unsigned long buzzer_on_time = 0;   // ブザーON開始時刻
bool buzzer_active = false;         // ブザーアクティブフラグ
bool startup_complete = false;      // 起動完了フラグ
unsigned long last_check_time = 0;  // 最後にメール受信をチェックした時刻
unsigned long last_relay_check_time = 0; // 最後にリレーをチェックした時刻
unsigned long last_buzzer_check_time = 0; // 最後にブザーをチェックした時刻
String access_token = "";           // アクセストークン
unsigned long token_expiry_time = 0; // トークン有効期限
time_t last_email_check_timestamp = 0; // 前回のメール受信チェック時刻（Unix timestamp）

// ===== 関数プロトタイプ =====
void setup_wifi();
void setup_relay_buzzer();
void set_relay(bool state);
void manage_buzzer();
void check_emails();
void manage_relay_timer();
bool get_access_token();
String make_graph_api_request(String endpoint);
bool contains_keyword(String text, String keyword);
String get_current_timestamp();
void buzzer_tone(int frequency, int duration);

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n");
  Serial.println("============================================================");
  Serial.println("Exchange Online Alert Relay & Buzzer Controller for ESP32-C3");
  Serial.println("============================================================");
  Serial.print("Mailbox: ");
  Serial.println(MAILBOX_ADDRESS);
  Serial.print("Alert Keyword: ");
  Serial.println(ALERT_KEYWORD);
  Serial.println("============================================================");
  
  // GPIO設定
  setup_relay_buzzer();
  
  // Wi-Fi接続
  setup_wifi();
  
  // 時刻同期（NTP）
  Serial.println("NTPサーバーから時刻を同期しています...");
  configTime(9 * 3600, 0, "ntp.nict.jp", "time.google.com");
  delay(2000);
  
  // 起動直後のスリープ
  Serial.println("起動直後の初期化スリープを開始します（20秒）");
  delay(STARTUP_DELAY);
  startup_complete = true;
  Serial.println("起動直後の初期化スリープが完了しました。通常動作を開始します");

  // 起動直後にパトランプを5秒稼働
  Serial.println("起動直後にパトランプを5秒稼働");
  set_relay(true);
  delay(5000);
  set_relay(false);
  
  last_check_time = millis();
  last_relay_check_time = millis();
  last_buzzer_check_time = millis();
  
  // 現在時刻を取得（初回チェック用）
  time_t now = time(nullptr);
  last_email_check_timestamp = now;

  // 起動直後にまずメールチェック
  check_emails();
}

void loop() {
  unsigned long current_time = millis();
  
  // メール受信チェック（5分ごと）
  if (current_time - last_check_time >= CHECK_INTERVAL) {
    check_emails();
    last_check_time = current_time;
  }
  
  // リレータイマー管理（10秒ごと）
  if (current_time - last_relay_check_time >= 10000) {
    manage_relay_timer();
    last_relay_check_time = current_time;
  }
  
  // ブザータイマー管理（50ミリ秒ごと）
  if (current_time - last_buzzer_check_time >= 50) {
    manage_buzzer();
    last_buzzer_check_time = current_time;
  }
  
  delay(10); // CPU負荷軽減
}

void setup_wifi() {
  Serial.print("Wi-Fi接続中: ");
  Serial.println(SSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.begin(SSID, PASSWORD);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi接続成功");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWi-Fi接続失敗");
  }
}

void setup_relay_buzzer() {
  // リレー設定
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // 起動時はリレーをOFF状態に初期化
  Serial.print("GPIO ");
  Serial.print(RELAY_PIN);
  Serial.println(" (A0 / D0) を初期化しました（リレーOFF状態）");
  
  // ブザー設定
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW); // 起動時はブザーをOFF状態に初期化
  Serial.print("GPIO ");
  Serial.print(BUZZER_PIN);
  Serial.println(" (A1 / D1) を初期化しました（ブザーOFF状態）");
}

void set_relay(bool state) {
  if (state) {
    digitalWrite(RELAY_PIN, HIGH);
    relay_active = true;
    Serial.println("リレーをONにしました");
  } else {
    digitalWrite(RELAY_PIN, LOW);
    relay_active = false;
    Serial.println("リレーをOFFにしました");
  }
}

void buzzer_tone(int frequency, int duration) {
  // PWM周波数でブザーを鳴らす
  int cycle = 1000000 / frequency; // マイクロ秒単位のサイクル時間
  unsigned long end_time = micros() + (duration * 1000); // ミリ秒をマイクロ秒に変換
  
  while (micros() < end_time) {
    digitalWrite(BUZZER_PIN, HIGH);
    delayMicroseconds(cycle / 2);
    digitalWrite(BUZZER_PIN, LOW);
    delayMicroseconds(cycle / 2);
  }
}

void manage_buzzer() {
  if (buzzer_active && buzzer_on_time > 0 && startup_complete) {
    unsigned long elapsed_time = millis() - buzzer_on_time;
    
    // ブザー鳴動時間に達したか確認
    if (elapsed_time >= BUZZER_DURATION) {
      Serial.print("ブザー鳴動時間（");
      Serial.print(BUZZER_DURATION / 1000);
      Serial.println("秒）に達しました");
      digitalWrite(BUZZER_PIN, LOW); // ブザーOFF
      buzzer_active = false;
      buzzer_on_time = 0;
      return;
    }
    
    // 0.5秒ON/1.5秒OFFのパターンを実行
    unsigned long pattern_time = elapsed_time % (BUZZER_ON_TIME + BUZZER_OFF_TIME);
    
    if (pattern_time < BUZZER_ON_TIME) {
      // ブザーON期間
      digitalWrite(BUZZER_PIN, HIGH);
    } else {
      // ブザーOFF期間
      digitalWrite(BUZZER_PIN, LOW);
    }
  }
}

bool get_access_token() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fiが接続されていません");
    return false;
  }
  
  // トークンがまだ有効な場合はスキップ
  if (access_token.length() > 0 && millis() < token_expiry_time) {
    Serial.println("既存のアクセストークンを使用します");
    return true;
  }
  
  HTTPClient http;
  String url = "https://login.microsoftonline.com/";
  url += TENANT_ID;
  url += "/oauth2/v2.0/token";
  
  String payload = "client_id=";
  payload += CLIENT_ID;
  payload += "&scope=https://graph.microsoft.com/.default";
  payload += "&client_secret=";
  payload += CLIENT_SECRET;
  payload += "&grant_type=client_credentials";
  
  Serial.println("アクセストークンを取得しています...");
  
  http.begin(url);
  http.addHeader("Content-Type", "application/x-www-form-urlencoded");
  
  int httpCode = http.POST(payload);
  
  if (httpCode == 200) {
    String response = http.getString();
    
    // JSON解析
    StaticJsonDocument<1024> doc;
    DeserializationError error = deserializeJson(doc, response);
    
    if (!error) {
      access_token = doc["access_token"].as<String>();
      int expires_in = doc["expires_in"].as<int>();
      token_expiry_time = millis() + (expires_in * 1000) - 60000; // 1分前にリフレッシュ
      
      Serial.println("アクセストークンを取得しました");
      return true;
    } else {
      Serial.println("JSON解析エラー");
      return false;
    }
  } else {
    Serial.print("トークン取得エラー: ");
    Serial.println(httpCode);
    return false;
  }
  
  http.end();
}

String make_graph_api_request(String endpoint) {
  if (!get_access_token()) {
    return "";
  }
  
  HTTPClient http;
  String url = "https://graph.microsoft.com/v1.0";
  url += endpoint;
  
  // デバッグ: リクエストURLを表示
  Serial.print("リクエストURL: " );
  Serial.println(url);
  
  // WiFiClientSecureを使用してSSL接続を確立
  http.begin(url );
  http.addHeader("Authorization", "Bearer " + access_token );
  // Content-Typeヘッダーを削除（GET リクエストでは不要）
  
  int httpCode = http.GET( );
  
  if (httpCode == 200 ) {
    String response = http.getString( );
    http.end( );
    return response;
  } else {
    String errorResponse = http.getString( );
    Serial.print("Graph API エラー: ");
    Serial.println(httpCode );
    Serial.print("エラーレスポンス: ");
    Serial.println(errorResponse);
    http.end( );
    return "";
  }
}

String get_current_timestamp() {
  // 現在時刻をISO 8601形式で取得（UTC）
  time_t now = time(nullptr);
  struct tm* timeinfo = gmtime(&now);
  
  char buffer[30];
  strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", timeinfo);
  
  return String(buffer);
}

void check_emails() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fiが接続されていません");
    return;
  }
  
  Serial.println("メール受信チェックを実行しています...");
  
  // 前回のチェック時刻以降に受信したメールをフィルタ
  // 受信日時（receivedDateTime）が前回のチェック時刻以降のメールを取得
  struct tm* timeinfo = gmtime(&last_email_check_timestamp);
  char timestamp_buffer[30];
  strftime(timestamp_buffer, sizeof(timestamp_buffer), "%Y-%m-%dT%H:%M:%SZ", timeinfo);
  
  String filter = "receivedDateTime%20gt%20";
  filter += timestamp_buffer;
  
  // エンドポイント構築
  String endpoint = "/users/";
  endpoint += MAILBOX_ADDRESS;
  endpoint += "/messages?%24filter=";
  endpoint += filter;
  endpoint += "&%24select=subject,bodyPreview,from,receivedDateTime&%24top=10&%24orderby=receivedDateTime%20desc";
  
  String response = make_graph_api_request(endpoint);
  
  if (response.length() == 0) {
    Serial.println("メール取得失敗");
    return;
  }
  
  // JSON解析
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, response);
  
  if (error) {
    Serial.println("JSON解析エラー");
    return;
  }
  
  JsonArray messages = doc["value"].as<JsonArray>();
  
  if (messages.size() == 0) {
    Serial.println("新しいメールはありません");
    // 現在時刻を更新
    last_email_check_timestamp = time(nullptr);
    return;
  }
  
  Serial.print(messages.size());
  Serial.println("件の新しいメールを検出しました");
  
  // メールをチェック
  for (JsonObject message : messages) {
    String subject = message["subject"].as<String>();
    String bodyPreview = message["bodyPreview"].as<String>();
    String from = message["from"]["emailAddress"]["address"].as<String>();
    String receivedDateTime = message["receivedDateTime"].as<String>();
    
    Serial.print("メール受信 - From: ");
    Serial.print(from);
    Serial.print(", Subject: ");
    Serial.print(subject);
    Serial.print(", ReceivedTime: ");
    Serial.println(receivedDateTime);
    
    // キーワード検出（キーワードが空の場合はすべてのメールを対象）
    bool should_activate_relay = false;
    
    if (ALERT_KEYWORD[0] == '\0') {
      // キーワードが空の場合：すべての新規メールでリレーON
      should_activate_relay = true;
      Serial.print("メールを検出しました（キーワード無指定）: ");
      Serial.println(subject);
    } else {
      // キーワードが指定されている場合：キーワード検出
      if (contains_keyword(subject, ALERT_KEYWORD) || contains_keyword(bodyPreview, ALERT_KEYWORD)) {
        should_activate_relay = true;
        Serial.print("ALERTメールを検出しました: ");
        Serial.println(subject);
      }
    }
    
    // リレーON
    if (should_activate_relay) {
      if (!relay_active && startup_complete) {
        set_relay(true);
        relay_on_time = millis();
        Serial.print("リレーON開始時刻: ");
        Serial.println(relay_on_time);
        
        // ブザーON
        buzzer_active = true;
        buzzer_on_time = millis();
        Serial.print("ブザーON開始時刻: ");
        Serial.println(buzzer_on_time);
        Serial.println("ブザーを0.5秒ON/1.5秒OFFで60秒鳴動開始");
      } else if (relay_active) {
        Serial.println("リレーは既にON状態です。タイマーはリセットしません");
        Serial.println("ブザーも既にON状態です。タイマーはリセットしません");
      }
    }
  }
  
  // 現在時刻を更新（次回のチェック用）
  last_email_check_timestamp = time(nullptr);
}

void manage_relay_timer() {
  if (relay_active && relay_on_time > 0 && startup_complete) {
    unsigned long elapsed_time = millis() - relay_on_time;
    
    if (elapsed_time >= RELAY_ON_DURATION) {
      Serial.print("リレーON時間（");
      Serial.print(RELAY_ON_DURATION / 1000);
      Serial.println("秒）に達しました");
      set_relay(false);
      relay_on_time = 0;
    }
  }
}

bool contains_keyword(String text, String keyword) {
  if (text.length() == 0 || keyword.length() == 0) {
    return false;
  }
  
  String text_upper = text;
  String keyword_upper = keyword;
  
  // 大文字に変換
  text_upper.toUpperCase();
  keyword_upper.toUpperCase();
  
  return text_upper.indexOf(keyword_upper) >= 0;
}
