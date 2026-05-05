"""合併 UI 字符集 + 閱星曈優化字集 = 完整 UI 字符集（給書名顯示用）"""
from pathlib import Path

ROOT = Path(__file__).parent.parent
ui_set = set((ROOT / "ui_charset.txt").read_text(encoding="utf-8"))
yuanxia_path = Path(r"D:\RURU-ALL\Library\工具\閱星曈刷機\風眼-繁中版\charset_yuanxia.txt")
if yuanxia_path.exists():
    yuanxia_set = set(yuanxia_path.read_text(encoding="utf-8"))
    print(f"yuanxia: {len(yuanxia_set)} chars")
else:
    print("yuanxia not found, using only UI")
    yuanxia_set = set()

merged = ui_set | yuanxia_set
merged.discard("\n")
merged.discard("\r")
merged.discard(" ")
sorted_chars = sorted(c for c in merged if "一" <= c <= "鿿" or "　" <= c <= "〿")

output = ROOT / "ui_charset_merged.txt"
output.write_text("".join(sorted_chars), encoding="utf-8")
print(f"Merged: {len(sorted_chars)} chars -> {output}")
