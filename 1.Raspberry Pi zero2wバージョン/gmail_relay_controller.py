#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Gmail Alert Relay Controller
Raspberry Pi Zero 2 W用のGmail連携リレー制御プログラム

機能：
- 指定のGmailアドレスからのメール受信を5分ごとにチェック
- メールタイトルまたは本文に「」を含む場合、リレーをON
- リレーON状態を3分間継続し、その後自動的にOFF
- ログ記録機能付き
"""

import imaplib
import email
from email.header import decode_header
import RPi.GPIO as GPIO
import time
import threading
import logging
import sys
from datetime import datetime, timedelta
import os
from pathlib import Path

# ===== 設定 =====
GMAIL_ADDRESS = "★★★メールアドレスを設定してください★★★@gmail.com"
GMAIL_APP_PASSWORD = "★★★GmailのApp Passwordを設定してください★★★"  # App Passwordを設定してください
GPIO_PIN = 17  # リレーモジュール接続GPIO番号
CHECK_INTERVAL = 300  # メール受信チェック間隔（秒）：5分 = 300秒
RELAY_ON_DURATION = 180  # リレーON継続時間（秒）：3分 = 180秒
ALERT_KEYWORD = ""  # 検出キーワード　★キーワードを追加したいならここに記入。指定なしは条件なしですべて対象となります★
LOG_FILE = os.path.expanduser("~/gmail_relay_controller.log")  # ログファイルパス
DEBUG_MODE = True  # デバッグモード（本番稼働時はFalseに変更）

# ===== グローバル変数 =====
relay_on_time = None  # リレーON開始時刻
relay_active = False  # リレーアクティブフラグ
last_processed_uid = None  # 最後に処理したメールのUID
startup_complete = False  # 起動完了フラグ


def setup_logging():
    """ログ設定"""
    global LOG_FILE
    
    log_dir = os.path.dirname(LOG_FILE)
    if log_dir and not os.path.exists(log_dir):
        try:
            os.makedirs(log_dir, exist_ok=True)
        except PermissionError:
            # /var/logへのアクセス権限がない場合はホームディレクトリを使用
            LOG_FILE = os.path.expanduser("~/gmail_relay_controller.log")
            log_dir = os.path.dirname(LOG_FILE)
            os.makedirs(log_dir, exist_ok=True)

    log_format = '%(asctime)s - %(levelname)s - %(message)s'
    log_level = logging.DEBUG if DEBUG_MODE else logging.INFO

    # ファイルハンドラ
    file_handler = logging.FileHandler(LOG_FILE)
    file_handler.setLevel(log_level)
    file_handler.setFormatter(logging.Formatter(log_format))

    # コンソールハンドラ
    console_handler = logging.StreamHandler(sys.stdout)
    console_handler.setLevel(log_level)
    console_handler.setFormatter(logging.Formatter(log_format))

    # ロガー設定
    logger = logging.getLogger()
    logger.setLevel(log_level)
    logger.addHandler(file_handler)
    logger.addHandler(console_handler)

    return logger


def setup_gpio():
    """GPIO設定"""
    try:
        GPIO.setmode(GPIO.BCM)
        GPIO.setup(GPIO_PIN, GPIO.OUT)
        # 起動時はリレーをOFF状態に初期化
        GPIO.output(GPIO_PIN, GPIO.LOW)
        logging.info(f"GPIO {GPIO_PIN} を初期化しました（OFF状態）")
    except Exception as e:
        logging.error(f"GPIO初期化エラー: {e}")
        raise


def cleanup_gpio():
    """GPIO クリーンアップ"""
    try:
        GPIO.output(GPIO_PIN, GPIO.LOW)  # リレーをOFF
        GPIO.cleanup()
        logging.info("GPIOをクリーンアップしました")
    except Exception as e:
        logging.error(f"GPIOクリーンアップエラー: {e}")


def set_relay(state):
    """リレー状態を設定"""
    global relay_active
    try:
        if state:
            GPIO.output(GPIO_PIN, GPIO.HIGH)
            relay_active = True
            logging.info("リレーをONにしました")
        else:
            GPIO.output(GPIO_PIN, GPIO.LOW)
            relay_active = False
            logging.info("リレーをOFFにしました")
    except Exception as e:
        logging.error(f"リレー制御エラー: {e}")


def decode_email_header(header):
    """メールヘッダーをデコード"""
    if header is None:
        return ""
    
    decoded_parts = decode_header(header)
    decoded_str = ""
    
    for part, charset in decoded_parts:
        if isinstance(part, bytes):
            try:
                decoded_str += part.decode(charset or 'utf-8', errors='ignore')
            except Exception:
                decoded_str += part.decode('utf-8', errors='ignore')
        else:
            decoded_str += str(part)
    
    return decoded_str


def get_email_body(msg):
    """メール本文を取得"""
    body = ""
    
    try:
        if msg.is_multipart():
            for part in msg.walk():
                content_type = part.get_content_type()
                
                if content_type == "text/plain":
                    payload = part.get_payload(decode=True)
                    charset = part.get_content_charset() or 'utf-8'
                    body += payload.decode(charset, errors='ignore')
                elif content_type == "text/html":
                    # HTMLメールの場合はテキスト部分を抽出
                    payload = part.get_payload(decode=True)
                    charset = part.get_content_charset() or 'utf-8'
                    body += payload.decode(charset, errors='ignore')
        else:
            payload = msg.get_payload(decode=True)
            charset = msg.get_content_charset() or 'utf-8'
            body = payload.decode(charset, errors='ignore')
    except Exception as e:
        logging.warning(f"メール本文取得エラー: {e}")
    
    return body


def check_alert_keyword(subject, body):
    """メールタイトルまたは本文にALERTキーワードを含むかチェック"""
    subject_upper = subject.upper()
    body_upper = body.upper()
    keyword_upper = ALERT_KEYWORD.upper()
    
    return keyword_upper in subject_upper or keyword_upper in body_upper


def connect_gmail(max_retries=3):
    """Gmail IMAP接続（自動再試行機能付き）"""
    for attempt in range(max_retries):
        try:
            imap = imaplib.IMAP4_SSL("imap.gmail.com", 993)
            imap.login(GMAIL_ADDRESS, GMAIL_APP_PASSWORD)
            logging.info("Gmailに接続しました")
            return imap
        except imaplib.IMAP4.error as e:
            logging.error(f"Gmail接続エラー（試行 {attempt + 1}/{max_retries}）: {e}")
            if attempt < max_retries - 1:
                wait_time = (attempt + 1) * 10  # 指数バックオフ
                logging.info(f"{wait_time}秒後に再試行します")
                time.sleep(wait_time)
        except Exception as e:
            logging.error(f"予期しないエラー（試行 {attempt + 1}/{max_retries}）: {e}")
            if attempt < max_retries - 1:
                wait_time = (attempt + 1) * 10
                logging.info(f"{wait_time}秒後に再試行します")
                time.sleep(wait_time)
    
    return None


def check_emails():
    """メール受信チェック"""
    global relay_on_time, relay_active, last_processed_uid, startup_complete
    
    try:
        imap = connect_gmail()
        if imap is None:
            logging.error("Gmailへの接続に失敗しました")
            return
        
        # INBOX を選択
        imap.select("INBOX")
        
        # 未読メールを検索
        status, messages = imap.search(None, "UNSEEN")
        
        if status != "OK":
            logging.warning("メール検索に失敗しました")
            imap.close()
            imap.logout()
            return
        
        message_ids = messages[0].split()
        
        if not message_ids:
            logging.debug("新しいメールはありません")
            imap.close()
            imap.logout()
            return
        
        logging.info(f"{len(message_ids)}件の未読メールを検出しました")
        
        # メールを処理
        for msg_id in message_ids:
            try:
                status, msg_data = imap.fetch(msg_id, "(RFC822)")
                
                if status != "OK":
                    continue
                
                msg = email.message_from_bytes(msg_data[0][1])
                
                # メールタイトルと本文を取得
                subject = decode_email_header(msg.get("Subject", ""))
                body = get_email_body(msg)
                from_addr = msg.get("From", "")
                
                logging.debug(f"メール受信 - From: {from_addr}, Subject: {subject}")
                
                # ALERTキーワードをチェック
                if check_alert_keyword(subject, body):
                    logging.warning(f"ALERTメールを検出しました: {subject}")
                    
                    # リレーをON
                    if not relay_active:
                        set_relay(True)
                        relay_on_time = datetime.now()
                        logging.info(f"リレーON開始時刻: {relay_on_time}")
                    else:
                        logging.info("リレーは既にON状態です。タイマーはリセットしません")
                
                # メールを既読にする
                imap.store(msg_id, "+FLAGS", "\\Seen")
                logging.debug(f"メール {msg_id} を既読にしました")
            
            except Exception as e:
                logging.error(f"メール処理エラー: {e}")
                continue
        
        imap.close()
        imap.logout()
    
    except Exception as e:
        logging.error(f"メール受信チェックエラー: {e}")


def manage_relay_timer():
    """リレータイマー管理（3分間ON状態を継続）"""
    global relay_on_time, relay_active, startup_complete
    
    while True:
        try:
            # 起動直後は処理をスキップ（startup_completeがTrueになるまで待機）
            if relay_active and relay_on_time is not None and startup_complete:
                elapsed_time = (datetime.now() - relay_on_time).total_seconds()
                
                if elapsed_time >= RELAY_ON_DURATION:
                    logging.info(f"リレーON時間（{RELAY_ON_DURATION}秒）に達しました")
                    set_relay(False)
                    relay_on_time = None
            
            time.sleep(10)  # 10秒ごとにチェック
        
        except Exception as e:
            logging.error(f"リレータイマー管理エラー: {e}")
            time.sleep(10)


def main():
    """メインループ"""
    global startup_complete
    try:
        # ロギング設定
        logger = setup_logging()
        logger.info("=" * 60)
        logger.info("Gmail Alert Relay Controller を起動しました")
        logger.info(f"Gmail Address: {GMAIL_ADDRESS}")
        logger.info(f"GPIO Pin: {GPIO_PIN}")
        logger.info(f"Check Interval: {CHECK_INTERVAL}秒")
        logger.info(f"Relay ON Duration: {RELAY_ON_DURATION}秒")
        logger.info(f"Alert Keyword: {ALERT_KEYWORD}")
        logger.info(f"Debug Mode: {DEBUG_MODE}")
        logger.info("=" * 60)
        
        # GPIO設定
        setup_gpio()
        
        # リレータイマー管理スレッドを開始
        timer_thread = threading.Thread(target=manage_relay_timer, daemon=True)
        timer_thread.start()
        logging.info("リレータイマー管理スレッドを開始しました")
        
        # 起動直後のスリープ（前回実行時の古い状態をクリアするため）
        logging.info("起動直後の初期化スリープを開始します（30秒）")
        time.sleep(30)
        startup_complete = True
        logging.info("起動直後の初期化スリープが完了しました。通常動作を開始します")
        
        # メール受信チェックループ
        logging.info(f"メール受信チェックを開始します（{CHECK_INTERVAL}秒間隔）")
        
        while True:
            try:
                check_emails()
            except KeyboardInterrupt:
                logging.info("キーボード割り込みを受信しました")
                break
            except Exception as e:
                logging.error(f"メール受信チェック中にエラーが発生しました: {e}")
            
            time.sleep(CHECK_INTERVAL)
    
    except KeyboardInterrupt:
        logging.info("プログラムを停止しています...")
    except Exception as e:
        logging.error(f"致命的エラー: {e}")
    finally:
        cleanup_gpio()
        logging.info("Gmail Alert Relay Controller を終了しました")


if __name__ == "__main__":
    main()
