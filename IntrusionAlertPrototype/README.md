# 危險區域入侵提醒器：整合原型

## 目前狀態（2026-10-18）

已在獨立的 `IntrusionAlertPrototype` 專案整合人物偵測、可調整 ROI、底部中心點判斷、連續影格防抖、入侵事件計數、離開確認、LED/OSD/Serial 輸出，以及 HTTP 即時狀態與 JPEG 快照網頁；RTSP 仍保留。原手勢專案未修改。

最新整合版於 01:32:11–01:32:24 使用 portable AmebaPro2 4.0.9 編譯成功；18 項桌面邏輯測試及 7 個網頁狀態情境通過。01:34 已透過 COM4 成功燒錄；韌體 SHA-256 為 `EB97A6B735F5C7679A46E7E60C56AEFEC50B14A458121B0A42CA87F5BE9B21FD`。01:39 的 Serial 輸出已確認推論序號持續增加、人物偵測結果有 0/1 變化，並輸出 `STATE=SAFE`、`inside=0`、`events=0`；這代表當時人物底部中心點尚未進入 ROI，符合未觸發狀態。使用者確認 VLC 串流與黃色 `DANGER ZONE` ROI 框正常，並實測人物進入 ROI 後出現 WARNING、`events=1` 且 LED 亮。RTSP、ROI OSD、入侵觸發及 LED 警示已初步實機確認。**重複觸發抑制、離開後解除/重進、網頁 JPEG 與 RTSP 長時間穩定性仍待驗證**。曾出現一次 `hal_voe_send2voe too long`，後續推論仍持續；需觀察是否反覆發生。先前 00:30 的成功燒錄是 Phase 2 人物偵測版，不含目前的 ROI、事件警示與網頁整合。最新候選版與燒錄紀錄見 `BUILD_VERIFICATION.json`、`UPLOAD_VERIFICATION.json`。

下一步先確認網頁及影像 URL，再用畫面中的 ROI 測試進入、持續停留與離開/重進；同時核對 `inside`、SAFE/WARNING、事件數和 LED。實機結果須另行記錄，不以編譯或燒錄成功推定。

尺寸修正：SDK 雖定義 VIDEO_WVGA，四參數建構函式未處理此值。現在使用 `VideoSetting(640, 360, 15, VIDEO_H264, 0)`，並印出 `VIDEO_CONFIG: 640x360 fps=15`。先前版本出現人物結果但觀看通道初始化失敗；修正版實機畫面仍待驗證。

最新驗證（2026-10-17）：官方基準版及人物版編譯成功，人物版經使用者授權後以 COM4 燒錄，工具回報 upload success。尚未驗證開機人物偵測、RTSP 畫面與穩定性。詳見 BUILD_VERIFICATION.json、UPLOAD_VERIFICATION.json。

目前程式已包含 SAFE / WARNING / WAITING / STALE 狀態。STALE 或網路中斷不能解讀為 SAFE；事件計數僅維持本次開機，不保存到斷電後。

## 架構與修改原因

沿用 Realtek AmebaPro2 4.0.9-build20250805 的 ObjectDetectionLoop：Camera Channel 0 → H.264 → RTSP；Channel 3 → RGB → YOLOv4-tiny。
人物版只取 person（類別 0）且 score >= 50 的結果，不是重新訓練或人物專用模型。score() 為整數百分制，模型 configThreshold 使用 0～1。

觀看串流改為 640×360、15 FPS、約 768 Kibit/s，以降低原先 1080p/30 FPS 的觀看負載；不保證解決斷線。推論輸入保留 576×320、10 FPS，不能當成實測推論率。

背景 callback 篩選人物並複製到容量為一的 FreeRTOS queue；loop() 只讀已完成的快照，繪製人物框、分數和 PERSON / NO_PERSON / WAITING / STALE 狀態。最多保存 14 個人物框，超出時顯示截斷標記。新結果可覆蓋舊結果，未承諾逐幀處理。沒有結果超過 3 秒顯示 STALE，不當成無人。

框座標為正規化值，顯示時乘上輸出寬高並限制在畫面內。尚待實機確認跨通道視野與對齊。

## 本機帳密

WiFiCredentials.h 僅本機使用，已由 .gitignore 排除；不要上傳含帳密的韌體或打包整個資料夾。新下載專案時，複製 WiFiCredentials.example.h 為 WiFiCredentials.h，再填入自己的熱點資料。

## 編譯

