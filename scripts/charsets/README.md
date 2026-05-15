# ruru-reader-tw UI 字集

這個資料夾收錄了 `ruru-reader-tw`（閱星曈 X4 繁中韌體）使用的字集檔，
可直接給 `fontconvert.py --charset-file` 用來生成 epdfont 子集化字型 `.h` 檔。

## 檔案清單

| 檔案 | 字數 | 用途 |
|------|------|------|
| **`ui_charset_common7000.txt`** ⭐ | **7054** | **stage15.3 介面字型用**（10pt + 12pt）|
| `charset_full.txt` | 11288 | 完整字集（書名 + reader 內文用、17pt）|
| `ui_charset_merged.txt` | 11175 | ChineseType base 累積的繁中字 + 嚕寶介面字 |
| `ui_charset.txt` | 362 | 嚕寶介面實際出現的中文字（從 src/ 自動掃出來） |
| `_edu4808.txt` | 4808 | 教育部常用字 4808 字（來源） |
| `_hanchar.json` | 8105 | 中華民國教育部「異體字字典」結構化字表（參考） |

## `ui_charset_common7000.txt` 組成

```
教育部常用字 4808 字（甲表）
  + ChineseType base 累積的繁中字補充（次常用範圍，補到 6800 漢字）
  + 嚕寶介面實際用字（已含於上述）
  + ASCII 95 字（U+0020-007E：英文 / 數字 / 標點）
  + CJK 標點 64 字（U+3000-303F）
  + 全形符號 95 字（U+FF00-FF5E）
  ──
  = 總共 7054 字
```

## 怎麼用

```bash
cd lib/EpdFont/scripts
python fontconvert.py jfopenhuninn_10_regular 10 \
  ../builtinFonts/source/jf-openhuninn/jf-openhuninn-2.1.ttf \
  --charset-file ../../../scripts/charsets/ui_charset_common7000.txt \
  > ../builtinFonts/jfopenhuninn_10_regular.h
```

## 自動化 build pipeline

`platformio.ini` 已接 pre-script `scripts/build_ui_fonts.py`，
build 時自動：

1. 掃 `src/*.cpp/h` 抓中文字
2. 合併進 charset
3. hash 比對：跟上次一樣 → 跳過、不跑 fontconvert
4. 不一樣 → 重新跑 fontconvert 生成子集字型 .h

## 對外分享版本

如果你要拿這份字集到自己的 e-ink reader 韌體 / epdfont 專案用，
**只需要兩個檔**：

- `ui_charset_common7000.txt`（字集本身）
- `_edu4808.txt`（教育部常用字、原始來源）

授權方式：
- 字集本身：CC0 / Public Domain（嚕寶整理、字本身來自公開字表）
- 教育部常用字表：中華民國教育部公告字表（公共領域）
- ChineseType 補充字：原 `crosspoint-chinesetype` MIT License

## 字型大小比較（jf-openhuninn-2.1）

| 字級 | charset_full（11149 字） | ui_charset_common7000（6800 漢字） | 省 |
|------|--------------------------|------------------------------------|-----|
| 10pt | 3,144,607 bytes | 2,558,369 bytes | -586 KB |
| 12pt | 4,084,153 bytes | 3,269,670 bytes | -814 KB |
| 17pt | 7,236,713 bytes | （不適用 — 17pt 給書名用、需 charset_full）| — |

整體 BIN 從 6.33 MB → 5.04 MB，**省 1.29 MB**。

## 已驗證裝置

- Xteink X4（ESP32-C3、320 KB RAM、16 MB Flash）
- 韌體：ruru-reader-tw stage15.3

## 來源

- [TraditionalChinese/TW-ABCN](https://github.com/TraditionalChinese/TW-ABCN) — 臺灣 TW-ABCN 正字甲乙丙表（PDF + xlsx）
- [Watermelonnn/ChineseUsefulToolKit](https://github.com/Watermelonnn/ChineseUsefulToolKit) — 教育部常用字 4808 字 txt
- [gitqwerty777/Chinese-Characters-Standards](https://github.com/gitqwerty777/Chinese-Characters-Standards) — 中華民國 + 中國教育部漢字標準資料
- [icannotttt/crosspoint-chinesetype](https://github.com/icannotttt/crosspoint-chinesetype) — ChineseType base 字集
