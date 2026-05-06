<h1 align="center">ruru-reader-tw</h1>

<p align="center"><strong>Traditional Chinese firmware for the YueXingTong X4 e-reader.</strong><br>
jf-openhuninn rounded font · full Bluetooth fixes · large-EPUB support · bookmarks</p>

<p align="center">
  <img src="https://img.shields.io/badge/license-MIT-D4A5A5?style=flat-square" alt="MIT License">
  <img src="https://img.shields.io/badge/platform-ESP32--C3-B8A9C9?style=flat-square" alt="ESP32-C3">
  <img src="https://img.shields.io/badge/font-jf--openhuninn-A8B5A0?style=flat-square" alt="jf-openhuninn">
  <img src="https://img.shields.io/badge/stage-12.6-E8B4B8?style=flat-square" alt="Stage 12.6">
</p>

<p align="center">
  <b>English</b> | <a href="README.zh-TW.md">繁體中文</a>
</p>

---

## :brain: What this is

The **YueXingTong X4** (閱星曈 X4) is an ESP32-C3 e-reader with 320KB RAM, 16MB flash, no PSRAM. The stock firmware (ChineseType) is built for Simplified Chinese users and bundles services that Traditional Chinese readers don't need.

This fork makes the X4 a great reader for **Traditional Chinese users**:

- :sparkles: **jf-openhuninn rounded font** with full punctuation, digits, and Latin glyphs (11288 chars)
- :wrench: **Four upstream Bluetooth crash bugs fixed** (RPA mishandling, vector-of-raw-pointers race, double-free on disconnect, blocking scan)
- :detective: **NimBLE `getAddressType()` workaround** — Resolvable Private Addresses (RPA) were misreported as PUBLIC, causing 30s connect timeouts
- :books: **Large-EPUB support** — books up to 15MB and 2752+ chapters now open
- :anchor: **HTML void-element self-close pre-pass** — fixes the dreaded "tap chapter TOC and the reader freezes" bug on web novels
- :bookmark: **Bookmarking system** with spine-aligned jump-to-bookmark
- :art: **3×3 home grid** with cover art and progress bars
- :globe_with_meridians: **Auto Simplified→Traditional conversion** via OpenCC `s2twp`

## :package: Installation

### 1. Choose a firmware

Latest builds live in `release/`:

| Build                    | Use case                                  | File                                       |
| :----------------------- | :---------------------------------------- | :----------------------------------------- |
| **Traditional Chinese**  | Daily driver (rounded font, punctuation)  | `ruru-reader-tw-stage12.6-20260505.bin`    |
| **Simplified Chinese**   | Keep upstream cloud services              | `ruru-reader-cn-stageA-20260505.bin`       |

### 2. Flash via web (recommended)

Open <https://flasher.crosspoint.world/> in **Chrome** (WebSerial required). Connect the X4 via USB-C, pick the BIN, click flash.

### 3. Or flash via ESPTool (CLI)

```bash
esptool.py --chip esp32c3 --port /dev/ttyUSB0 --baud 921600 \
  write_flash -z 0x10000 release/ruru-reader-tw-stage12.6-20260505.bin
```

### 4. First-boot setup

1. Power on
2. Settings → Bluetooth → **Enable Bluetooth**
3. Scan → pick your page-turner → Connect
4. Subsequent boots auto-reconnect

> :information_source: When upgrading firmware, consider deleting `.crosspoint/` on the SD card to avoid stale chapter caches. Settings (BT state, fonts, margins) are preserved.

## :wrench: Verified hardware

### Bluetooth page-turners

| Device       | Connect             | Page-turn                                | Notes                                                                    |
| :----------- | :------------------ | :--------------------------------------- | :----------------------------------------------------------------------- |
| iDal-10822   | :white_check_mark:  | :white_check_mark: keycode `0x4E`        | Daily driver                                                             |
| E1 Control   | :white_check_mark:  | :white_check_mark: keycode `0x4B`        | Backup                                                                   |
| HBTR003-XT   | :x:                 | —                                        | RPA instability (NimBLE / hardware limitation, not fixable in firmware)  |

### Large EPUB compatibility

| Book                                 | Size      | Spine items | Open                | Page-turn           | Tap TOC             |
| :----------------------------------- | :-------- | :---------- | :------------------ | :------------------ | :------------------ |
| Web novel (no chapter index)         | 15 MB     | —           | :white_check_mark:  | :white_check_mark:  | :white_check_mark:  |
| Multi-chapter web novel              | 10.25 MB  | 2752        | :white_check_mark:  | :white_check_mark:  | :white_check_mark:  |

