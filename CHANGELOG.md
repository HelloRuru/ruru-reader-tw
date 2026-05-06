<!-- markdownlint-disable MD024 -->
# Changelog

All notable changes to **ruru-reader-tw** are documented here.

The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

This project is forked from [crosspoint-chinesetype](https://github.com/icannotttt/crosspoint-chinesetype) and primarily targets **Traditional Chinese** users on the YueXingTong X4 e-reader.

---

## [stage12.6] — 2026-05-05

### Added

- **jf-openhuninn font subset now includes punctuation, digits, and Latin glyphs.**
  - Character set expanded from 11149 → 11288 characters.
  - Added: full-width punctuation (`，。！？：；「」『』（）《》、…—`), ASCII punctuation, half-width and full-width digits, Latin A–Z / a–z, smart quotes (`'' ""`), bracket variants (`【】〔〕〈〉`).
  - Fixes the long-standing bug where book titles in list views displayed without punctuation (e.g. `《Book Title》` rendered as `Book Title` with brackets stripped).

### Fixed

- N/A. Pure font-subset extension — no code changes.

### Notes

- **Traditional Chinese only.** The Simplified Chinese build does not use jf-openhuninn (it uses ChineseType's bundled `notosans_18_bold` which already includes GB2312 punctuation).
- Flash usage: 93.8% → 93.9% (+1 KB).

---

## [stage12.5] — 2026-05-05

### Added

- **Bookmark system.**
  - New activities: `BookmarkActivity`, `BookmarkStore`, `EpubBookmarkSelectionActivity`.
  - Reader Menu now includes **Add Bookmark** and **Bookmark List** entries.
  - Bookmarks store spine index + page + page count + progress percent. Jump-to-bookmark prefers spine when chapter count matches; falls back to percent for resilience after font/layout changes.
- **3×3 grid for the home screen recent-books view.**
  - Replaces the previous list view. Shows 9 books per page with cover thumbnails and progress bars.
  - 12 new icons added under `src/components/icons/` (`book24`, `cog`, `file24`, `folder24`, `image24`, `pen_15`, `settings`, `text24`, `txt_24`, `wifi_32`, `xtc_24`, `cloudy_32`).

### Changed

- `RecentBook` struct extended with `int progressPercent` (default `-1` = unknown).

### Removed

- **OpenDyslexic font.** Not used by Traditional Chinese readers; saves a few KB.
- **RoundedRaff theme stub** (commented-out remnants from earlier stages cleaned up).

### Notes

- This release **fixes** the open-book regression introduced in stage12.
- The `ParsedText::isEffectivelyEmpty()` shortcut and `ChapterHtmlSlim` skip-empty-block logic from the original stage12 / stage13 are intentionally **not** included (they caused chapter pages to fail to render when the first block was an invisible-only `<p>`).
- Flash usage: 93.8% (繁中) / 95% (簡中).

---

## [stage11] — 2026-05-05

### Fixed

- **HTML void-element self-close pre-pass.** Resolves the "tap chapter TOC and the reader freezes" bug on large EPUBs.
  - Many EPUB chapter files contain non-self-closed void elements (`<br>`, `<img src="x">`, `<hr>`, etc.). expat (a strict XML parser) rejects the entire file with a "mismatched tag" error, causing chapters to fail to load.
  - Solution: a new `selfCloseVoidElementsInHtmlFile()` function in `lib/Epub/Epub/Section.cpp` runs the chapter HTML through a 1024-byte buffered state machine, automatically inserting `/` before `>` for void elements. Output is fed to expat, which then parses successfully.
  - Covers HTML5 void elements: `area`, `base`, `br`, `col`, `embed`, `hr`, `img`, `input`, `link`, `meta`, `param`, `source`, `track`, `wbr`.
  - Skips `<! ... >` and `<? ... ?>` blocks (DOCTYPE, comments, processing instructions, CDATA).
  - On preprocessing failure, falls back to the original file (no regression risk).

### Notes

- Flash usage: +3 KB.

---

## [stageA] — 2026-05-05

A "friendly fork" branch (`stage11/jianti-with-jianguo`) for users who want the upstream Chinese cloud services preserved.

### Added

- All fixes through stage11 (Bluetooth + large books + HTML self-close).

### Removed

- Nothing. JianGuo cloud, KOReaderSync, OPDS Browser, OTA Update, OpenDyslexic — **all retained**.

### Use case

- Friends who want the bug fixes but rely on JianGuo cloud or KOReaderSync for cross-device sync.
- Distributing source code to upstream maintainers (preserves their work).

---

## [stage10] — 2026-05-05

### Removed

- **JianGuo (堅果雲) WebDAV browser** — China-only cloud service, not used by Traditional Chinese readers.
- **KOReader sync** — niche feature, not used by target audience.
- **OPDS Browser + Calibre settings.**
- **CLASSIC theme** (UITheme falls back to LYRA_FLOW).

### Notes

- Flash usage: 94.7% → 93.7% (saved ~68 KB).
- Simplified Chinese build keeps everything (target users may want JianGuo).

---

## [stage9.1] — 2026-05-05

### Fixed

- **Bluetooth enable state is now persistent across reboots.**
  - `CrossPointSettings::loadFromFile()` had `bluetoothEnabled = 0` hard-coded at the end (upstream defensive workaround for crash bugs that no longer exist after stage7). Removed.
- **Bluetooth settings UI no longer freezes when switching to a new page-turner.**
  - `checkAutoReconnect()` was triggering `connectToDeviceWithRetries(oldDevice, 1)` (30s NimBLE timeout) in the background while the user was browsing the device list, locking up the UI.
  - Solution: added `_uiBluetoothActive` flag. `BluetoothSettingsActivity::onEnter` sets it `true`, `onExit` sets it `false`. `checkAutoReconnect()` early-returns when the flag is set.

---

## [stage9] — 2026-05-05

### Fixed

- **NimBLE `getAddressType()` misreports RPA addresses as PUBLIC.** Causes 30-second connect timeouts (NimBLE error reason 534, `BLE_ERR_HCI_CONN_FAILED_TO_BE_ESTABLISHED`).
  - Affects Resolvable Private Address (RPA) devices like HBTR003-XT page-turners.
  - Solution: read MAC byte 0 bits 7:6 directly. `0b11` = static random, `0b01` = RPA, `0b00` = NRPA — all map to `BLE_ADDR_RANDOM`. Anything else = `BLE_ADDR_PUBLIC`. Override NimBLE's reported type when the heuristic disagrees.
  - Verified: iDal-10822 connects reliably; HBTR003-XT still flaky due to RPA address rotation (hardware-level limitation, not firmware).
- **Bluetooth disconnect race condition + stale connection state.**
  - Added `onClientDisconnected` callback to NimBLE `ClientCallbacks`.
  - Added `pruneDisconnectedDevices()` periodic cleanup.
  - Disconnect now removes from `_connectedDevices` *before* calling `client->disconnect()` to avoid dangling pointers after `setSelfDelete(true, true)` cleans up.
  - Added `_manualDisconnectSuppressUntil` to prevent immediate auto-reconnect after manual disconnect.

---

## [stage8.1-3] — 2026-05-05

### Added

- **Large-Book Mode for spine items > 500.**
  - Previously, large web novels (10MB+, 2000+ chapters) failed to open because `buildBookBin()` would parse the entire ncx/nav TOC (often 100KB+ XML), exhausting RAM.
  - Solution: when spine count exceeds 500, skip the TOC pass during `buildBookBin()`. Use spine href as a placeholder chapter title.
  - TOC is later loaded lazily (when the user opens the chapter selection menu), parsing only what's needed.
- **Chapter selection fallback.** When in Large-Book Mode without a parsed TOC, the chapter selection menu uses spine href as the chapter label.

---

## [stage7] — 2026-05-05

### Fixed

- **Four upstream Bluetooth crash bugs.** Upstream ChineseType had the entire Bluetooth UI commented out — for a reason.
  - **A. Hard-coded `BLE_ADDR_RANDOM`.** `NimBLEAddress(addr, BLE_ADDR_RANDOM)` was being passed to `connect()` regardless of the actual device address type. Devices with public addresses fell into a NimBLE failure path that triggered a cleanup race condition. Fix: pass the address type recorded during scan.
  - **B. `std::vector<ConnectedDevice>` reallocation race.** `ConnectedDevice` contained raw `NimBLEClient*` pointers; when the vector reallocated, NimBLE callbacks could still hold references to the old memory. Fix: `_connectedDevices.reserve(4)` to bound reallocations.
  - **C. Double-free on disconnect.** `pClient->setSelfDelete(false, false)` plus 5 manual `deleteClient()` calls caused double-free (NimBLE's own `onDisconnect` cleanup ran first). Fix: `setSelfDelete(true, true)` and remove all manual `deleteClient()` calls.
  - **D. Blocking BLE scan.** Scan was using `delay(durationMs)` to wait for completion, freezing the UI for up to 10 seconds. Fix: use NimBLE's own scan-end callback (`_setScanningFinished()`).

---

## [stage6] — 2026-05-05

### Changed

- **Re-enabled the Bluetooth UI entry** in `SettingsActivity` and `SettingsLists`. Upstream had it commented out due to the crash bugs fixed in stage7.

---

## [stage5.5] — 2026-05-05

### Added

- **PNG cover support** with streaming pipeline (`lib/PngToBmpConverter/`). Handles covers up to 1500×2000 without OOM by decoding row-by-row.
- **EPUB 3 `<item properties="cover-image">` recognition.** Upstream parser only recognized EPUB 2's `<meta name="cover">`, so most EPUB 3 books (web novels, doujin) showed empty covers.
- **OpenCC `s2twp` simplified-to-traditional conversion** for chapter text.

### Fixed

- **Empty cover bitmap regression.** When `Bitmap.cpp::readNextRow()` failed, it would write a zero-byte BMP, leading to "white square" covers. Fix: do not write the cache file on failure.

### Removed

- **RoundedRaff theme** (had a blank menu rendering bug; saving for a future fix).

---

## Pre-stage5 (upstream)

Inherited from [crosspoint-chinesetype](https://github.com/icannotttt/crosspoint-chinesetype) and the upstream chain (CrossInk-Carousel → CrossInk → crosspoint-reader). See upstream repos for their changelog.
