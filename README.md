## 【社畜パトランプ】
特定のメールアドレスへのメールを受信したら、パトランプ（リレーモジュール）やブザーをONにするプログラムです。<br>
Youtubeの動画で公開しているのでぜひご覧ください。

### 試行錯誤を経て色々バージョンが増えているので整理しました。

| バージョン＆フォルダ名              | メールサービス     | 補足                                                                                                                                       | 
| -----------------------------       | ------------------ | ------------------------------------------------------------------------------------------------------------------------------------------ | 
| Raspberry Pi zero2wバージョン       | Gmail              | 初代です。Raspberry Pi zero2wで動作します。<br>処理能力が過剰なのと、ケーブルを抜くだけで電源を気楽に切れないのでESP32版の方がお勧めです。 | 
| ESP32-S1バージョン                  | Microsoft Exchange | 2代目です。SeeedStudioXIAOESP32-S3で動くバージョンです。                                                                                   | 
| ESP32-C3バージョン                  | Microsoft Exchange | 3代目です。SeeedStudioXIAOESP32-S3ではこれまた処理能力が過剰なので、お安いC3に置き換えたバージョンです。                                   | 
| ESP32-C3_ブザー付きバージョン       | Microsoft Exchange | 4代目です。パトランプだけでは深夜に起きられませんでした。。。<br>ブザーを追加しています。                                                  | 
| ESP32-C6_シールド・ブザーバージョン | Microsoft Exchange | ★最新版★5代目です。SeeedStudio XIAOのシールドを使用した版です。マイコンやC3より安くて高性能なC6を使っています。                          | 

ESP32バージョンは、メールサーバーはExchangeServerと接続する仕組みにしてありますが、ExchangeServerへのアプリの登録などいろいろめんどくさかったです。
メールサービスはGmailの方が楽です。

### Youtubeで動画を公開しています。ぜひご覧ください。
* [深夜のメールに対応するため社畜パトランプを作る Raspberry Pi Zero 2w](https://youtu.be/jD-DJ_TBCCw)
* [社畜パトランプを小型化・省電力化する Seeed Studio XIAO ESP32-S3](https://youtu.be/1cY0oliM73M)
* [社畜呼び込み君 深夜にメールを受信したらあなたを楽しく叩き起こします Seeed Studio XIAO ESP32-S3](https://youtu.be/54o2braTIRY)
* [社畜パトランプをUSB 5Vのみ駆動型に改良しました Seeed Studio XIAO ESP32-C3](https://youtu.be/b0TYgWEAZyU)
* [【初心者向け】ESP32の基本とシンプルな使い方 Seeed Studio XIAO ESP32 + Groveモジュール](https://youtu.be/KH83TCG_Z40)
