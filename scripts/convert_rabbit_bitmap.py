#!/usr/bin/env python3
# stage15.35: 把 HelloRuru 兔子 Logo 轉成 128x128 1-bit monochrome bitmap
# 嚕寶要求「全黑底兔子」= 兔子變實心黑、背景白

from PIL import Image
import sys

SRC = "G:/我的雲端硬碟/【Ruru】重要素材/付費LOGO-兔子/blog-favicons/android-chrome-192x192.png"
DST = "D:/RURU-ALL/Library/工具/閱星曈刷機/Carousel-繁中版/src/images/RabbitLarge.h"
SIZE = 128

img = Image.open(SRC)
print(f"Source: {img.size} mode={img.mode}")

# 處理透明背景：把 RGBA 合成到白底
if img.mode == "RGBA":
    bg = Image.new("RGBA", img.size, (255, 255, 255, 255))
    img = Image.alpha_composite(bg, img).convert("RGB")
elif img.mode != "RGB":
    img = img.convert("RGB")

# stage15.38 (嚕寶要求順時針 90° + 放大):
#   先在大尺寸 (192x192) crop 中央放大區、再旋轉、最後縮成 128x128
#   放大方式：crop 中央 156x156（原 192x192）= 81% 區域、相當於兔子內容放大 ~23%
crop_size = int(img.width * 0.81)
left = (img.width - crop_size) // 2
top = (img.height - crop_size) // 2
img = img.crop((left, top, left + crop_size, top + crop_size))

# 縮成 128x128
img = img.resize((SIZE, SIZE), Image.LANCZOS)

# stage15.39 (嚕寶說 stage15.38 頭顛倒、再轉 180°):
#   原本 ROTATE_270 結果頭顛倒、改 ROTATE_90（差 180°）
img = img.transpose(Image.ROTATE_90)

# 轉灰階再二值化、threshold=128
img = img.convert("L")

# 嚕寶要「全黑底兔子」= 兔子實心黑
img = img.convert("1", dither=Image.NONE)

# 輸出 byte array、每 byte 8 bit、橫掃描
data = bytearray()
for y in range(SIZE):
    for x in range(0, SIZE, 8):
        byte = 0
        for bit in range(8):
            if x + bit < SIZE:
                # PIL '1' mode：0=黑、255=白
                pixel = img.getpixel((x + bit, y))
                # GfxRenderer 慣例 bit=1 表示白、bit=0 表示黑
                if pixel >= 128:
                    byte |= (1 << (7 - bit))
        data.append(byte)

# 寫成 C array
with open(DST, "w", encoding="utf-8") as f:
    f.write("#pragma once\n")
    f.write("#include <cstdint>\n\n")
    f.write("// stage15.35: HelloRuru 兔子 Logo、128x128 1-bit monochrome\n")
    f.write("// 來源：G:/我的雲端硬碟/【Ruru】重要素材/付費LOGO-兔子/blog-favicons/android-chrome-192x192.png\n")
    f.write("// 嚕寶 boot 畫面用、取代原 CrossLarge\n\n")
    f.write("static const uint8_t RabbitLarge[] = {\n")
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        line = "    " + ", ".join(f"0x{b:02X}" for b in chunk) + ","
        f.write(line + "\n")
    f.write("};\n")

print(f"Output: {DST}")
print(f"Bytes: {len(data)}")
