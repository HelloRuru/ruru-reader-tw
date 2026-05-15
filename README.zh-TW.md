<h1 align="center">ruru-reader-tw</h1>

<p align="center"><strong>閱星曈 X4 電子書 reader 的繁體中文韌體。</strong><br>
思源黑體子集 · UI 多語系 · 藍芽翻頁 · 大書能開 · 直排閱讀</p>

<p align="center">
  <img src="https://img.shields.io/badge/license-MIT_%2B_PolyForm_NC-D4A5A5?style=flat-square" alt="Dual License">
  <img src="https://img.shields.io/badge/platform-ESP32--C3-B8A9C9?style=flat-square" alt="ESP32-C3">
  <img src="https://img.shields.io/badge/font-Source_Han_Sans_TC-A8B5A0?style=flat-square" alt="Source Han Sans TC">
  <img src="https://img.shields.io/badge/stage-15.48-E8B4B8?style=flat-square" alt="Stage 15.48">
</p>

<p align="center">
  <a href="README.md">English</a> | <b>繁體中文</b>
</p>

---

> **雙重授權聲明** — 上游 code 維持 MIT（可商用）。
> HelloRuru 從 stage 8 到 stage 15.48 的修改採用 PolyForm Noncommercial 1.0.0。
> HelloRuru 修改部分的商業使用需另外取得授權，請洽 <hello@helloruru.com>。
> 詳見 [LICENSE](LICENSE)。

---

## 這是什麼

**閱星曈 X4** 是一款 ESP32-C3 電子書 reader，320KB RAM、16MB flash、無 PSRAM。原廠韌體（ChineseType）主要面向簡中使用者，也包含不少繁中讀者不需要的雲端服務。

這個 fork 把 X4 變成適合繁中使用者的閱讀器，同時保留可切換簡中與英文 UI 的基礎：

- **Source Han Sans TC / 思源黑體 TC 子集字型**，UI、書名、reader 內文皆已切換。
- **UI 多語系**，介面文字集中於 `src/LanguageMapper.h`，支援繁中、簡中、English。
- **藍芽 HID 翻頁修復**，包含 RPA 判斷、vector 裸指標 race、disconnect double-free、阻塞掃描與 disable callback 保護。
- **大書模式**，10MB+、數千章節 EPUB 可開啟與翻頁。
- **HTML void-element 自閉合預處理**，修復部分網文 EPUB 點章節目錄凍結問題。
- **直排閱讀**，EPUB/TXT 支援橫排與直排。
- **底線設定**，橫排上/下線與直排間距可調。
- **自訂字型上傳**，可使用 SD 卡中的 `.epdfont`。
- **書籤系統**，用 spine 與進度對位。
- **Carousel 首頁**，顯示書封、最近閱讀與閱讀進度。
- **HelloRuru 兔兔開機與休眠畫面**。

## 安裝

### 1. 選一個韌體

最新版本在 `release/`：

| 版本 | 適用 | 檔案 | SHA256 |
| :--- | :--- | :--- | :--- |
| **stage15.48** | 最新測試版；思源黑體、UI 多語系、藍芽 disable 修補 | `ruru-reader-tw-stage15.48-20260515.bin` | `66CA892D997A5CE967007F3A978891F4F40440E1F8E48EA9FD34267DEB4ED9AC` |
| **stage15.46** | 最新已確認可用基準版 | `ruru-reader-tw-stage15.46-20260515.bin` | `90E4B8F2A194D3D8B4B32A018F124B8017871FD4D91831F1D59BFC0377F4166B` |

### 2. 用網頁燒錄

用 **Chrome** 開 <https://flasher.crosspoint.world/>（需要 WebSerial）。USB-C 接 X4，選 BIN，按燒錄。

### 3. 或用 ESPTool

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 --baud 921600 \
  write_flash -z 0x10000 release/ruru-reader-tw-stage15.48-20260515.bin