從 C:\Users\vow19\Documents\Arduino 執行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\hw2\IntrusionAlertPrototype\build.ps1
```

Core：realtek:AmebaPro2 4.0.9-build20250805；FQBN：realtek:AmebaPro2:Ameba_AMB82-MINI。
JXF37 / Flash model / Auto Upload Disable / Erase Disable / FCS Disable / NN logs Disable。

建置使用 ../.amb82/portable 既有副本；輸出在 ../.amb82/intrusion-build，紀錄在 ../.amb82/intrusion-logs。不與手勢版共用 build 和紀錄，但 SDK 工具中間產物仍共用，不可同時編譯兩專案。每次燒錄前須重新核對工具目錄中的映像與對應 build 映像雜湊，不能直接沿用其他專案燒錄指令。

本腳本只編譯，不燒錄。實際編譯結果見 BUILD_VERIFICATION.json；實機測試結果另記，不由編譯成功推定。

## 整合版燒錄後的實機驗證（待正常開機與測試）

1. 開啟監視器，等待「連接 COM4 中」後確認 UART_DOWNLOAD 已放開，只按一下 RESET：

```powershell
arduino-cli monitor -p COM4 --config baudrate=115200
```

2. 確認開機標記 `INTRUSION_WEB_V1`、`VIDEO_CONFIG: 640x360 fps=15`，並看到 Wi-Fi、RTSP URL 及 `WEB URL`。
3. 電腦與板子連同一網路，以 VLC 開啟當次輸出的 `rtsp://IP:port`，確認畫面持續更新。
4. 開啟 `http://<板子當次 IP>/`，確認快照、狀態、人物數與事件數會更新；斷網或資料過期時不能仍顯示有效 SAFE。
5. 先無人，再讓人物完整進入 ROI，確認底部中心點、OSD/Serial 的 SAFE→WARNING、事件數加一及 LED；持續停留不應重複計數。
6. 人物離開 ROI 並等待釋放確認後，再進入一次，確認恢復 SAFE 並只增加一次事件；另測短暫漏偵測、多人和超出畫面的邊界情況。
7. 連續觀看至少 2 分鐘，再測關閉/重開 VLC 和網頁。記錄卡頓、`ENC Queue full`、重連、LED 極性及框/ROI 對齊；不要把模型設定 FPS 當成實測值。
8. STALE、影像過期或 Wi-Fi 中斷都不代表安全；記錄任何狀態、影像或 LED 不一致。

## 官方功能與新增項目

官方已有模型推論、人物類別、類別過濾、RTSP、框與分數、OSD 繪字。本原型相對官方範例新增可調 ROI 與底部中心點判斷、具連續新推論影格確認與短暫漏偵測容忍的事件狀態機、重複觸發抑制/事件計數，以及 LED 警示與網頁狀態呈現。人物模型、基本偵測、RTSP、人物框與 OSD 是沿用官方能力，不列為自行開發成果。上述自訂功能已完成程式與桌面測試，仍須板上實測後才能宣稱硬體驗證完成。

## 網頁影像查核

本機 SDK 根目錄：%LOCALAPPDATA%/Arduino15/packages/realtek/hardware/AmebaPro2/4.0.9-build20250805。

- libraries/Multimedia/examples/CaptureJPEG/HTTPDisplayJPEG：WiFiServer(80)、Camera.getImage()、image/jpeg 單張影像。
- libraries/Multimedia/examples/CaptureJPEG/HTTPDisplayJPEGContinuous：multipart/x-mixed-replace 連續 JPEG（MJPEG），官方註解指定 Firefox 測試。其他瀏覽器尚未驗證。
- libraries/NeuralNetwork/examples/ObjectDetectionSaveSDCard/SaveAsJPEG：Channel 0 H.264、Channel 1 JPEG、Channel 3 RGB 同時配置，並為 RTSP/JPEG 設定 OSD。此處只借鏡通道配置，後續網頁不需 SD 卡。

整合方案：保留 RTSP，另使用 JPEG 通道、獨立擷取任務與有界 HTTP socket 服務，網頁輪詢 `/status.json` 及 `/snapshot.jpg`。HTTP 請求大小、等待時間、JPEG 緩衝區均有上限，避免慢客戶端直接阻塞狀態主迴圈。網頁在資料或影像過期時遮蔽舊畫面與狀態。
程式已整合並通過編譯及模擬網頁行為測試；JPEG 通道 OSD 同步、網頁實際載入、效能、斷線重連與長時間穩定性仍待硬體/瀏覽器驗證。
