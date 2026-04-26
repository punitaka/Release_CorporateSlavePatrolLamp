/*
  Exchange Online Alert Relay & Buzzer Melody Controller for ESP32-C3
  Exchange Online REST APIとGrove - Relay & Grove - Passive Buzzer制御プログラム

  機能：
  - Wi-Fi接続
  - Exchange Online REST APIをポーリング
  - 前回のチェック時刻以降に新たに受信したメールをチェック
  - リレーをON（3分間継続）
  - ブザーでメロディを再生（60秒継続）
  - 自動的にOFF
  - メールを既読にしない（複数台の機材で同じメールを処理可能）

  対応ボード：Seeed Studio XIAO ESP32-C3
  Grove Shield：Seeeduino XIAO用Grove シールド
  リレー接続：A0ポート（GPIO 2）
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

// ===== 音符定義（Seeed Studio公式ドキュメント参照） =====
#define NOTE_D0 0
#define NOTE_D1 294
#define NOTE_D2 330
#define NOTE_D3 350
#define NOTE_D4 393
#define NOTE_D5 441
#define NOTE_D6 495
#define NOTE_D7 556

#define NOTE_DL1 147
#define NOTE_DL2 165
#define NOTE_DL3 175
#define NOTE_DL4 196
#define NOTE_DL5 221
#define NOTE_DL6 248
#define NOTE_DL7 278

#define NOTE_DH1 589
#define NOTE_DH2 661
#define NOTE_DH3 700
#define NOTE_DH4 786
#define NOTE_DH5 882
#define NOTE_DH6 990
#define NOTE_DH7 1112

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
const int MELODY_SPEED = 400;        // メロディ速度（ミリ秒）：小さいほど速い
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
String access_token = "";           // アクセストークン
unsigned long token_expiry_time = 0; // トークン有効期限
time_t last_email_check_timestamp = 0; // 前回のメール受信チェック時刻（Unix timestamp）
int melody_index = 0;               // メロディ再生位置
unsigned long last_note_time = 0;   // 最後に音符を再生した時刻

// ===== アラート用メロディ定義（ファミマ入店音） =====
// 参考: https://everykalax.hateblo.jp/entry/2021/08/09/125816
// 音符の周波数（Hz）：
//   NOTE_FS4 = 370 Hz（F#4）
//   NOTE_D4  = 294 Hz（D4）  ※ここではD4の標準値 293.66 Hz ≒ 294 Hz
//   NOTE_FS3 = 185 Hz（F#3）
//   NOTE_E4  = 330 Hz（E4）
//   NOTE_A4  = 440 Hz（A4）
//   NOTE_E3  = 165 Hz（E3）
// メロディ: FS4, D4, FS3, D4, E4, A4, E3, E4, FS4, E4, FS3, D4
int alert_melody[] = {
  370, 294, 185, 294, 330, 440, 165,
  330, 370, 330, 185, 294
};

// 音の長さ（MELODY_SPEED を基準とした倍率）
// L8（八分音符）= 0.5、L4（四分音符）= 1.0
// 参考サイトの定義: L1=2000ms, L2=1000ms, L4=500ms, L8=250ms
// MELODY_SPEED=400ms を四分音符(L4)の基準とすると、
//   L8 = 0.5 倍、L4 = 1.0 倍
float alert_duration[] = {
  0.5, 0.5, 0.5, 0.5, 0.5, 1.0, 0.5,
  0.5, 0.5, 0.5, 0.5, 1.0
};

int alert_melody_length = sizeof(alert_melody) / sizeof(alert_melody[0]);

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

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n");
  Serial.println("============================================================");
  Serial.println("Exchange Online Alert Relay & Buzzer Melody Controller");
  Serial.println("for ESP32-C3");
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

  // 起動直後にリレーを5秒稼働
  Serial.println("起動直後にリレーを5秒稼働");
  set_relay(true);
  delay(5000);
  set_relay(false);
  
  last_check_time = millis();
  last_relay_check_time = millis();
  
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
  
  // ブザータイマー管理
  manage_buzzer();
  
  delay(1000); // CPU負荷軽減
}

// ===== Wi-Fi設定 =====
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

// ===== リレー・ブザー設定 =====
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

// ===== リレー制御 =====
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

// ===== ブザータイマー管理 =====
void manage_buzzer() {
  if (!buzzer_active) {
    return;
  }
  
  unsigned long elapsed = millis() - buzzer_on_time;
  
  if (elapsed >= BUZZER_DURATION) {
    // ブザー鳴動時間終了
    noTone(BUZZER_PIN);
    buzzer_active = false;
    Serial.println("ブザー鳴動を停止しました");
    return;
  }
  
  // メロディ再生
  if (millis() - last_note_time >= MELODY_SPEED * alert_duration[melody_index]) {
    // 次の音符へ
    melody_index++;
    if (melody_index >= alert_melody_length) {
      melody_index = 0;
    }
    
    // 音符を再生
    if (alert_melody[melody_index] > 0) {
      tone(BUZZER_PIN, alert_melody[melody_index]);
    } else {
      noTone(BUZZER_PIN);
    }
    
    last_note_time = millis();
  }
}

// ===== リレータイマー管理 =====
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

// ===== メール受信チェック =====
void check_emails() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Wi-Fiが接続されていません");
    return;
  }
  
  Serial.println("--- メール受信チェック開始 ---");
  
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
    Serial.println("エラー: メール取得失敗");
    Serial.println("--- メール受信チェック終了 ---\n");
    return;
  }
  
  // JSON解析
  StaticJsonDocument<2048> doc;
  DeserializationError error = deserializeJson(doc, response);
  
  if (error) {
    Serial.println("JSON解析エラー");
    Serial.println("--- メール受信チェック終了 ---\n");
    return;
  }
  
  JsonArray messages = doc["value"].as<JsonArray>();
  
  if (messages.size() == 0) {
    Serial.println("新しいメールはありません");
    // 現在時刻を更新
    last_email_check_timestamp = time(nullptr);
    Serial.println("--- メール受信チェック終了 ---\n");
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
      Serial.print("メールを検出しました（キーワード未設定）: ");
      Serial.println(subject);
    } else {
      // キーワードが指定されている場合：キーワード検出
      if (contains_keyword(subject, ALERT_KEYWORD) || contains_keyword(bodyPreview, ALERT_KEYWORD)) {
        should_activate_relay = true;
        Serial.print("ALERTメールを検出しました: ");
        Serial.println(subject);
      }
    }
    
    // リレーとブザーON
    if (should_activate_relay) {
      if (!relay_active && startup_complete) {
        set_relay(true);
        relay_on_time = millis();
        Serial.print("リレーON開始時刻: ");
        Serial.println(relay_on_time);
        
        // ブザーも起動
        buzzer_active = true;
        buzzer_on_time = millis();
        melody_index = 0;
        last_note_time = millis();
        Serial.println("ブザーを起動しました");
      } else if (relay_active) {
        Serial.println("リレーは既にON状態です。タイマーはリセットしません");
      }
    }
  }
  
  // 現在時刻を更新（次回のチェック用）
  last_email_check_timestamp = time(nullptr);
  Serial.println("--- メール受信チェック終了 ---\n");
}

// ===== アクセストークン取得 =====
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

// ===== Graph API呼び出し =====
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
  http.begin(url);
  http.addHeader("Authorization", "Bearer " + access_token);
  // Content-Typeヘッダーを削除（GET リクエストでは不要）
  
  int httpCode = http.GET();
  
  if (httpCode == 200) {
    String response = http.getString();
    http.end();
    return response;
  } else {
    String errorResponse = http.getString();
    Serial.print("Graph API エラー: ");
    Serial.println(httpCode);
    Serial.print("エラーレスポンス: ");
    Serial.println(errorResponse);
    http.end();
    return "";
  }
}

// ===== キーワード検索 =====
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
