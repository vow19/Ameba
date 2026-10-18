# 實作紀錄

## 整合原型（2026-10-18）：ROI、事件警示與網頁

- 依使用者確認一次完成核心實作；建立/沿用獨立 `IntrusionAlertPrototype`，未修改原手勢專案。
- `AlertConfig.h`：集中設定正規化 ROI、信心門檻、進入/離開確認影格數與時間、STALE、冷卻、JPEG 與 HTTP 限制。
- `IntrusionLogic.h`：以有效人物框底部中心點做 ROI OR 判斷；只接受新序號，連續推論結果及最短停留時間達標才觸發。短暫漏偵測保留 WARNING；確認離開後才釋放，持續停留只計一次。處理序號/時間回繞與推論間隔過長。
- `IntrusionAlertPrototype.ino`：背景 callback 將結果複製到固定容量 queue；主迴圈消費快照並更新 OSD、Serial、LED、事件數和網頁狀態。保留 RTSP，新增 JPEG 通道；明確設定觀看影像 640×360/15 FPS。
- `WebPreview.cpp` / `WebPage.h`：獨立 JPEG 擷取與 HTTP 任務，提供 `/`、`/status.json`、`/snapshot.jpg`；限制請求大小、連線等待、傳輸時間，並讓過期影像/狀態不以有效即時資料呈現。網頁不承擔核心入侵判斷。
- 桌面驗證：`tests/run-tests.ps1` 的 18 項 C++ 邏輯案例及 7 個網頁狀態情境全部通過。Zig 編譯器發出既有 libunwind dllexport 警告，未阻止測試。
- Arduino 編譯：2026-10-18 01:17:19–01:18:02，exit 0；FQBN `realtek:AmebaPro2:Ameba_AMB82-MINI`，portable Core 4.0.9-build20250805，模型封裝與全域 manifest 檢查通過。Sketch SHA-256 `9CE4A0FA4460CAA5B29FDB963A1016254999E56B1411BCD18E4DB5DBE11B1396`；候選 `flash_ntz.bin` 為 9,580,544 bytes，SHA-256 `E7DA77AD320CB6BBBDECB97E7E6A584FDF360B41CA2EFAF9788075F6B549923E`。紀錄見 `../.amb82/intrusion-logs/last-build.json`。
- 2026-10-18 01:32:11–01:32:24 再次以 CLI 編譯整合版，Sketch uses 9,580,544 bytes（57%）。Arduino CLI 的獨立 upload/compile-upload 路徑回報找不到建置成品，未執行燒錄；比對 build 與工具目錄映像雜湊相同後，改用 SDK `platform.txt` 定義的 `image_windows.exe` 官方上傳配方。
- 2026-10-18 01:34:31 COM4 燒錄回報 `upload success` / `End Upload Flash`。韌體 SHA-256 `EB97A6B735F5C7679A46E7E60C56AEFEC50B14A458121B0A42CA87F5BE9B21FD`；Sketch SHA-256 `9CE4A0FA4460CAA5B29FDB963A1016254999E56B1411BCD18E4DB5DBE11B1396`。紀錄見 `UPLOAD_VERIFICATION.json` 及 `../.amb82/intrusion-logs/upload-20261018-012900.log`。
- 2026-10-18 01:39 使用者貼上整合版 Serial 執行輸出：`seq` 由 11 持續增加至 58；`persons` 在 0/1 間變化；樣本皆為 `STATE=SAFE events=0 inside=0`。確認韌體正在執行人物推論與新狀態輸出，當時偵測框底部中心點未進 ROI，因此沒有警告或計數；尚未證明 ROI 內觸發正確。
- 同一段期間出現一次 `hal_voe_send2voe too long 31935 cmd 0x00000208`，之後仍有後續推論輸出，暫不判定持續故障；需在觀察串流時留意是否重複及是否造成卡頓。
- 使用者確認 VLC 串流播放成功，且畫面可見黃色 `DANGER ZONE` ROI 框；RTSP 畫面與 ROI OSD 已初步實機確認，座標對齊仍待人物進入 ROI 驗證。
- 使用者實測人物進入黃色 ROI 約 2 秒：確認出現 WARNING、`events=1` 且板載 LED 亮。入侵觸發與 LED 警示的基本路徑已通過一次實機測試；持續停留不重複計數及退出/重進尚待測。
- **後續實機驗證待完成**：先確認 Serial 的 `RTSP URL`/`WEB URL`，測試 VLC 與網頁；再讓人物底部中心點進入畫面 ROI，確認 `inside=1`、連續確認後轉 WARNING、事件只加一次及 LED 亮；退出並等待後重進，確認釋放與再次觸發。ROI/影像座標對齊、HTTP JPEG、LED 極性、Wi-Fi/串流重連和穩定性仍待測。

