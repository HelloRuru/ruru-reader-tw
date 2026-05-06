"""
掃描 src/ 所有 .cpp/.h 中含中文字符的 UI 字串字面量，
產出 unicode 字符集（用於 fontconvert.py 的 --additional-intervals 參數）。
"""
import re
from pathlib import Path

ROOT = Path(__file__).parent.parent
SRC = ROOT / "src"
CHINESE_RANGE = re.compile(r"[一-鿿]")
STRING_PATTERN = re.compile(r'"((?:[^"\\\n]|\\.)*)"')

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

print(f"Total unique Chinese chars: {len(charset)}")

# 輸出排序後的字符到檔案
sorted_chars = sorted(charset)
output_file = ROOT / "ui_charset.txt"
output_file.write_text("".join(sorted_chars), encoding="utf-8")
print(f"Written to: {output_file}")
print(f"First 50 chars: {sorted_chars[:50]}")
