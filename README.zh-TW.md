<h1 align="center">ruru-reader-tw</h1>

<p align="center"><strong>閱星曈 X4 電子書 reader 的繁體中文韌體。</strong><br>
jf-openhuninn 粉圓字型 · 完整藍芽修復 · 大書能開 · 內建書籤</p>

<p align="center">
  <img src="https://img.shields.io/badge/license-MIT-D4A5A5?style=flat-square" alt="MIT License">
  <img src="https://img.shields.io/badge/platform-ESP32--C3-B8A9C9?style=flat-square" alt="ESP32-C3">
  <img src="https://img.shields.io/badge/font-jf--openhuninn-A8B5A0?style=flat-square" alt="jf-openhuninn">
  <img src="https://img.shields.io/badge/stage-12.6-E8B4B8?style=flat-square" alt="Stage 12.6">
</p>

<p align="center">
  <a href="README.md">English</a> | <b>繁體中文</b>
</p>

---

## :brain: 這是什麼

**閱星曈 X4** 是一款 ESP32-C3 電子書 reader，320KB RAM、16MB flash、無 PSRAM。

這個 fork 把 X4 變成**適合繁中使用者**的閱讀器：

- :sparkles: **jf-openhuninn 粉圓字型**，含完整標點、數字、英文（11288 字）
- :wrench: **修復原廠 4 重藍芽 crash bug**（RPA 處理錯、vector 含裸指標 race、disconnect 時 double-free、阻塞掃描）
- :detective: **NimBLE `getAddressType()` 修正** — 解析私人地址（RPA）被誤判成 PUBLIC，造成 30 秒連線 timeout
- :books: **大書支援** — 15 MB / 2752+ 章的網文現在能開
- :anchor: **HTML void-element 自閉合預處理** — 解決點章節目錄 reader 凍結的惡名 bug
- :bookmark: **書籤系統**，跳轉用 spine 對位
- :art: **3×3 首頁網格**，顯示書封與閱讀進度
- :globe_with_meridians: **OpenCC `s2twp` 自動簡轉繁**

## :package: 安裝

### 1. 選一個韌體

最新版本在 `release/`：

| 版本     | 適用                          | 檔案                                       |
| :------- | :---------------------------- | :----------------------------------------- |
| **繁中** | 自用首選（粉圓字型 + 標點）   | `ruru-reader-tw-stage12.6-20260505.bin`    |
| **簡中** | 保留中國雲端服務              | `ruru-reader-cn-stageA-20260505.bin`       |

### 2. 用網頁燒錄（推薦）

用 **Chrome** 開 <https://flasher.crosspoint.world/>（需要 WebSerial）。USB-C 接 X4，選 BIN，按燒錄。

### 3. 或用 ESPTool 命令列

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 --baud 921600 \
  write_flash -z 0x10000 release/ruru-reader-tw-stage12.6-20260505.bin