## Phase 2：人物基礎與網頁影像查核

- 建立獨立 IntrusionAlertPrototype，原手勢程式未修改。
- 先複製已安裝 SDK 的 ObjectDetectionLoop.ino 與 ObjectClassList.h，官方原版編譯 exit 0，封裝 yolov4_tiny，全球 SDK 模型清單雜湊未變。
- 官方原版建置紀錄：../.amb82/intrusion-logs/official-baseline.json。
- IntrusionAlertPrototype.ino：沿用雙通道 Camera、StreamIO、RTSP 與 YOLOv4-tiny；僅保留 person、score >= 50。觀看串流改 640×360/15 FPS/768×1024 bit/s；AI 輸入仍 576×320/10 FPS。
- DetectionSnapshot.h：固定 14 個框容量；SDK callback 將有效正規化座標複製到單格 FreeRTOS queue，loop 不直接與背景任務共用結果 vector。不進行人物追蹤，編號不是穩定人物 ID。
- 狀態為 PERSON、NO_PERSON、WAITING、STALE；資料超過 3 秒無更新清除舊框並標記 STALE。Serial 每秒摘要、每 10 秒顯示當前網路狀態與網址。尚無自動 Wi-Fi 重連。
- build.ps1：獨立 build/log/response-file 路徑，模型預期更改為 yolov4_tiny，關閉 NN 大量日誌，保留原有 portable/locale/response-file 相容處理。僅編譯、不燒錄。
- WiFiCredentials.h：本機沿用帳密，.gitignore 排除；提供無真實帳密的 example。ObjectClassList.h 保留官方類別清單供參考，人物版直接比較 type()==0，不包含此表。
- 網頁：已讀 SDK HTTPDisplayJPEG、HTTPDisplayJPEGContinuous、ObjectDetectionSaveSDCard/SaveAsJPEG。官方有 HTTP JPEG、連續 multipart JPEG，以及 H.264 + JPEG + RGB 三通道與 OSD 範例；本次未建立網頁，未驗證三通道效能或瀏覽器顯示。
- 未燒錄、未實機驗證人物框/信心值/串流穩定度；無 ROI、入侵計數、LED、SAFE/WARNING 或正式網頁。
- 待使用者確認後燒錄 Phase 2，先實測人物基礎；通過並另行確認後才進 Phase 3。
人物版編譯完成：2026-10-17 19:50:39，exit 0；模型驗證與全域清單不變檢查通過，build/工具映像雜湊一致。映像 9580544 bytes。完整紀錄見 BUILD_VERIFICATION.json；uploaded=false、hardwareTested=false。

## Phase 2 燒錄（2026-10-17）

- 使用者明確授權後重新編譯，exit 0；模型與全域模型清單檢查通過。
- 標頭最初逐位元比對不一致，確認為 Arduino CLI 加入 #line 註記；排除註記與換行差異後，兩份標頭內容與來源一致，非使用者更改帳密。
- 燒錄前已核對 build 映像與 portable 工具中的映像雜湊一致，未發現 Arduino CLI Serial Monitor 程序。
- COM4 燒錄日誌顯示 upload success、End Upload Flash，CLI exit 0。完整時間與雜湊見 UPLOAD_VERIFICATION.json。
- 本次已取代板上手勢韌體；電腦中的手勢原始程式不變。
- 尚待正常開機確認 PHASE2_PERSON_BASELINE、人物框與分數、串流穩定性；未宣稱實機測試通過。網頁影像與 ROI 等後續功能尚未整合。

## Phase 2 使用者實機日誌：人物結果已出現，串流待驗證