```

### 4. 第一次開機

1. 開機。
2. 進「設定 → 藍芽」啟用藍芽。
3. 掃描，選翻頁器，連線。
4. 之後每次開機會依設定恢復。

> 升級 stage15 系列後，建議刪除 SD 卡 `.crosspoint/`，讓書籍 cache 重建。設定檔會保留；stage15.48 設定檔版本為 `9`，新增語言欄位，預設繁中。

## 實機驗證

### 藍芽翻頁器

| 裝置 | 連線 | 翻頁 | 備註 |
| :--- | :--- | :--- | :--- |
| iDal-10822 | 可用 | 可用，keycode `0x4E` | 主要使用 |
| E1 Control | 可用 | 可用，keycode `0x4B` | 備用 |
| HBTR003-XT | 不穩 | - | RPA 不穩，建議改用 iDal / E1 |

### 大書相容性

| 書 | 大小 | 章節 | 開書 | 翻頁 | 點目錄 |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 一般網文 | 15 MB | - | 可用 | 可用 | 可用 |
| 多章節網文 | 10.25 MB | 2752 | 可用 | 可用 | 可用 |

## UI 文字與字型子集化

介面顯示文字集中在：

```text
src/LanguageMapper.h
```

build 前會自動執行：

```text
scripts/generate_ui_charset.py
scripts/generate_ui_font_subset.py
```

流程如下：

1. 掃描 `src/` 內的 UI 字串。
2. 從 `LanguageMapper` 讀取繁中、簡中、英文介面文字。
3. 產生 `scripts/charsets/ui_charset.txt` 與 `scripts/charsets/ui_charset_merged.txt`。
4. 使用 Source Han Sans TC / 思源黑體 TC 來源字型產生子集。
5. 重寫以下內建字型 header：

```text
lib/EpdFont/builtinFonts/source_han_sans_tc_10_regular.h
lib/EpdFont/builtinFonts/source_han_sans_tc_12_regular.h
lib/EpdFont/builtinFonts/source_han_sans_tc_17_regular.h
```

目前字集策略：

| 字級 | 用途 | 字集 |
| :--- | :--- | :--- |
| 10pt | 外部文字、檔名、狀態、小字 | common7000 + UI 三語 |
| 12pt | 設定頁、標題、主要 UI | 純 UI 三語 |
| 17pt | 書名與 reader 內文 | common7000 + UI 三語 |

目前 stage15.48 編譯結果：

| 項目 | 數值 |
| :--- | :--- |
| RAM | `110,924 / 327,680 bytes`，約 `33.9%` |
| Flash | `4,532,080 / 6,553,600 bytes`，約 `69.2%` |
| BIN | `4,719,872 bytes` |

## 專案結構

```text
ruru-reader-tw/
├── lib/
│   ├── EpdFont/                 # 內建字型與 fontconvert 工具
│   ├── Epub/                    # EPUB 解析與分頁
│   ├── GfxRenderer/             # 畫面渲染與直排文字
│   └── hal/
│       └── BluetoothHIDManager  # 藍芽 HID 翻頁器
├── src/
│   ├── LanguageMapper.h         # UI 多語系文字表
│   ├── CrossPointSettings.*     # 設定儲存，含 uiLanguage
│   ├── SettingsLists.h          # 設定頁項目
│   ├── activities/
│   │   ├── home/                # Carousel / 最近閱讀
│   │   ├── reader/              # EPUB/TXT/XTC reader
│   │   └── settings/            # 設定頁、藍芽、字型、cache
│   └── components/
│       └── themes/              # Lyra / Flow theme
├── scripts/
│   ├── generate_ui_charset.py
│   ├── generate_ui_font_subset.py
│   └── charsets/
└── release/                     # 預先 build 好的 BIN（預設在 .gitignore）
```

## 客製化

| 想改什麼 | 去哪裡看 |
| :--- | :--- |
| UI 翻譯 | `src/LanguageMapper.h` |
| 語言選單 | `src/SettingsLists.h` 與 `CrossPointSettings::uiLanguage` |
| 字型子集 | `scripts/charsets/ui_charset_common7000.txt` 與 `scripts/generate_ui_font_subset.py` |
| Reader 字型 | `src/main.cpp` 中的 `source_han_sans_tc_17_regular` |
| 藍芽 keycode | `lib/hal/DeviceProfiles.cpp` |
| Reader menu 項目 | `src/activities/reader/EpubReaderMenuActivity.h` |
| 跳過自動重連窗口 | `BluetoothHIDManager::_uiBluetoothActive` |

## 設計理念

1. **NimBLE 地址型別自判**
   從 MAC byte 0 bit 7:6 自己判斷，不完全信任 `getAddressType()`。RPA 地址會被識別成 RANDOM，控制器才找得到。

2. **零手動釋放 BLE client**
   不手動呼叫 `NimBLEDevice::deleteClient`，client 生命週期交給 NimBLE。

3. **HTML 預處理優先於大換 parser**
   在 expat 前跑小型 state machine 補 void element 自閉斜線，降低改動範圍。

4. **字型依用途拆字集**
   12pt UI 不塞常用七千字，10pt 外部文字與 17pt reader 才使用 common7000，避免 BIN 變肥。

5. **UI 多語系先集中再逐步接線**
   新增文字集中放 `LanguageMapper`。已接 mapper 的畫面會跟著語言切換；仍有舊硬編字串需要後續整理。

## 致謝

| 上游 | 作者 | 貢獻 |
| :--- | :--- | :--- |
| [crosspoint-reader](https://github.com/daveallie/crosspoint-reader) | Dave Allie | 原始 MIT 授權 reader |
| CrossInk | uxjulia | 中間 fork |
| CrossInk-Carousel | chintanvajariya | UI theme 系統（Lyra / Flow / 3Covers） |
| [crosspoint-chinesetype](https://github.com/icannotttt/crosspoint-chinesetype) | icannotttt | 中文化、堅果雲、KOReaderSync、藍芽框架 |
| [crosspoint-chinesetype](https://github.com/SamLaio/crosspoint-chinesetype) | SamLaio | 並行繁中 fork，直排與 UI 多語系參考 |

字型：

- [Source Han Sans / 思源黑體](https://github.com/adobe-fonts/source-han-sans) by Adobe，SIL Open Font License 1.1。
- 舊版使用 [jf-openhuninn](https://justfont.com/huninn/) by justfont，CC BY 4.0。

## 需求

- ESP32-C3 + 16MB flash + 電子紙顯示器（X4 硬體）
- 藍芽 HID 翻頁器，或使用實體按鈕
- SD 卡放書本、cache 與自訂字型

## 授權

本專案採用**雙重授權**模式。完整條款請見 [LICENSE](LICENSE)。

| 程式碼來源 | 授權 | 可商用？ |
| :--- | :--- | :--- |
| 上游（Dave Allie、uxjulia 等） | MIT | 可以 |
| HelloRuru 修改（stage 8-15.48） | PolyForm Noncommercial 1.0.0 | 須另外取得商業授權 |
| Source Han Sans / 思源黑體 | SIL Open Font License 1.1 | 可依 OFL 使用 |
| jf-openhuninn 舊版字型 | CC BY 4.0 | 須標註出處 |

個人刷機、學習研究、非營利用途、原始碼修改與再散布可以使用。
HelloRuru 修改部分如需商業使用，請洽 <hello@helloruru.com>。

---

<p align="center">
  <sub>由 <a href="https://github.com/HelloRuru">HelloRuru</a> 製作 · 2026 · 給繁體中文讀者</sub>
</p>
