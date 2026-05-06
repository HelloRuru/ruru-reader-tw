# Security Policy / 資安政策

> Bluetooth, WiFi, OTA — embedded firmware has real attack surface. We take it seriously.

## English

### Scope

This firmware exposes:

- **Bluetooth Low Energy (BLE)** — pairing with HID page-turners
- **WiFi (mutually exclusive with BLE on ESP32-C3)** — local web server for book transfer / settings
- **SD card filesystem** — book content, settings persistence, font cache
- **Web settings interface** — exposed on local WiFi when active

Anything that lets an attacker trigger code execution on the X4, exfiltrate book content from the SD card, or persist on the device qualifies as a security issue.

### Out of scope

- Issues that require physical USB access (we already trust whoever has the device)
- Issues in upstream libraries (NimBLE-Arduino, expat, etc.) — please report those upstream
- Issues only reachable when the user explicitly enables the WiFi web server in a hostile environment
- "It runs unsigned firmware" — yes, by design; that's how you flash this in the first place

### Reporting

**Do not open public issues for security bugs.**

Instead:

1. Email the maintainer (HelloRuru) with `[security] ruru-reader-tw` in the subject line
2. Include: what you found, how to reproduce, what an attacker could do, what build version is affected
3. Allow up to **30 days** for an initial response
4. We'll coordinate disclosure; typical embargo is 30–90 days depending on severity

### Recognized severity

| Severity | Examples                                                                 |
| :------- | :----------------------------------------------------------------------- |
| Critical | Remote code execution via crafted EPUB, BLE, or WiFi packet              |
| High     | Persistent storage corruption, exfiltration via WiFi, BLE pairing bypass |
| Medium   | Crash that requires power-cycle; settings-file corruption                |
| Low      | UI freezes recoverable by reboot; cosmetic exploits                      |

### What we'll do

- Acknowledge receipt within 7 days
- Patch in a stage release (e.g. `stageN.X`)
- Credit you in CHANGELOG and release notes (unless you prefer anonymity)
- For critical issues: coordinate with upstream forks (ChineseType, CrossInk) so they can patch too

### What we won't do

- Pay bug bounties (this is a hobby project)
- Negotiate over disclosure timelines under threat
- Patch versions older than the previous stage

---

## 繁體中文

### 範圍

本韌體暴露：

- **藍芽 BLE** — 跟 HID 翻頁器配對
- **WiFi（ESP32-C3 上跟 BLE 互斥）** — 本機網頁 server 傳書 / 改設定
- **SD 卡檔案系統** — 書本內容、設定持久化、字型 cache
- **網頁設定介面** — 啟用 WiFi 時開放於本機網路

任何能讓攻擊者在 X4 觸發程式碼執行、從 SD 卡偷書本內容，或常駐裝置的 issue，都屬於資安 issue。

### 不在範圍

- 需要實體 USB 存取的問題（拿到裝置的人本來就被信任）
- 上游函式庫的問題（NimBLE-Arduino、expat 等）— 請回報上游
- 只在使用者主動啟用 WiFi 網頁 server + 暴露於惡意環境才能觸發的問題
- 「能燒未簽名韌體」— 是設計如此，否則你也燒不了這個

### 回報方式

**不要開公開 issue 報資安 bug。**

請：

1. 寄信給維護者（HelloRuru），標題加 `[security] ruru-reader-tw`
2. 說明：發現什麼、怎麼重現、攻擊者能做什麼、影響哪個 build
3. 給最多 **30 天**初步回應時間
4. 我們會協調揭露時程，通常依嚴重度禁言 30–90 天

### 嚴重度分級

| 嚴重度 | 範例                                                              |
| :----- | :---------------------------------------------------------------- |
| 致命   | 透過特製 EPUB、BLE 或 WiFi 封包遠端執行程式碼                     |
| 高     | 持久儲存破壞、WiFi 偷資料、BLE 配對繞過                           |
| 中     | 需要斷電重啟才能復原的 crash、settings 檔損壞                     |
| 低     | UI 凍結但能重啟復原、純美觀類 exploit                             |

### 我們會做

- 7 天內確認收到
- 在 stage release（例 `stageN.X`）發 patch
- 在 CHANGELOG 跟 release notes 致謝（除非你要匿名）
- 致命問題會跟上游 fork（ChineseType、CrossInk）協調，讓他們也能 patch

### 我們不會做

- 給 bug bounty（業餘專案）
- 在威脅下協商揭露時程
- patch 更舊的 stage 版本（只支援上一個 stage）
