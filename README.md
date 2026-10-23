# Ameba 撲克牌辨識：第三週 V4

程式庫：https://github.com/yoting0910/Ameba-Card-Recognition-V4

影片 1｜桌面放置：https://youtu.be/FIOiZxqZ3Ns

影片 2｜手持操作：https://youtu.be/L0svma8mWBA

## 成果與版本

本版本為第九組第三週使用的 **V4、52 類撲克牌辨識版本**。
紅心、黑桃、方塊、梅花各 A～K，共 52 張牌，在桌面實機測試中全部辨識正確。
兩支影片均於電腦教室拍攝，四種花色各選三張，共 12 張牌；桌面與手持情境
各 12/12 辨識正確，每支約 1 分 17 秒。影片包含 Ameba 鏡頭、實際放牌操作與網頁結果。

| V4 模型資料 | 張數／結果 |
|---|---:|
| 訓練資料 | 934 張 |
| 驗證資料 | 619 張 |
| 保留測試資料 | 608 張 |
| 資料合計 | 2,161 張 |
| 訓練上限 | 50 epochs |
| 最佳驗證正確率 | 97.25% |
| 離線保留測試正確率 | 586/608，96.38% |

離線正確率來自 H5 模型的保留測試資料；52 張實機結果與影片結果是本組在
所述環境的操作測試，分開呈現。相較第二週的 26 類，本週擴充為 52 類並補拍易混淆牌面。
第二週 26 張操作約 12 分鐘；本週影片每 12 張約 77 秒，平均每張操作約 6.4 秒。
這是含放牌、調整與等待的整體操作時間，兩週拍攝情境與樣本範圍不同。

## 系統設計

程式燒錄於 Realtek Ameba AMB82-MINI，使用板載鏡頭取得影像。
板上完成完整牌框定位、透視校正、角標數字與小花色 ROI 裁切，再由 52 類模型分類。
網頁顯示攝影畫面、牌面數字、花色與辨識狀態，提供暫停／恢復操作。
分類信心至少 0.60、前兩名差距至少 0.10，連續兩次結果一致才接受。
JPEG 取像使用獨立工作，網頁分開查詢辨識結果。色彩校正關閉。

可支援些微角度旋轉與距離調整；部分反光可接受，但應避開數字與花色。
拍攝時應讓整張牌及四角完整入鏡。遮擋或背景干擾可能影響完整牌框與角標定位，
部分牌面需稍微調整角度才能完成辨識。

## 環境與檔案

- Arduino IDE、Realtek AmebaPro2 SDK `4.0.9-build20250805`、tools `1.4.5`。
- 開發板 AMB82-MINI，相機選項 F37，Load NN Model Source 選 flash。
- 序列監控視窗速率 115200。
- `RTSPImageClassification/` 包含 `.ino`、必要 `.h` 與 V4 `img_class_cnn.nb`。
- 保持資料夾名稱與 `RTSPImageClassification.ino` 名稱一致。
- `wifi_config.h` 是公開用範本，下載後在自己的電腦填入 Wi-Fi 設定。
- `version.json` 記錄版本、資料量與模型／程式檔案 SHA256。

## 燒錄與操作

1. 下載並解壓縮整份程式庫，先完成上述 SDK 與開發板安裝。
2. 編輯 `RTSPImageClassification/wifi_config.h`，將兩個 `YOUR_WIFI_...` 換成自己的 Wi-Fi 名稱與密碼。
3. **先設定 SDK 的 V4 模型來源**：關閉 Arduino IDE；在專案根目錄開啟 PowerShell，執行
   `powershell -NoProfile -ExecutionPolicy Bypass -File .\Prepare_V4_Model.ps1`。
   此工具核對 V4 模型雜湊、備份 SDK 舊模型，再更新五個已知的 SDK 模型來源。
   可加 `-WhatIf` 先查看將變更的位置。工具只準備編譯來源，不會燒錄開發板。
   如 SDK／tools 版本或安裝路徑不同，工具會停止，請先核對環境。
4. Arduino IDE 開啟 `RTSPImageClassification/RTSPImageClassification.ino`。
   選擇 AMB82-MINI、F37、模型來源 flash 與實際 COM 埠，重新編譯後自行上傳。
5. 開啟 115200 序列監控視窗，確認 `MODEL card_frame_index_roi_v4_52cards` 與
   `firmware async_preview_v6_model_v4_52`；使用當次顯示的 `http://板子IP/` 開啟辨識頁。
   電腦與板子須位於可互通的網路，重新連線後 IP 可能變更。
6. 完整放入一張牌，等待數字與花色顯示；換牌時先移走上一張，再放入下一張。
   診斷頁為 `http://板子IP/diagnostic`，可查看模型與 52 類輸出等資訊。

SDK 使用共用模型檔，單獨切換 `.ino` 或只把 NB 放在草稿資料夾，不足以保證編譯使用 V4。
準備工具也更新 SDK 的 `Cbackup_2026-10-08_img_class_cnn.nb`，避免打包流程還原成其他模型。

V4 NB SHA256：
`dc9fb06ba97685aa187f2d7eddb3d2a36824769e116d017f0b9d7c4568c1ebc6`

## 分工

| 組員 | 工作 |
|---|---|
| 李庶荃 | 硬體設定、開發板操作與拍攝 |
| 趙子綾 | 資料整理、模型訓練與拍攝 |
| 羅右庭 | 網頁介面、測試整合、報告與影片整理 |

公開上傳前請維持 `wifi_config.h` 為範本內容；已填入的私人連線資訊留在本機。
