#!python3
import freetype
import zlib
import sys
import re
import math
import struct
import argparse
from collections import namedtuple

# Originally from https://github.com/vroland/epdiy

parser = argparse.ArgumentParser(description="Generate a header file or .epdfont binary from a font to be used with epdiy.")
parser.add_argument("name", action="store", help="name of the font.")
parser.add_argument("size", type=int, help="font size to use.")
parser.add_argument("fontstack", action="store", nargs='+', help="list of font files, ordered by descending priority.")
parser.add_argument("--2bit", dest="is2Bit", action="store_true", help="generate 2-bit greyscale bitmap instead of 1-bit black and white.")
parser.add_argument("--additional-intervals", dest="additional_intervals", action="append", help="Additional code point intervals to export as min,max. This argument can be repeated.")
parser.add_argument("--charset-file", dest="charset_file", type=str, default=None, help="Path to a text file containing the exact characters to include (one line of chars, UTF-8). Overrides --charset.")
parser.add_argument("--binary", dest="binary", type=str, default=None, metavar="OUTPUT", help="Output .epdfont binary file instead of C header. Specify output file path.")
parser.add_argument("--format", dest="fmt", choices=["v0", "v1"], default="v0", help="Binary format version: v0 (13-byte glyphs, default) or v1 (16-byte glyphs).")
parser.add_argument("--charset", dest="charset", choices=["all", "gb2312", "gb2312-plus"], default="gb2312-plus", help="Character set to include: 'gb2312' (default, ~7700 chars: ASCII + GB2312 + CJK punctuation + fullwidth), 'gb2312-plus' (gb2312 + extra symbols: ellipsis/dash/curly-quotes/middle-dot/kana/etc), or 'all' (entire Unicode).")
parser.add_argument("--baseline-offset", dest="baseline_offset", type=int, default=0, metavar="N", help="Shift glyphs vertically within the line box. Positive = move DOWN (fix glyphs appearing too high), negative = move UP. Adjusts the stored ascender value by N pixels.")
args = parser.parse_args()

GlyphProps = namedtuple("GlyphProps", ["width", "height", "advance_x", "left", "top", "data_length", "data_offset", "code_point"])

font_stack = [freetype.Face(f) for f in args.fontstack]
is2Bit = args.is2Bit
size = args.size
font_name = args.name

def generate_gb2312_codepoints():
    """Generate all Unicode code points covered by GB2312 (6,763 CJK + symbols)."""
    codepoints = set()
    for high in range(0xA1, 0xF8):
        if high < 0xB0:
            # A1-A9: symbol area
            low_range = range(0xA1, 0xFF)
        else:
            # B0-F7: CJK area (level 1 + level 2)
            low_range = range(0xA1, 0xFF)
        for low in low_range:
            gb_bytes = bytes([high, low])
            try:
                char = gb_bytes.decode('gb2312')
                codepoints.add(ord(char))
            except (UnicodeDecodeError, ValueError):
                pass
    return sorted(codepoints)

def codepoints_to_intervals(codepoints):
    """Convert a sorted list of code points to inclusive (start, end) intervals."""
    if not codepoints:
        return []
    result = []
    start = codepoints[0]
    end = codepoints[0]
    for cp in codepoints[1:]:
        if cp == end + 1:
            end = cp
        else:
            result.append((start, end))
            start = cp
            end = cp
    result.append((start, end))
    return result

def merge_intervals(intervals, max_gap):
    """Merge adjacent intervals whose gap is <= max_gap.
    Returns (merged_intervals, gap_codepoints_set).
    gap_codepoints are positions inserted by merging that the font may not have."""
    if not intervals or max_gap <= 0:
        return list(intervals), set()
    merged = [list(intervals[0])]
    gap_cps = set()
    for start, end in intervals[1:]:
        gap = start - merged[-1][1] - 1
        if gap <= max_gap:
            for g in range(merged[-1][1] + 1, start):
                gap_cps.add(g)
            merged[-1][1] = end
        else:
            merged.append([start, end])
    return [tuple(i) for i in merged], gap_cps

