/*
  Exchange Online Alert Relay & Buzzer Melody Controller for ESP32-C6
  Exchange Online REST APIとGrove - Relay & Grove - Passive Buzzer制御プログラム

  機能：
  - Wi-Fi接続
  - Exchange Online REST APIをポーリング
  - 前回のチェック時刻以降に新たに受信したメールをチェック
  - リレーをON（3分間継続）
  - ブザーでメロディを再生（60秒継続）
  - 自動的にOFF
  - メールを既読にしない（複数台の機材で同じメールを処理可能）

  対応ボード：Seeed Studio XIAO ESP32C6
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

// 対応する音符と周波数を設定
#define NOTE_D0  0
#define NOTE_D1  294
#define NOTE_D2  330
#define NOTE_D3  350
#define NOTE_D4  393
#define NOTE_D5  441
#define NOTE_D6  495
#define NOTE_D7  556

#define NOTE_DL6 248

#define NOTE_DH1 589


// 曲全体の音符部分
int alert_melody[] = {
  NOTE_DH1, NOTE_D6, NOTE_D5, NOTE_D6, NOTE_D0,
  NOTE_DH1, NOTE_D6, NOTE_D5, NOTE_DH1, NOTE_D6, NOTE_D0, NOTE_D6,
  NOTE_D6, NOTE_D6, NOTE_D5, NOTE_D6, NOTE_D0, NOTE_D6,
  NOTE_DH1, NOTE_D6, NOTE_D5, NOTE_DH1, NOTE_D6, NOTE_D0,

  NOTE_D1, NOTE_D1, NOTE_D3,
  NOTE_D1, NOTE_D1, NOTE_D3, NOTE_D0,
  NOTE_D6, NOTE_D6, NOTE_D6, NOTE_D5, NOTE_D6,
  NOTE_D5, NOTE_D1, NOTE_D3, NOTE_D0,
  NOTE_DH1, NOTE_D6, NOTE_D6, NOTE_D5, NOTE_D6,
  NOTE_D5, NOTE_D1, NOTE_D2, NOTE_D0,
  NOTE_D7, NOTE_D7, NOTE_D5, NOTE_D3,
  NOTE_D5,
  NOTE_DH1, NOTE_D0, NOTE_D6, NOTE_D6, NOTE_D5, NOTE_D5, NOTE_D6, NOTE_D6,
  NOTE_D0, NOTE_D5, NOTE_D1, NOTE_D3, NOTE_D0,
  NOTE_DH1, NOTE_D0, NOTE_D6, NOTE_D6, NOTE_D5, NOTE_D5, NOTE_D6, NOTE_D6,
  NOTE_D0, NOTE_D5, NOTE_D1, NOTE_D2, NOTE_D0,
  NOTE_D3, NOTE_D3, NOTE_D1, NOTE_DL6,
  NOTE_D1,
  NOTE_D3, NOTE_D5, NOTE_D6, NOTE_D6,
  NOTE_D3, NOTE_D5, NOTE_D6, NOTE_D6,
  NOTE_DH1, NOTE_D0, NOTE_D7, NOTE_D5,
  NOTE_D6,
};

// 各音符の持続時間
float alert_duration[] = {
  1, 1, 0.5, 0.5, 1,
  0.5, 0.5, 0.5, 0.5, 1, 0.5, 0.5,
  0.5, 1, 0.5, 1, 0.5, 0.5,
  0.5, 0.5, 0.5, 0.5, 1, 1,

  1, 1, 1 + 1,
  0.5, 1, 1 + 0.5, 1,
  1, 1, 0.5, 0.5, 1,
  0.5, 1, 1 + 0.5, 1,
  0.5, 0.5, 0.5, 0.5, 1 + 1,
  0.5, 1, 1 + 0.5, 1,
  1 + 1, 0.5, 0.5, 1,
  1 + 1 + 1 + 1,
  0.5, 0.5, 0.5 + 0.25, 0.25, 0.5 + 0.25, 0.25, 0.5 + 0.25, 0.25,
  0.5, 1, 0.5, 1, 1,
  0.5, 0.5, 0.5 + 0.25, 0.25, 0.5 + 0.25, 0.25, 0.5 + 0.25, 0.25,
  0.5, 1, 0.5, 1, 1,
  1 + 1, 0.5, 0.5, 1,
  1 + 1 + 1 + 1,
  0.5, 1, 0.5, 1 + 1,
  0.5, 1, 0.5, 1 + 1,
  1 + 1, 0.5, 0.5, 1,
  1 + 1 + 1 + 1
};


// ===== 設定 =====
const char* SSID = "★ここにWifiのSSIDを設定★";              // Wi-Fi SSID（設定してください）
const char* PASSWORD = "★ここにWifiのパスワードを設定★";          // Wi-Fi パスワード（設定してください）

// Microsoft Entra ID認証情報
const char* TENANT_ID = "★ここにExchangeのテナントIDを設定★";         // テナントID（設定してください）
const char* CLIENT_ID = "★ここにExchangeのクライアントIDを設定★";         // クライアントID（設定してください）
const char* CLIENT_SECRET = "★ここにExchangeのクライアントシークレットを設定★";     // クライアントシークレット（設定してください）
const char* MAILBOX_ADDRESS = "xxxxx@XXXXX.XXXXX"; // 監視対象メールアドレス（設定してください）

// リレー・ブザー設定
const int RELAY_PIN = 0;           // A0ポート- Grove - Relay接続
const int BUZZER_PIN = 1;          // A1ポート- Grove - Passive Buzzer接続

const int CHECK_INTERVAL = 300000;  // メール受信チェック間隔（ミリ秒）：5分 = 300000ms
const int RELAY_ON_DURATION = 180000; // リレーON継続時間（ミリ秒）：3分 = 180000ms
const int BUZZER_DURATION = 30000;   // ブザー鳴動時間（ミリ秒）：30秒 = 30000msyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyyy
//const int CHECK_INTERVAL = 30000;  // メール受信チェック間隔（ミリ秒）：30秒 = 30000ms
//const int RELAY_ON_DURATION = 30000; // リレーON継続時間（ミリ秒）：30秒 = 30000ms
//const int BUZZER_DURATION = 15000;   // ブザー鳴動時間（ミリ秒）：15秒 = 15000ms

const int MELODY_SPEED = 400;        // メロディ速度（ミリ秒）：Wikiコード例2準拠、小さいほど速い
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

int alert_melody_length = sizeof(alert_melody) / sizeof(alert_melody[0]);

// ===== 関数プロトタイプ =====
void setup_wifi();
void setup_relay_buzzer();
void set_relay(bool state);
void play_melody_blocking();
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
  Serial.println("for ESP32-C6");
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
  // ブザーがアクティブな間はメロディ再生を優先する
  // （ブロッキング再生のため、他の処理はメロディ終了後に実行される）
  if (buzzer_active) {
    manage_buzzer();
    return;
  }

  unsigned long current_time = millis();
  
  // メール受信チェック（30秒ごと）
  if (current_time - last_check_time >= CHECK_INTERVAL) {
    check_emails();
    last_check_time = millis(); // check_emails()の実行時間を考慮して再取得
  }
  
  // リレータイマー管理（10秒ごと）
  if (millis() - last_relay_check_time >= 10000) {
    manage_relay_timer();
    last_relay_check_time = millis();
  }
  
  delay(100); // CPU負荷軽減（ブザー非動作時のみ）
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

// ===== ブザータイマー管理（ブロッキング方式・Wikiコード例2準拠） =====
// Wikiコード例2と同じく tone() → delay() → noTone() をブロッキングで実行する。
// BUZZER_DURATION を超えたら途中でも再生を打ち切る。
void manage_buzzer() {
  if (!buzzer_active) {
    return;
  }

  Serial.println("ブザーメロディ再生開始");

  for (int x = melody_index; x < alert_melody_length; x++) {
    // 鳴動時間を超えたら打ち切る
    if (millis() - buzzer_on_time >= BUZZER_DURATION) {
      noTone(BUZZER_PIN);
      buzzer_active = false;
      melody_index = 0;
      Serial.println("ブザー鳴動時間終了（途中打ち切り）");
      return;
    }

    tone(BUZZER_PIN, alert_melody[x]);           // 音符を出力
    delay((int)(MELODY_SPEED * alert_duration[x])); // Wikiコード例2と同じ: delay(400 * duration[x])
    noTone(BUZZER_PIN);                           // 音符を停止
    melody_index = x + 1;
  }

  // 1周分の演奏が終わった場合、鳴動時間内であれば先頭に戻って継続
  melody_index = 0;
  if (millis() - buzzer_on_time < BUZZER_DURATION) {
    // loop()に戻り、次のloop()呼び出しで再び先頭から再生される
    return;
  }

  // 鳴動時間終了
  noTone(BUZZER_PIN);
  buzzer_active = false;
  melody_index = 0;
  Serial.println("ブザー鳴動を停止しました");
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
      http.end();
      return true;
    } else {
      Serial.println("JSON解析エラー");
      http.end();
      return false;
    }
  } else {
    Serial.print("トークン取得エラー: ");
    Serial.println(httpCode);
    http.end();
    return false;
  }
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
