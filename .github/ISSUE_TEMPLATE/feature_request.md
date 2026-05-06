---
name: Feature request / 功能建議
about: Suggest a new feature for ruru-reader-tw / 建議新功能
title: '[Feature] '
labels: enhancement
assignees: ''
---

## What you want / 你想要什麼

<!--
A clear description of the feature. Example: "Auto-rotate page based on accelerometer."
簡短描述。例：「依重力感測器自動轉頁面方向。」
-->

## Why / 為什麼

<!--
What's the use case? Who benefits? What does it enable?
這個功能解決什麼問題？誰受惠？能多做什麼事？
-->

## Where to look / 從哪裡下手（如果你有頭緒）

<!--
If you've poked around the code and have ideas about implementation, share here.
如果你看過程式碼有實作想法，可以分享。
-->

## Constraints to remember / 限制提醒

The X4 is **resource-constrained**:

- ESP32-C3 with 320 KB RAM, **no PSRAM**
- 16 MB flash, app partition ~6.25 MB (currently 89–95% used depending on build)
- Single-core 160 MHz
- E-ink display, slow refresh

X4 是**資源有限**的硬體：

- ESP32-C3、320 KB RAM、**沒有 PSRAM**
- 16 MB flash，app 分區 ~6.25 MB（目前用 89–95%）
- 單核 160 MHz
- 電子紙、刷新慢

If your feature would push us over flash or stretch RAM, please flag it. /
如果功能會擠爆 flash 或 RAM，請在 issue 裡標一下。

## Out of scope / 不會做的事

- Anything requiring a paid API or recurring cost
- Anything requiring China-only cloud services (use the `stageA` branch if you want JianGuo / KOReader)
- Features that compromise reader stability for cosmetic gain
- 任何需要付費 API 或持續花錢的功能
- 中國雲端服務（要堅果雲 / KOReader 請用 `stageA` 分支）
- 為了好看而犧牲穩定的功能