# inclusive unicode code point intervals
# must not overlap and be in ascending order
if args.charset_file:
    # 讀取字元檔案，使用精確字集
    with open(args.charset_file, 'r', encoding='utf-8') as cf:
        chars_content = cf.read()
    cps_set = set(ord(c) for c in chars_content if c.isprintable() or c in '\t\n ')
    # 加上 ASCII
    cps_set.update(range(0x20, 0x7F))
    all_cps = sorted(cps_set)
    intervals = codepoints_to_intervals(all_cps)
    print(f"Charset from file: {args.charset_file} = {len(all_cps)} code points (pre-filter)", file=sys.stderr)
elif args.charset in ('gb2312', 'gb2312-plus'):
    # gb2312 mode: decode the exact GB2312 standard character set to get the
    # precise ~7,700 code points. These will be fragmented across Unicode, but
    # we fix that below with interval merging (gap<=20) after font validation.
    gb2312_cps = generate_gb2312_codepoints()
    ascii_cps = list(range(0x20, 0x7F))
    cjk_punct_cps = list(range(0x3000, 0x3040))
    common_punct_cps = list(range(0x2013, 0x2027))  # – — ' ' " " • …
    fullwidth_cps = list(range(0xFF00, 0xFFF0))
    all_cps = set(gb2312_cps + ascii_cps + cjk_punct_cps + common_punct_cps + fullwidth_cps)

    if args.charset == 'gb2312-plus':
        # Extra Unicode blocks commonly found in Chinese ebooks but absent from GB2312.
        # These are chosen as large contiguous blocks to avoid fragmentation.
        extra_ranges = [
            (0x00A0, 0x00FF),  # Latin-1 Supplement (·°©®±¼½¾ etc.)
            (0x0100, 0x017F),  # Latin Extended-A: accented letters (café, naïve, etc.)
            (0x02B0, 0x02FF),  # Spacing Modifier Letters
            (0x2000, 0x2012),  # General Punctuation (remaining dashes/spaces)
            (0x2027, 0x206F),  # General Punctuation (daggers, primes, etc.)
            (0x2070, 0x20CF),  # Superscripts, Subscripts, Currency Symbols
            (0x2100, 0x218F),  # Letterlike (℃℉™№), Number Forms (½Ⅰ Ⅱ)
            (0x2190, 0x27BF),  # Arrows, Math, Box Drawing, Shapes, Symbols, Dingbats
            (0x3040, 0x30FF),  # Hiragana + Katakana
            (0x3200, 0x33FF),  # Enclosed CJK, CJK Compatibility (①②㎡㎝)
            (0xFE30, 0xFE6F),  # CJK Compat Forms + Small Form Variants
            (0xFFFD, 0xFFFD),  # Replacement character
        ]
        for s, e in extra_ranges:
            all_cps.update(range(s, e + 1))
        label = 'GB2312-plus'
    else:
        label = 'GB2312'

    all_cps = sorted(all_cps)
    intervals = codepoints_to_intervals(all_cps)
    print(f"Charset: {label} = {len(all_cps)} code points (pre-filter)", file=sys.stderr)
else:
    intervals = [
        (0x0000, 0x10FFFF),
    ]

add_ints = []
if args.additional_intervals:
    add_ints = [tuple([int(n, base=0) for n in i.split(",")]) for i in args.additional_intervals]

def norm_floor(val):
    return int(math.floor(val / (1 << 6)))

def norm_ceil(val):
    return int(math.ceil(val / (1 << 6)))

def chunks(l, n):
    for i in range(0, len(l), n):
        yield l[i:i + n]

def load_glyph(code_point):
    face_index = 0
    while face_index < len(font_stack):
        face = font_stack[face_index]
        glyph_index = face.get_char_index(code_point)
        if glyph_index > 0:
            face.load_glyph(glyph_index, freetype.FT_LOAD_RENDER)
            return face
        face_index += 1
    print(f"code point {code_point} ({hex(code_point)}) not found in font stack!", file=sys.stderr)
    return None

unmerged_intervals = sorted(intervals + add_ints)
intervals = []
unvalidated_intervals = []
for i_start, i_end in unmerged_intervals:
    if len(unvalidated_intervals) > 0 and i_start + 1 <= unvalidated_intervals[-1][1]:
        unvalidated_intervals[-1] = (unvalidated_intervals[-1][0], max(unvalidated_intervals[-1][1], i_end))
        continue
    unvalidated_intervals.append((i_start, i_end))

