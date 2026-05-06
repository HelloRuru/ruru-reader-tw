# Contributing to ruru-reader-tw

> English instructions first; 繁體中文版在下半部。

## English

### What this project is — and isn't

**Is**: an opinionated firmware fork tuned for Traditional Chinese readers on the YueXingTong X4 e-reader. We fix bugs, add features useful to TC readers, and try not to make a mess.

**Isn't**: a general-purpose framework or a vendor-neutral upstream. We will reject contributions that compromise the experience for the target audience to support adjacent use cases.

### Before you start

1. **Read the [CHANGELOG](CHANGELOG.md)** to understand stage history.
2. **Pick a base branch** (current recommended: `stage12.5/epub-fast-open` for 繁中, `stage12.5/jianti` for 簡中).
3. **Open an issue first** for non-trivial changes. Avoid surprise PRs that touch the EPUB parser or Bluetooth manager — those are load-bearing.

### Build environment

You'll need:

- [PlatformIO](https://platformio.org/) (CLI or VSCode extension)
- ESP32-C3 toolchain (PlatformIO will fetch this on first build)
- Python 3.10+ (for `fontconvert.py` if you touch fonts)
- Git

Build:

```bash
pio run                              # full build
pio run -t upload                    # build + flash via USB
pio run -t monitor                   # serial monitor at 115200 baud
```

The output BIN ends up at `.pio/build/default/firmware.bin` — copy it into `release/` with a stage-tagged name when releasing.

### Code style

- **C++**: match the surrounding style. `const` correctness matters. Avoid raw `new` / `delete` outside hardware managers; prefer `std::unique_ptr`.
- **Comments**: write the *why*, not the *what*. The compiler already knows the *what*.
- **Encoding**: UTF-8 with BOM-less. Traditional Chinese in user-facing strings is fine on `stage7/*` branches; Simplified Chinese on `stage5.5/jianti`. Don't mix within a single string.
- **Stage tags in commits**: prefix your commit with `feat(stageN)`, `fix(stageN)`, `chore(stageN)` etc. This keeps `git log --oneline` legible.

### Branching model

We don't use feature branches off `main` — we use **stage branches** that capture coherent feature groups:

```text
stage7/epub-fast-open       Traditional Chinese, current latest
stage5.5/jianti              Simplified Chinese, current latest
stage12.5/*                  Recommended stable
stage11/jianti-with-jianguo  Friendly fork preserving JianGuo / KOReader
```

Branch your PR from the appropriate stage branch. We'll merge via cherry-pick to the parallel branch (TC ↔ SC) when it makes sense.

### Testing requirements

PRs touching these subsystems must include hardware verification:

| Subsystem                    | Verify with                                                               |
| :--------------------------- | :------------------------------------------------------------------------ |
| `BluetoothHIDManager`        | Real BLE page-turner: connect → page-turn → re-pair after power cycle     |
| `lib/Epub/`                  | Small EPUB (<1MB) + large EPUB (>10MB, >1000 spine items)                 |
| `lib/EpdFont/`               | Regenerate affected `.h` via `fontconvert.py`; check Flash usage delta    |
| `RecentBooksActivity`        | Add 10+ recent books, verify grid pagination                              |
| Settings / persistence       | Reboot device, confirm settings survive                                   |

We don't have CI for hardware tests. Honesty is the policy: don't claim you tested something you didn't.

### License of contributions

By submitting a PR, you agree your changes are licensed under the same MIT license as this project, and that you have the right to grant that license.

---

## 繁體中文

### 這是什麼專案、不是什麼

**是**：閱星曈 X4 的繁中使用者向的 fork。修 bug、加繁中讀者有用的功能、盡量不弄亂。

**不是**：通用框架，也不是中立上游。為了支援邊緣用例而傷害主要使用體驗的貢獻會被拒絕。

### 開始之前

1. **讀 [CHANGELOG](CHANGELOG.md)** 了解 stage 歷史
2. **選對基底分支**（推薦：繁中用 `stage12.5/epub-fast-open`，簡中用 `stage12.5/jianti`）
3. **大改動先開 issue 討論**。動 EPUB parser 或藍芽 manager 之前一定要先說一聲。

### Build 環境

需要：

- [PlatformIO](https://platformio.org/)（CLI 或 VSCode extension）
- ESP32-C3 toolchain（PlatformIO 會自動下載）
- Python 3.10+（要動字型才需要）
- Git

Build 指令：

```bash
pio run                              # 完整 build
pio run -t upload                    # build + USB 燒錄
pio run -t monitor                   # serial monitor 115200 baud
```

BIN 在 `.pio/build/default/firmware.bin`，發布時複製到 `release/` 並改名加 stage 標籤。

### 程式碼風格

- **C++**：跟周圍程式碼一致。`const` correctness 重要。硬體 manager 之外避免裸 `new` / `delete`，優先用 `std::unique_ptr`。
- **註解**：寫**為什麼**，不是**做什麼**。compiler 已經知道在做什麼。
- **編碼**：UTF-8，無 BOM。`stage7/*` 分支的使用者文字用繁中，`stage5.5/jianti` 用簡中。同一字串不要混。
- **commit 前綴**：`feat(stageN)` / `fix(stageN)` / `chore(stageN)`，方便看 `git log --oneline`。

### 分支模型

不用 main + feature branch，用 **stage 分支**：

```text
stage7/epub-fast-open       繁中目前最新
stage5.5/jianti              簡中目前最新
stage12.5/*                  推薦的穩定版
stage11/jianti-with-jianguo  保留堅果雲 / KOReader 的友善 fork
```

PR 從對應 stage 分支開。合理時我們會 cherry-pick 到對側分支（繁中 ↔ 簡中）。

### 測試要求

動到這些子系統的 PR，必須做實機驗證：

| 子系統                       | 怎麼驗                                                            |
| :--------------------------- | :---------------------------------------------------------------- |
| `BluetoothHIDManager`        | 真翻頁器：連線 → 翻頁 → 重啟後重新配對                            |
| `lib/Epub/`                  | 小書（<1MB）+ 大書（>10MB、>1000 spine）                          |
| `lib/EpdFont/`               | 用 `fontconvert.py` 重產字型 `.h`；檢查 Flash 用量變化            |
| `RecentBooksActivity`        | 加 10+ 本最近閱讀，驗 grid 翻頁                                   |
| 設定 / 持久化                | 重啟裝置，確認設定保留                                            |

沒 CI 跑實機測試，**誠實是規則**：沒測過的東西不要說測過。

### 貢獻的授權

送 PR 即代表你同意改動採用本專案 MIT 授權，且你有權授權這個改動。