- 使用者貼上的開機輸出含 WiFi status: 3、RTSP URL: rtsp://172.20.10.2:554。
- 已看到 STATE=PERSON、persons=1、score 51–83 及正規化框座標，seq 持續增加至 386；也有 NO_PERSON。可確認模型結果與應用程式快照/Serial 路徑已在板上執行；未經人工對照畫面，不宣稱辨識正確率已驗證。
- 啟動期間出現 VOE_OPEN_CMD command fail、hal_video_open fail、Please check sensor id first；隨後雖然出現人物結果，仍不能據此認定 Channel 0 RTSP 影像初始化正常，需確認 VLC 畫面與框對齊。
- 最初 STALE seq=0 隨後恢復新結果；不是整段推論停止。接近門檻的 PERSON/NO_PERSON 切換待畫面對照，可能包含漏偵測或人物離開，尚不能指定原因。
- 多個框的 xmax/ymax 接近 1，後續 ROI 底部中心點測試需讓整個人（包含腳部）留在畫面，避免將裁切框底部當成真實落腳位置。
- Phase 2 仍待影像、單人/多人、無人、退出重進與串流穩定性驗證；尚未進入 Phase 3。

## Phase 2 修正：觀看通道尺寸未設定

- 實機日誌已有 PERSON 結果，但使用者回報 VLC 沒畫面，並包含 hal_video_open fail。
- 本次直接 DESCRIBE RTSP 收到 200 OK，證明服務可回應，不代表有 H.264 影格。
- 查閱已安裝 VideoStream.cpp 的四參數 VideoSetting 建構函式，僅處理 VIDEO_FHD、VIDEO_HD、VIDEO_VGA、VIDEO_D1，沒有 VIDEO_WVGA 分支。即使標頭有此常數，全域 config 物件的寬高仍保持零。先前只核對常數存在及編譯成功不足，這是本專案新增觀看設定的錯誤。
- IntrusionAlertPrototype.ino 改用已核對的五參數建構函式 VideoSetting(640, 360, 15, VIDEO_H264, 0)。保持解析度目標、幀率與 bitrate 不變，先修正尺寸來源。
- 新增開機 VIDEO_CONFIG 寬高/FPS 輸出與尺寸檢查；若不是 640×360 就停止初始化並明確報錯。
- 此次只修改新專案，不修改 SDK 或手勢程式。尚未燒錄修正版；15 FPS 實機支援、影像、框對齊與重連仍待驗證。

## 2026-10-18：更換網路後重新準備 Phase 2

- 使用者回報更換網路且板子已燒錄其他程式；目前不能沿用先前板上狀態。
- 本機已更新 WiFiCredentials.h（iPhone 熱點），帳密格式檢查通過；不輸出帳密。
- 發現全域 Core 已升級為 4.1.1-build20260922，4.0.9 全域檔案不存在，原建置保護檢查中止。修正 build.ps1 為比對當前所有全域版本的模型清單；不修改或降版全域 Core。
- 固定使用工作區 portable Core 4.0.9-build20250805；修正版明確設定 640×360/15 FPS 並加入 VIDEO_CONFIG 檢查。
- 00:27:34–00:27:50 編譯 exit 0，映像 9,580,544 bytes（57%），YOLOv4-tiny 封裝通過，全域清單未變；build 和工具映像雜湊一致。
- BUILD_VERIFICATION.json 已更新且 uploaded=false。UPLOAD_VERIFICATION.json 僅為先前版本的歷史上傳紀錄，不代表這份候選版已燒錄。
- 待使用者關閉監視器並進入下載模式後重新燒錄；目前未進 Phase 3，網頁尚未整合。

2026-10-18 修正版已在使用者確認下載模式後經 COM4 燒錄：工具回報 upload success、CLI exit 0。時間 2026-10-18T00:30:52.5098851+08:00 至 2026-10-18T00:31:55.7635705+08:00，映像 SHA-256 CFA9DBA4B704E41F1C3FCE0F988CC93DF6CD57280E1C2A33AEBAC8FC7C96E69B。主程式、標頭（排除 CLI 的 #line）與工具映像已核對；帳密未輸出。尚待正常開機確認 VIDEO_CONFIG: 640x360 fps=15、人物結果及 RTSP 畫面。