for i_start, i_end in unvalidated_intervals:
    start = i_start
    for code_point in range(i_start, i_end + 1):
        face = load_glyph(code_point)
        if face is None:
            if start < code_point:
                intervals.append((start, code_point - 1))
            start = code_point + 1
    if start != i_end + 1:
        intervals.append((start, i_end))

# Merge nearby intervals to reduce interval count and RAM usage.
# For gb2312/gb2312-plus the CJK chars are scattered across Unicode,
# creating thousands of tiny intervals after font validation.
# Merging with gap<=20 reduces them to tens of intervals (saves ~40KB RAM)
# while only adding a handful of empty placeholder glyphs in the gaps.
_default_merge_gap = 20 if args.charset in ('gb2312', 'gb2312-plus') else 0
_merge_gap = _default_merge_gap
intervals, gap_codepoints = merge_intervals(intervals, _merge_gap)
print(f"Intervals after validation + merge(gap<={_merge_gap}): {len(intervals)}", file=sys.stderr)

for face in font_stack:
    face.set_char_size(size << 6, size << 6, 150, 150)

total_size = 0
all_glyphs = []

for i_start, i_end in intervals:
    for code_point in range(i_start, i_end + 1):
        if code_point in gap_codepoints:
            # Gap char inserted by interval merging - font doesn't have it.
            # Store as zero-size placeholder so the glyph index table stays valid.
            glyph = GlyphProps(0, 0, 0, 0, 0, 0, total_size, code_point)
            all_glyphs.append((glyph, b''))
            continue
        face = load_glyph(code_point)
        if face is None:
            # Shouldn't happen after validation, but guard defensively.
            glyph = GlyphProps(0, 0, 0, 0, 0, 0, total_size, code_point)
            all_glyphs.append((glyph, b''))
            continue
        bitmap = face.glyph.bitmap

        # Build out 4-bit greyscale bitmap
        pixels4g = []
        px = 0
        for i, v in enumerate(bitmap.buffer):
            y = i / bitmap.width
            x = i % bitmap.width
            if x % 2 == 0:
                px = (v >> 4)
            else:
                px = px | (v & 0xF0)
                pixels4g.append(px);
                px = 0
            # eol
            if x == bitmap.width - 1 and bitmap.width % 2 > 0:
                pixels4g.append(px)
                px = 0

        if is2Bit:
            # 0-3 white, 4-7 light grey, 8-11 dark grey, 12-15 black
            # Downsample to 2-bit bitmap
            pixels2b = []
            px = 0
            pitch = (bitmap.width // 2) + (bitmap.width % 2)
            for y in range(bitmap.rows):
                for x in range(bitmap.width):
                    px = px << 2
                    bm = pixels4g[y * pitch + (x // 2)]
                    bm = (bm >> ((x % 2) * 4)) & 0xF

                    if bm >= 12:
                        px += 3
                    elif bm >= 8:
                        px += 2
                    elif bm >= 4:
                        px += 1

                    if (y * bitmap.width + x) % 4 == 3:
                        pixels2b.append(px)
                        px = 0
            if (bitmap.width * bitmap.rows) % 4 != 0:
                px = px << (4 - (bitmap.width * bitmap.rows) % 4) * 2
                pixels2b.append(px)

            # for y in range(bitmap.rows):
            #     line = ''
            #     for x in range(bitmap.width):
            #         pixelPosition = y * bitmap.width + x
            #         byte = pixels2b[pixelPosition // 4]
            #         bit_index = (3 - (pixelPosition % 4)) * 2
            #         line += '#' if ((byte >> bit_index) & 3) > 0 else '.'
            #     print(line)
            # print('')
        else:
            # Downsample to 1-bit bitmap - treat any 2+ as black
            pixelsbw = []
            px = 0
            pitch = (bitmap.width // 2) + (bitmap.width % 2)
            for y in range(bitmap.rows):
                for x in range(bitmap.width):
                    px = px << 1
                    bm = pixels4g[y * pitch + (x // 2)]
                    px += 1 if ((x & 1) == 0 and bm & 0xE > 0) or ((x & 1) == 1 and bm & 0xE0 > 0) else 0

                    if (y * bitmap.width + x) % 8 == 7:
                        pixelsbw.append(px)
                        px = 0
            if (bitmap.width * bitmap.rows) % 8 != 0:
                px = px << (8 - (bitmap.width * bitmap.rows) % 8)
                pixelsbw.append(px)

            # for y in range(bitmap.rows):
            #     line = ''
            #     for x in range(bitmap.width):
            #         pixelPosition = y * bitmap.width + x
            #         byte = pixelsbw[pixelPosition // 8]
            #         bit_index = 7 - (pixelPosition % 8)
            #         line += '#' if (byte >> bit_index) & 1 else '.'
            #     print(line)
            # print('')

        pixels = pixels2b if is2Bit else pixelsbw

        # Build output data
        packed = bytes(pixels)
        glyph = GlyphProps(
            width = bitmap.width,
            height = bitmap.rows,
            advance_x = norm_floor(face.glyph.advance.x),
            left = face.glyph.bitmap_left,
            top = face.glyph.bitmap_top,
            data_length = len(packed),
            data_offset = total_size,
            code_point = code_point,
        )
        total_size += len(packed)
        all_glyphs.append((glyph, packed))

# pipe seems to be a good heuristic for the "real" descender
face = load_glyph(ord('|'))

glyph_data = []
glyph_props = []
for index, glyph in enumerate(all_glyphs):
    props, packed = glyph
    glyph_data.extend([b for b in packed])
    glyph_props.append(props)

# Compute font metrics
advanceY = norm_ceil(face.size.height)
# Use the larger of typographic ascender and the actual maximum bitmap_top
# across all rendered glyphs.  CJK fonts often have bitmap_top > face.size.ascender,
# which would make glyphs draw above the line box and appear shifted upward.
typographic_ascender = norm_ceil(face.size.ascender)
actual_max_top = max((g.top for g, _ in all_glyphs if g.top > 0), default=typographic_ascender)
ascender = max(typographic_ascender, actual_max_top)
if args.baseline_offset != 0:
    ascender += args.baseline_offset
    print(f"Baseline offset applied: {args.baseline_offset:+d}px, final ascender={ascender}", file=sys.stderr)
elif actual_max_top > typographic_ascender:
    print(f"Ascender auto-corrected: typographic={typographic_ascender} → actual_max_top={actual_max_top}, stored ascender={ascender}", file=sys.stderr)
descender = norm_floor(face.size.descender)
total_glyphs = sum(i_end - i_start + 1 for i_start, i_end in intervals)

if args.binary:
    # ============================================================
    # Output .epdfont binary file
    # ============================================================
    fmt = args.fmt

    # --- Build intervals data ---
    intervals_bin = bytearray()
    glyph_offset = 0
    for i_start, i_end in intervals:
        intervals_bin += struct.pack('<III', i_start, i_end, glyph_offset)
        glyph_offset += i_end - i_start + 1

    # --- Build glyphs data ---
    glyphs_bin = bytearray()
    for g in glyph_props:
        if fmt == 'v1':
            # V1: 16 bytes per glyph
            glyphs_bin += struct.pack('<BBBbhhII',
                g.width, g.height, g.advance_x, 0,
                g.left, g.top, g.data_length, g.data_offset)
        else:
            # V0: 13 bytes per glyph
            left_i8 = max(-128, min(127, g.left))
            top_i8 = max(-128, min(127, g.top))
            dlen_u16 = g.data_length & 0xFFFF
            glyphs_bin += struct.pack('<BBBbBbBHI',
                g.width, g.height, g.advance_x,
                left_i8, 0, top_i8, 0,
                dlen_u16, g.data_offset)

    # --- Build bitmaps data ---
    bitmaps_bin = bytes(glyph_data)

    # --- Calculate offsets ---
    if fmt == 'v1':
        HEADER_SIZE = 32
    else:
        HEADER_SIZE = 48  # V0: 12 x uint32

    offsetIntervals = HEADER_SIZE
    offsetGlyphs = offsetIntervals + len(intervals_bin)
    offsetBitmaps = offsetGlyphs + len(glyphs_bin)

    # --- Build header ---
    if fmt == 'v1':
        # V1 header: 32 bytes
        header = bytearray(32)
        header[0:4] = b'EPDF'
        header[4] = 1  # version
        header[5] = 0  # reserved
        header[6] = 1 if is2Bit else 0
        header[7] = 0  # reserved
        header[8] = advanceY & 0xFF
        header[9] = ascender & 0xFF if ascender >= 0 else (ascender + 256) & 0xFF
        header[10] = descender & 0xFF if descender >= 0 else (descender + 256) & 0xFF
        header[11] = 0  # reserved
        struct.pack_into('<I', header, 12, len(intervals))
        struct.pack_into('<I', header, 16, total_glyphs)
        struct.pack_into('<I', header, 20, offsetIntervals)
        struct.pack_into('<I', header, 24, offsetGlyphs)
        struct.pack_into('<I', header, 28, offsetBitmaps)
    else:
        # V0 header: 48 bytes (12 x uint32)
        header = bytearray(48)
        header[0:4] = b'EPDF'
        struct.pack_into('<I', header, 4, len(intervals))       # buf[1]: intervalCount
        struct.pack_into('<I', header, 8, total_glyphs)         # buf[2]: glyphCount
        struct.pack_into('<I', header, 12, advanceY)            # buf[3]: advanceY
        struct.pack_into('<I', header, 16, 0)                   # buf[4]: reserved
        struct.pack_into('<i', header, 20, ascender)            # buf[5]: ascender
        struct.pack_into('<I', header, 24, 0)                   # buf[6]: reserved
        struct.pack_into('<i', header, 28, descender)           # buf[7]: descender
        struct.pack_into('<I', header, 32, 1 if is2Bit else 0)  # buf[8]: is2Bit
        struct.pack_into('<I', header, 36, offsetIntervals)     # buf[9]: offsetIntervals
        struct.pack_into('<I', header, 40, offsetGlyphs)        # buf[10]: offsetGlyphs
        struct.pack_into('<I', header, 44, offsetBitmaps)       # buf[11]: offsetBitmaps

    # --- Write file ---
    output_path = args.binary
    with open(output_path, 'wb') as f:
        f.write(header)
        f.write(intervals_bin)
        f.write(glyphs_bin)
        f.write(bitmaps_bin)

    file_size = len(header) + len(intervals_bin) + len(glyphs_bin) + len(bitmaps_bin)
    print(f"Generated {fmt.upper()} .epdfont: {output_path}", file=sys.stderr)
    print(f"  Font: {font_name}, Size: {size}pt @ 150 DPI, Mode: {'2-bit' if is2Bit else '1-bit'}", file=sys.stderr)
    print(f"  Intervals: {len(intervals)}, Glyphs: {total_glyphs}, Bitmaps: {len(bitmaps_bin)} bytes", file=sys.stderr)
    print(f"  advanceY={advanceY}, ascender={ascender}, descender={descender}", file=sys.stderr)
    print(f"  Offsets: intervals={offsetIntervals}, glyphs={offsetGlyphs}, bitmaps={offsetBitmaps}", file=sys.stderr)
    print(f"  Total file size: {file_size} bytes ({file_size/1024/1024:.2f} MB)", file=sys.stderr)

else:
    # ============================================================
    # Output C header (original behavior)
    # ============================================================
    print(f"""/**
 * generated by fontconvert.py
 * name: {font_name}
 * size: {size}
 * mode: {'2-bit' if is2Bit else '1-bit'}
 * Command used: {' '.join(sys.argv)}
 */
#pragma once
#include "EpdFontData.h"
""")

    print(f"static const uint8_t {font_name}Bitmaps[{len(glyph_data)}] = {{")
    for c in chunks(glyph_data, 16):
        print ("    " + " ".join(f"0x{b:02X}," for b in c))
    print ("};");
    print()

    print(f"static const EpdGlyph {font_name}Glyphs[] = {{")
    for i, g in enumerate(glyph_props):
        print ("    { " + ", ".join([f"{a}" for a in list(g[:-1])]),"},", f"// {chr(g.code_point) if g.code_point != 92 else '<backslash>'}")
    print ("};");
    print()

    print(f"static const EpdUnicodeInterval {font_name}Intervals[] = {{")
    offset = 0
    for i_start, i_end in intervals:
        print (f"    {{ 0x{i_start:X}, 0x{i_end:X}, 0x{offset:X} }},")
        offset += i_end - i_start + 1
    print ("};");
    print()

    print(f"static const EpdFontData {font_name} = {{")
    print(f"    {font_name}Bitmaps,")
    print(f"    {font_name}Glyphs,")
    print(f"    {font_name}Intervals,")
    print(f"    {len(intervals)},")
    print(f"    {advanceY},")
    print(f"    {ascender},")
    print(f"    {descender},")
    print(f"    {'true' if is2Bit else 'false'},")
    print("};")
