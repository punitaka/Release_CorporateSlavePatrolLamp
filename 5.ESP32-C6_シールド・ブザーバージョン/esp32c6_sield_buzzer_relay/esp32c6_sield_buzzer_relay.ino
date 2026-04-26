/*
  Exchange Online Alert Relay & Buzzer Melody Controller for ESP32-C6
  Exchange Online REST APIとGrove - Relay & Grove - Passive Buzzer制御プログラム

  機能：
  - Wi-Fi接続
  - Exchange Online REST APIをポーリング（5分間隔）
  - 前回のチェック時刻以降に新たに受信したメールをチェック
  - メール検出時にリレーをON（3分間継続）
  - メール検出時にブザーでメロディを再生（30秒継続）
  - 自動的にリレーOFF・ブザー停止
  - メールを既読にしない（複数台の機材で同じメールを処理可能）
  - 起動直後にリレーとブザーを5秒間動作させて動作確認

  メロディ仕様：
  - BPMベースのノンブロッキング再生方式
  - BPM: 158.000764（MIDI基準テンポ）
  - 音符間無音比率: 8%
  - 1フレーズ再生後に3秒待機して繰り返し

  対応ボード：Seeed Studio XIAO ESP32C6
  Grove Shield：Seeeduino XIAO用Grove シールド
  リレー接続：A0ポート（GPIO 0）
  ブザー接続：A1ポート（GPIO 1）

  必要なライブラリ：
  - WiFi.h（ESP32標準）
  - HTTPClient.h（ESP32標準）
  - ArduinoJson.h（Arduino JSON Library）
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <time.h>

// 曲全体の音符部分（ユーザー提供のMIDI基準メロディを周波数へ変換）
int alert_melody[] = {
  698, 740, 831, 831, 831, 831, 831, 554, 554, 554, 831, 740,
  698, 740, 622, 1047, 1047, 932, 1047, 1109, 932, 1047, 1109, 1047,
  932, 932, 1047, 1047, 1245, 1109, 698, 740, 740, 831, 932, 880,
  880, 740, 698, 622, 698, 831, 1109, 932, 1109, 1047, 1047, 1109
};

// 各音符の持続時間（1.0 = 4分音符, 0.5 = 8分音符）
float alert_duration[] = {
  0.5, 0.5, 1.0, 0.5, 1.0, 1.0, 1.0, 0.5, 1.5, 0.5, 0.5, 0.5,
  1.0, 0.5, 1.0, 1.0, 1.0, 0.5, 0.5, 1.5, 0.5, 0.5, 1.0, 0.5,
  1.0, 1.0, 1.0, 0.5, 1.0, 1.5, 0.5, 0.5, 0.5, 1.0, 1.0, 1.0,
  0.5, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 1.0, 2.0, 1.0, 0.5, 5.0
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

const float BPM = 158.000764f;               // ユーザー提供MIDI基準のテンポ
const float NOTE_GAP_RATIO = 0.08f;           // 各音符の間に入れる無音比率
const int REPEAT_WAIT_MS = 3000;              // 1フレーズ再生後の待機時間（ミリ秒）
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
bool note_in_gap = false;           // ブザー音符の無音区間フラグ
bool melody_waiting_restart = false; // 1フレーズ再生後の待機フラグ
unsigned long note_phase_start = 0; // 現在の音符または無音区間の開始時刻
unsigned long repeat_wait_start = 0; // フレーズ再生後の待機開始時刻
unsigned long current_sound_ms = 0; // 現在の音符の発音時間
unsigned long current_gap_ms = 0;   // 現在の音符の無音時間

int alert_melody_length = sizeof(alert_melody) / sizeof(alert_melody[0]);

// ===== 関数プロトタイプ =====
void setup_wifi();
void setup_relay_buzzer();
void set_relay(bool state);
void start_buzzer();
void stop_buzzer();
int beatToMs(float beats);
void start_next_note(unsigned long now);
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
  start_buzzer();
  unsigned long startup_buzzer_start = millis();
  while (millis() - startup_buzzer_start < 5000) {
    manage_buzzer();
    delay(5);
  }
  stop_buzzer();
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

// ===== ブザー制御 =====
void start_buzzer() {
  buzzer_active = true;
  buzzer_on_time = millis();
  melody_index = 0;
  note_in_gap = false;
  melody_waiting_restart = false;
  note_phase_start = 0;
  repeat_wait_start = 0;
  current_sound_ms = 0;
  current_gap_ms = 0;
  Serial.println("ブザーを起動しました");
}

void stop_buzzer() {
  noTone(BUZZER_PIN);
  buzzer_active = false;
  melody_index = 0;
  note_in_gap = false;
  melody_waiting_restart = false;
  note_phase_start = 0;
  repeat_wait_start = 0;
  current_sound_ms = 0;
  current_gap_ms = 0;
  Serial.println("ブザー鳴動を停止しました");
}

int beatToMs(float beats) {
  const float quarterMs = 60000.0f / BPM;
  return (int)(quarterMs * beats + 0.5f);
}

void start_next_note(unsigned long now) {
  int total_ms = beatToMs(alert_duration[melody_index]);
  current_sound_ms = (unsigned long)(total_ms * (1.0f - NOTE_GAP_RATIO));
  current_gap_ms = (unsigned long)(total_ms - current_sound_ms);
  note_phase_start = now;
  note_in_gap = false;
  melody_waiting_restart = false;
  repeat_wait_start = 0;

  if (alert_melody[melody_index] > 0) {
    tone(BUZZER_PIN, alert_melody[melody_index]);  // 停止はmanage_buzzer()内のnoTone()に任せる
  } else {
    noTone(BUZZER_PIN);
  }
}

// ===== ブザータイマー管理（ノンブロッキング方式・BPMベース） =====
void manage_buzzer() {
  if (!buzzer_active) {
    return;
  }

  unsigned long now = millis();
  if (now - buzzer_on_time >= BUZZER_DURATION) {
    Serial.println("ブザー鳴動時間終了");
    stop_buzzer();
    return;
  }

  if (melody_waiting_restart) {
    if (now - repeat_wait_start >= REPEAT_WAIT_MS) {
      melody_index = 0;
      start_next_note(now);
    }
    return;
  }

  if (note_phase_start == 0) {
    start_next_note(now);
    return;
  }

  if (!note_in_gap) {
    if (now - note_phase_start >= current_sound_ms) {
      noTone(BUZZER_PIN);
      note_in_gap = true;
      note_phase_start = now;
    }
    return;
  }

  if (now - note_phase_start >= current_gap_ms) {
    melody_index++;
    if (melody_index >= alert_melody_length) {
      noTone(BUZZER_PIN);
      melody_index = 0;
      note_phase_start = 0;
      note_in_gap = false;
      melody_waiting_restart = true;
      repeat_wait_start = now;
      return;
    }
    start_next_note(now);
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
        start_buzzer();
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