```

### 4. 第一次開機

1. 開機
2. 設定 → 藍芽 → **啟用藍芽**
3. 掃描 → 選翻頁器 → 連線
4. 之後每次開機自動連回去

> :information_source: 升級韌體時，建議先刪 SD 卡上的 `.crosspoint/` 避免舊章節 cache 用舊邏輯解析。設定（藍芽啟用狀態、字型、邊距）會保留。

## :wrench: 實機驗證

### 藍芽翻頁器

| 裝置        | 連線                | 翻頁                                | 備註                                       |
| :---------- | :------------------ | :---------------------------------- | :----------------------------------------- |
| iDal-10822  | :white_check_mark:  | :white_check_mark: keycode `0x4E`   | 主要使用                                   |
| E1 Control  | :white_check_mark:  | :white_check_mark: keycode `0x4B`   | 備用                                       |
| HBTR003-XT  | :x:                 | —                                   | RPA 不穩（NimBLE / 硬體限制，韌體無解）    |

### 大書相容性

| 書           | 大小      | 章節  | 開書                | 翻頁                | 點目錄              |
| :----------- | :-------- | :---- | :------------------ | :------------------ | :------------------ |
| 一般網文     | 15 MB     | —     | :white_check_mark:  | :white_check_mark:  | :white_check_mark:  |
| 多章節網文   | 10.25 MB  | 2752  | :white_check_mark:  | :white_check_mark:  | :white_check_mark:  |

## :open_file_folder: 專案結構

```text
ruru-reader-tw/
├── lib/
│   ├── Epub/                    # EPUB 解析（Section、ChapterHtmlSlim、ParsedText）
│   │   └── Epub/Section.cpp     # HTML void-element 自閉合預處理（核心修復）
│   ├── hal/
│   │   └── BluetoothHIDManager  # 4 重藍芽修復 + RPA 解法 + 自動重連
│   └── PngToBmpConverter/       # 串流 PNG 解碼器（大封面不爆 RAM）
├── src/
│   ├── activities/
│   │   ├── home/                # 3×3 grid 最近閱讀
│   │   ├── reader/              # EpubReader、書籤、書籤選擇
│   │   └── settings/            # 藍芽設定（含 _uiBluetoothActive flag）
│   ├── components/
│   │   └── icons/               # grid 用的 12 個新 icon
│   └── CrossPointSettings.cpp   # 藍芽啟用狀態持久化修正
├── scripts/
│   └── charsets/charset_full.txt  # 11288 字字型 subset（含標點）
└── release/                     # 預先 build 好的 BIN（預設在 .gitignore）
```

## :wrench: 客製化

| 想改什麼          | 去哪裡看                                                                          |
| :---------------- | :-------------------------------------------------------------------------------- |
| 字型 subset       | `scripts/charsets/charset_full.txt` + 跑 `lib/EpdFont/scripts/fontconvert.py`     |
| 藍芽 keycode      | `lib/hal/DeviceProfiles.cpp`（每個裝置的 profile）                                |
| Reader menu 項目  | `src/activities/reader/EpubReaderMenuActivity.h`                                  |
| 預設 theme        | `src/CrossPointSettings.h` 的 `uiTheme` 初始值                                    |
| 跳過自動重連窗口  | `BluetoothHIDManager::_uiBluetoothActive` flag                                    |

## :bulb: 設計理念

**為什麼要 fork？** 上游 ChineseType 把藍芽 UI 入口註解掉、書架只有清單視圖 — 這些是作者知道有問題但用迴避策略的訊號。我們選擇真正修底層 bug：

1. **NimBLE 地址型別自判** — 從 MAC byte 0 bit 7:6 自己判，不信任 `getAddressType()`。RPA 地址（`01`）會被識別成 RANDOM，控制器才找得到。
2. **expat 前先預處理 HTML** — 不換 XML parser（改動太大），改在前面跑 1024-byte 緩衝的 state machine 自動補 void element 自閉斜線。多 ~3KB flash，100KB 章節 < 50 ms 跑完。
3. **書籤存 spine + page** — 儲存 spine index + 章節頁數 + 進度百分比；跳轉時優先用 spine（章節數一致時），fallback 用百分比。換字型/邊距重排版仍可用。
4. **3×3 grid** — 一頁 9 個書封 vs 原本 5 個清單項。480×800 電子紙的資訊密度好很多。

## :pray: 致謝

這個專案站在以下 repo 的肩膀上：

| 上游                                                                           | 作者              | 貢獻                                       |
| :----------------------------------------------------------------------------- | :---------------- | :----------------------------------------- |
| [crosspoint-reader](https://github.com/daveallie/crosspoint-reader)            | Dave Allie        | 原始 MIT 授權 reader                       |
| CrossInk                                                                       | uxjulia           | 中間 fork                                  |
| CrossInk-Carousel                                                              | chintanvajariya   | UI theme 系統（Lyra / Flow / 3Covers）     |
| [crosspoint-chinesetype](https://github.com/icannotttt/crosspoint-chinesetype) | icannotttt        | 中文化、堅果雲、KOReaderSync、藍芽框架     |

字型：[jf-openhuninn](https://justfont.com/huninn/) by **justfont** — CC BY 4.0。

HTML 自閉合預處理的概念啟發自另一個並行 fork；實作在這裡是原創。

## :scroll: 需求

- ESP32-C3 + 16MB flash + 電子紙顯示器（X4 硬體）
- 藍芽 HID 翻頁器（或用側邊實體按鈕）
- SD 卡放書本與 cache

## :balance_scale: 授權

採用 **MIT License**。詳見 [LICENSE](LICENSE)。

本專案延續上游 fork 鏈的 MIT 授權。所有修改也以 MIT 釋出。

---

<p align="center">
  <sub>由 <a href="https://github.com/HelloRuru">HelloRuru</a> 製作 · 2026 · 給繁體中文讀者</sub>
</p>
