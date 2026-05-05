"""
階段 3：簡轉繁腳本
用 OpenCC s2twp 模式把 src/ 內所有 C++ 字串字面量的簡體中文轉成台灣繁體。

只動雙引號內的字串、不動程式碼註解和識別符號。
跑前先 git commit 保護現狀。
"""
import os
import re
import sys
from pathlib import Path

try:
    import opencc
except ImportError:
    print("請先 pip install opencc-python-reimplemented")
    sys.exit(1)

CC = opencc.OpenCC("s2twp")
CHINESE_RANGE = re.compile(r"[一-鿿]")
# 抓 C++ 雙引號字串字面量（含跳脫處理，但保守避開可能的多行）
STRING_PATTERN = re.compile(r'"((?:[^"\\\n]|\\.)*)"')

ROOT = Path(__file__).parent.parent
SRC = ROOT / "src"

def has_chinese(s):
    return bool(CHINESE_RANGE.search(s))

def convert_file(path: Path):
    text = path.read_text(encoding="utf-8")
    changes = []

    def replace(match):
        original = match.group(1)
        if not has_chinese(original):
            return match.group(0)  # 沒中文，不動
        converted = CC.convert(original)
        if converted == original:
            return match.group(0)  # OpenCC 沒改（已經是繁中或不需轉）
        changes.append((original, converted))
        return f'"{converted}"'

    new_text = STRING_PATTERN.sub(replace, text)

    if changes:
        path.write_text(new_text, encoding="utf-8")
        print(f"\n=== {path.relative_to(ROOT)} ({len(changes)} changes) ===")
        for orig, conv in changes[:5]:  # 列前 5 個示範
            print(f"  '{orig}' -> '{conv}'")
        if len(changes) > 5:
            print(f"  ... 還有 {len(changes) - 5} 處")

    return len(changes)

def main():
    total_files = 0
    total_changes = 0
    for path in SRC.rglob("*"):
        if path.suffix not in {".cpp", ".h"}:
            continue
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8")
        if not has_chinese(text):
            continue
        n = convert_file(path)
        if n > 0:
            total_files += 1
            total_changes += n

    print(f"\n--- 總計 {total_files} 檔、{total_changes} 處轉換 ---")

if __name__ == "__main__":
    main()