## :open_file_folder: Project structure

```text
ruru-reader-tw/
├── lib/
│   ├── Epub/                    # EPUB parser (Section, ChapterHtmlSlim, ParsedText)
│   │   └── Epub/Section.cpp     # HTML void-element self-close pre-pass (the big fix)
│   ├── hal/
│   │   └── BluetoothHIDManager  # 4-way BT fix + RPA workaround + reconnect
│   └── PngToBmpConverter/       # streaming PNG decoder (no OOM on big covers)
├── src/
│   ├── activities/
│   │   ├── home/                # 3×3 grid recent books
│   │   ├── reader/              # EpubReader, BookmarkActivity, EpubBookmarkSelection
│   │   └── settings/            # BluetoothSettings (with _uiBluetoothActive flag)
│   ├── components/
│   │   └── icons/               # 12 new icons for grid view
│   └── CrossPointSettings.cpp   # Bluetooth state persistence fix
├── scripts/
│   └── charsets/charset_full.txt  # 11288-char font subset (with punctuation)
└── release/                     # Pre-built BINs (in .gitignore by default)
```

## :wrench: Customizing

| Want to change              | Look at                                                                          |
| :-------------------------- | :------------------------------------------------------------------------------- |
| Font subset                 | `scripts/charsets/charset_full.txt` + run `lib/EpdFont/scripts/fontconvert.py`   |
| Bluetooth keycodes          | `lib/hal/DeviceProfiles.cpp` (per-device profile)                                |
| Reader menu items           | `src/activities/reader/EpubReaderMenuActivity.h`                                 |
| Default theme               | `src/CrossPointSettings.h` `uiTheme` initializer                                 |
| Skip auto-reconnect window  | `BluetoothHIDManager::_uiBluetoothActive` flag                                   |

## :bulb: Design rationale

**Why fork at all?** Upstream ChineseType has the BLE manager UI commented out and bookshelf list-only — these are signals the upstream author knows there are issues but worked around them. We chose to actually fix the underlying bugs:

1. **NimBLE address-type heuristic** — read MAC byte 0 bits 7:6 instead of trusting `getAddressType()`. RPA addresses (`01`) become RANDOM, which the controller can actually find.
2. **HTML pre-pass over expat** — instead of swapping XML parsers (huge change), we run a 1024-byte buffered state machine that auto-closes void elements. Adds ~3KB flash, parses 100KB chapters in under 50ms.
3. **Bookmark spine+page** — store spine index + page count + progress percent; on jump, prefer spine when chapter count matches, fallback to percent. Survives layout reflow.
4. **3×3 grid** — shows 9 covers per page instead of 5 list items. Better information density on a 480×800 e-ink panel.

## :pray: Acknowledgments

This project stands on the shoulders of:

| Upstream                                                                       | Author           | Contribution                                                       |
| :----------------------------------------------------------------------------- | :--------------- | :----------------------------------------------------------------- |
| [crosspoint-reader](https://github.com/daveallie/crosspoint-reader)            | Dave Allie       | Original MIT-licensed reader                                       |
| CrossInk                                                                       | uxjulia          | Intermediate Chinese-friendly fork                                 |
| CrossInk-Carousel                                                              | chintanvajariya  | UI theme system (Lyra / Flow / 3Covers)                            |
| [crosspoint-chinesetype](https://github.com/icannotttt/crosspoint-chinesetype) | icannotttt       | Chinese localization, JianGuo cloud, KOReader sync, BLE skeleton   |

Font: [jf-openhuninn](https://justfont.com/huninn/) by **justfont** — CC BY 4.0.

Inspiration for the HTML self-close approach came from a parallel fork; implementation here is original.

## :scroll: Requirements

- ESP32-C3 + 16MB flash + e-ink display (the X4 hardware)
- A Bluetooth HID page-turner (or use the side buttons)
- SD card for books and caches

## :balance_scale: License

Released under **MIT License**. See [LICENSE](LICENSE).

This project inherits MIT from the upstream chain. All modifications are also MIT.

---

<p align="center">
  <sub>Made by <a href="https://github.com/HelloRuru">HelloRuru</a> · 2026 · for Traditional Chinese readers</sub>
</p>
