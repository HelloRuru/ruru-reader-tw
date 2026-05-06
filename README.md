# ruru-reader-tw — 简中分支

> 本分支提供闪烁 X4 阅读器的**简体中文**固件。
>
> This branch carries the **Simplified Chinese** firmware for the YueXingTong X4 e-reader.

## 分支说明

主分支（[`main`](https://github.com/HelloRuru/ruru-reader-tw)）是**繁体中文版**（jf-openhuninn 粉圆字型 + 完整标点）。

这个 `jianti` 分支保留 ChineseType 上游的简体中文字型（notosans + ubuntu），并继承所有 bug 修复：

- 4 重蓝牙修复 + RPA 解法 + 自动重连
- 大书模式 + HTML 自闭合预处理（解决章节目录卡死）
- 书签系统 + 3×3 grid

> 注意：简中分支**不含** stage12.6 粉圆字集补标点（粉圆字型只用在繁中分支）。
>
> The `jianti` branch does **not** include stage12.6's punctuation patch (jf-openhuninn is only used in the Traditional Chinese build).

## 韧体下载

请到 [Releases](https://github.com/HelloRuru/ruru-reader-tw/releases) 下载：

- `ruru-reader-tw-stage12.6-*.bin` — 繁体中文版（main 分支对应）
- `ruru-reader-cn-stage12.5-*.bin` — 简体中文版（本 jianti 分支对应）

## 完整文档

详细使用说明、致谢、授权请见 main 分支的 [README.md](https://github.com/HelloRuru/ruru-reader-tw/blob/main/README.md)。
