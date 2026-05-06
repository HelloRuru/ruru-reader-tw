<!--
Thanks for contributing to ruru-reader-tw!
感謝貢獻！

Use English or Traditional Chinese. Both are welcome.
英文或繁體中文都可以。
-->

## Summary / 摘要

<!-- One or two sentences. What does this PR do? / 一兩句話說 PR 做了什麼。 -->

## Type of change / 改動類型

- [ ] Bug fix (non-breaking change which fixes an issue)
- [ ] New feature (non-breaking change which adds functionality)
- [ ] Breaking change (fix or feature that would cause existing functionality to not work as expected)
- [ ] Documentation only / 純文件
- [ ] Refactor / 重構（不改變功能）
- [ ] Build / tooling / config

## Stage label / Stage 標籤

<!-- If this is a coherent feature group, propose a stage label (e.g. stage13, stage12.7).
若這是一組相關改動，提一個 stage 標籤。 -->

Proposed stage:

## Affected branches / 影響的分支

- [ ] `stage7/epub-fast-open` (繁中 main)
- [ ] `stage5.5/jianti` (簡中 main)
- [ ] `stage12.5/*` (current recommended)
- [ ] `stage11/jianti-with-jianguo` (stageA, friendly fork)
- [ ] Other:

## Verified on hardware / 已實機驗證

- [ ] Built successfully (繁中) — Flash usage: __%
- [ ] Built successfully (簡中) — Flash usage: __%
- [ ] Flashed to physical X4
- [ ] Bluetooth page-turner test (which device?):
- [ ] Large EPUB test (which book?):
- [ ] No regression on previously-working features

## Code checklist / 程式碼檢查

- [ ] No hardcoded local paths (no `C:\Users\...`)
- [ ] No API keys, tokens, or credentials
- [ ] No personally-identifying info beyond the author byline
- [ ] Comments where the *why* is non-obvious (not commenting the *what*)
- [ ] If touching `BluetoothHIDManager`, tested re-pair flow with a real device
- [ ] If touching `lib/Epub/`, tested with both small (<1MB) and large (>10MB) EPUBs
- [ ] If touching `lib/EpdFont/`, regenerated affected `.h` files via `fontconvert.py`

## Notes / 備註

<!-- Anything reviewers should know. Tradeoffs? Limitations? Future work?
評審需要知道什麼？取捨？限制？未來可改進的點？ -->
