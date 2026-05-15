"""
build_ui_fonts.py — 在 PlatformIO build 階段自動子集化 UI 字型

流程：
  1. 掃 src/ 所有 .cpp/.h 抓中文字串字面量 → ui_chars
  2. 合併 ui_chars + 風眼預設字集（charset_full.txt 或外部 yuanxia 字集）
     → 產出 scripts/charsets/ui_charset_merged.txt
  3. 比對 merged charset 的 hash 跟上次 build 的 hash
     - 一致 → 跳過，不重 build 字型（省時間）
     - 不一致 → 跑 fontconvert.py 產生 jfopenhuninn_{10,12}_regular_ui.h
  4. 把 ui 子集字型檔放在 lib/EpdFont/builtinFonts/ 底下

設計原則：
  - 不動既有 jfopenhuninn_{10,12}_regular.h（保留完整版以備不時之需）
  - 新檔以 _ui 後綴 (jfopenhuninn_10_regular_ui.h)
  - 是否啟用由 build flag -DUSE_UI_SUBSET_FONT 控制（嚕寶確認效果後手動 enable）
  - 第一次 build 跑全套約 1-2 分鐘，之後增量 build 無感

呼叫時機：PlatformIO 的 pre 階段（在 build 第一行 compile 前執行）
  platformio.ini: extra_scripts = pre:scripts/build_ui_fonts.py
"""
import hashlib
import os
import re
import subprocess
import sys
from pathlib import Path

# PlatformIO 把 pre-script 用 exec() 跑，沒有 __file__
# Fallback: 用 cwd 當 ROOT（PlatformIO 預設 cwd 是專案根目錄）
try:
    ROOT = Path(__file__).parent.parent
except NameError:
    ROOT = Path(os.getcwd())
SRC = ROOT / "src"
CHARSETS_DIR = ROOT / "scripts" / "charsets"
UI_CHARSET = CHARSETS_DIR / "ui_charset.txt"
UI_CHARSET_MERGED = CHARSETS_DIR / "ui_charset_merged.txt"
FULL_CHARSET = CHARSETS_DIR / "charset_full.txt"
HASH_CACHE = ROOT / ".pio" / "ui_font_charset.hash"  # 上次 build 用的 charset hash

FONT_SOURCE = ROOT / "lib" / "EpdFont" / "builtinFonts" / "source" / "jf-openhuninn"
FONT_OUTPUT_DIR = ROOT / "lib" / "EpdFont" / "builtinFonts"
FONTCONVERT = ROOT / "lib" / "EpdFont" / "scripts" / "fontconvert.py"

# UI 用的字級（reader 用 charset_full 不動）
UI_FONT_SIZES = [10, 12]
UI_FONT_STYLES = ["regular"]

CHINESE_RANGE = re.compile(r"[一-鿿]")
STRING_PATTERN = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def scan_ui_chars():
    """掃 src/ 所有 .cpp/.h 中的中文字串字面量"""
    charset = set()
    for path in SRC.rglob("*"):
        if path.suffix not in {".cpp", ".h"}:
            continue
        if not path.is_file():
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except Exception:
            continue
        for match in STRING_PATTERN.finditer(text):
            s = match.group(1)
            for ch in s:
                if CHINESE_RANGE.match(ch):
                    charset.add(ch)
    return charset


def load_full_charset():
    """載入完整字集（給書名 fallback 用）"""
    if FULL_CHARSET.exists():
        return set(FULL_CHARSET.read_text(encoding="utf-8")) - {"\n", "\r", " "}
    return set()


def merged_charset_hash(charset):
    """計算 charset 的 hash（給 incremental build 判斷用）"""
    h = hashlib.sha256()
    for ch in sorted(charset):
        h.update(ch.encode("utf-8"))
    return h.hexdigest()


def run_fontconvert(size, style, charset_file, output_path):
    """跑 fontconvert.py 產 epdfont .h"""
    font_name = f"jfopenhuninn_{size}_{style}_ui"
    font_file = FONT_SOURCE / "jf-openhuninn-2.1.ttf"
    if not font_file.exists():
        # fallback 找其他 ttf
        ttfs = list(FONT_SOURCE.glob("*.ttf"))
        if not ttfs:
            print(f"  ERROR: No .ttf found in {FONT_SOURCE}", file=sys.stderr)
            return False
        font_file = ttfs[0]

    cmd = [
        sys.executable,
        str(FONTCONVERT),
        font_name,
        str(size),
        str(font_file),
        "--charset-file",
        str(charset_file),
    ]
    try:
        result = subprocess.run(cmd, capture_output=True, text=True, encoding="utf-8")
        if result.returncode != 0:
            print(f"  ERROR: fontconvert failed for {font_name}", file=sys.stderr)
            print(result.stderr, file=sys.stderr)
            return False
        output_path.write_text(result.stdout, encoding="utf-8")
        size_kb = output_path.stat().st_size / 1024
        print(f"  Generated {output_path.name} ({size_kb:.1f} KB)")
        return True
    except Exception as e:
        print(f"  ERROR running fontconvert: {e}", file=sys.stderr)
        return False


def main():
    print("[build_ui_fonts] Scanning UI Chinese characters from src/...")

    # 1. 掃 UI 字
    ui_chars = scan_ui_chars()
    print(f"  Found {len(ui_chars)} unique UI Chinese characters")

    # 2. 合併（UI 字 + 完整字集當 fallback，保證書名不缺字）
    full_chars = load_full_charset()
    print(f"  Loaded {len(full_chars)} chars from full charset (for book titles)")
    merged = ui_chars | full_chars
    merged.discard("\n")
    merged.discard("\r")
    merged.discard(" ")
    print(f"  Merged total: {len(merged)} chars")

    # 寫出 merged charset 供後續 fontconvert 用
    CHARSETS_DIR.mkdir(parents=True, exist_ok=True)
    sorted_chars = sorted(c for c in merged if "一" <= c <= "鿿" or "　" <= c <= "〿")
    UI_CHARSET.write_text("".join(sorted(ui_chars)), encoding="utf-8")
    UI_CHARSET_MERGED.write_text("".join(sorted_chars), encoding="utf-8")

    # 3. hash 比對：跟上次一樣 → 跳過
    current_hash = merged_charset_hash(merged)
    HASH_CACHE.parent.mkdir(parents=True, exist_ok=True)
    if HASH_CACHE.exists():
        last_hash = HASH_CACHE.read_text().strip()
        if last_hash == current_hash and all(
            (FONT_OUTPUT_DIR / f"jfopenhuninn_{size}_{style}_ui.h").exists()
            for size in UI_FONT_SIZES
            for style in UI_FONT_STYLES
        ):
            print("[build_ui_fonts] No charset changes since last build, skipping font generation.")
            return

    # 4. 跑 fontconvert 產生子集字型
    print("[build_ui_fonts] Charset changed (or first build), regenerating UI fonts...")
    success_count = 0
    for size in UI_FONT_SIZES:
        for style in UI_FONT_STYLES:
            output = FONT_OUTPUT_DIR / f"jfopenhuninn_{size}_{style}_ui.h"
            if run_fontconvert(size, style, UI_CHARSET_MERGED, output):
                success_count += 1

    total = len(UI_FONT_SIZES) * len(UI_FONT_STYLES)
    if success_count == total:
        HASH_CACHE.write_text(current_hash)
        print(f"[build_ui_fonts] OK ({success_count}/{total} fonts regenerated)")
    else:
        print(f"[build_ui_fonts] WARN: only {success_count}/{total} succeeded", file=sys.stderr)


# PlatformIO pre-script 入口（platformio.ini extra_scripts 會呼叫）
try:
    Import("env")  # noqa: F821 — pio injects
    main()
except NameError:
    # 也支援獨立執行：python scripts/build_ui_fonts.py
    if __name__ == "__main__":
        main()
