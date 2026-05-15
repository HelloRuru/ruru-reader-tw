#include "GfxRenderer.h"

#include <algorithm>
#include <cstdlib>

#include <Utf8.h>
#include <SDCardManager.h>

namespace {
struct VerticalGlyphMap {
  uint32_t horizontal;
  uint32_t vertical;
};

constexpr VerticalGlyphMap kVerticalGlyphMap[] = {
    {0x002C, 0xFE10},  // , -> ︐
    {0xFF0C, 0xFE10},  // ， -> ︐
    {0x3001, 0xFE11},  // 、 -> ︑
    {0x002E, 0xFE12},  // . -> ︒
    {0x3002, 0xFE12},  // 。 -> ︒
    {0x003A, 0xFE13},  // : -> ︓
    {0xFF1A, 0xFE13},  // ： -> ︓
    {0x003B, 0xFE14},  // ; -> ︔
    {0xFF1B, 0xFE14},  // ； -> ︔
    {0x0021, 0xFE15},  // ! -> ︕
    {0xFF01, 0xFE15},  // ！ -> ︕
    {0x003F, 0xFE16},  // ? -> ︖
    {0xFF1F, 0xFE16},  // ？ -> ︖
    {0x201C, 0xFE41},  // “ -> ﹁
    {0x2018, 0xFE41},  // ‘ -> ﹁
    {0x300C, 0xFE41},  // 「 -> ﹁
    {0x201D, 0xFE42},  // ” -> ﹂
    {0x2019, 0xFE42},  // ’ -> ﹂
    {0x300D, 0xFE42},  // 」 -> ﹂
    {0x300E, 0xFE43},  // 『 -> ﹃
    {0x300F, 0xFE44},  // 』 -> ﹄
    {0x0028, 0xFE35},  // ( -> ︵
    {0xFF08, 0xFE35},  // （ -> ︵
    {0x0029, 0xFE36},  // ) -> ︶
    {0xFF09, 0xFE36},  // ） -> ︶
    {0x007B, 0xFE37},  // { -> ︷
    {0xFF5B, 0xFE37},  // ｛ -> ︷
    {0x007D, 0xFE38},  // } -> ︸
    {0xFF5D, 0xFE38},  // ｝ -> ︸
    {0x3014, 0xFE39},  // 〔 -> ︹
    {0x3015, 0xFE3A},  // 〕 -> ︺
    {0x3010, 0xFE3B},  // 【 -> ︻
    {0x3011, 0xFE3C},  // 】 -> ︼
    {0x300A, 0xFE3D},  // 《 -> ︽
    {0x300B, 0xFE3E},  // 》 -> ︾
    {0x3008, 0xFE3F},  // 〈 -> ︿
    {0x3009, 0xFE40},  // 〉 -> ﹀
    {0x2026, 0xFE19},  // … -> ︙
    {0x2014, 0xFE31},  // — -> ︱
    {0x007C, 0xFE33},  // | -> ︳
    {0xFF5C, 0xFE33},  // ｜ -> ︳
    {0x2016, 0xFE34},  // ‖ -> ︴
    {0x005B, 0xFE47},  // [ -> ﹇
    {0xFF3B, 0xFE47},  // ［ -> ﹇
    {0x005D, 0xFE48},  // ] -> ﹈
    {0xFF3D, 0xFE48},  // ］ -> ﹈
};

uint32_t findVerticalCodepoint(const uint32_t cp) {
  for (const auto& entry : kVerticalGlyphMap) {
    if (entry.horizontal == cp) {
      return entry.vertical;
    }
  }
  return 0;
}

uint32_t findHorizontalCodepointForVertical(const uint32_t cp) {
  for (const auto& entry : kVerticalGlyphMap) {
    if (entry.vertical == cp) {
      return entry.horizontal;
    }
  }
  return 0;
}

uint32_t fallbackVerticalSourceCodepoint(const uint32_t cp) {
  if (findVerticalCodepoint(cp) != 0) {
    return cp;
  }
  return findHorizontalCodepointForVertical(cp);
}

uint32_t mapVerticalCodepointIfAvailable(const EpdFontFamily& font, const uint32_t cp,
                                         const EpdFontFamily::Style style) {
  const uint32_t verticalCp = findVerticalCodepoint(cp);
  if (verticalCp != 0 && font.getGlyph(verticalCp, style)) {
    return verticalCp;
  }
  return cp;
}

bool shouldCenterFallbackVerticalPunctuation(const uint32_t cp) {
  switch (cp) {
    case 0x0021:  // !
    case 0x002C:  // ,
    case 0x002E:  // .
    case 0x003A:  // :
    case 0x003B:  // ;
    case 0x003F:  // ?
    case 0x3001:  // 、
    case 0x3002:  // 。
    case 0xFF01:  // ！
    case 0xFF0C:  // ，
    case 0xFF1A:  // ：
    case 0xFF1B:  // ；
    case 0xFF1F:  // ？
      return true;
    default:
      return false;
  }
}

const EpdGlyph* getRenderableGlyph(const EpdFontFamily& font, const uint32_t cp, const EpdFontFamily::Style style) {
  const EpdGlyph* glyph = font.getGlyph(cp, style);
  if (!glyph) {
    glyph = font.getGlyph(REPLACEMENT_GLYPH, style);
  }
  return glyph;
}

void drawThickLine(const GfxRenderer& renderer, const int x1, const int y1, const int x2, const int y2,
                   const bool black) {
  if (x1 == x2) {
    const int top = std::min(y1, y2);
    const int height = std::abs(y2 - y1) + 1;
    renderer.fillRect(x1 - 1, top, 2, height, black);
    return;
  }
  if (y1 == y2) {
    const int left = std::min(x1, x2);
    const int width = std::abs(x2 - x1) + 1;
    renderer.fillRect(left, y1 - 1, width, 2, black);
    return;
  }
  renderer.drawLine(x1, y1, x2, y2, 2, black);
}

void drawDiagonalLine(const GfxRenderer& renderer, int x0, int y0, const int x1, const int y1, const bool black) {
  const int dx = std::abs(x1 - x0);
  const int sx = x0 < x1 ? 1 : -1;
  const int dy = -std::abs(y1 - y0);
  const int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  while (true) {
    renderer.fillRect(x0, y0, 2, 2, black);
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int e2 = 2 * err;
    if (e2 >= dy) {
      err += dy;
      x0 += sx;
    }
    if (e2 <= dx) {
      err += dx;
      y0 += sy;
    }
  }
}

bool drawFallbackVerticalForm(const GfxRenderer& renderer, const uint32_t cp, const int x, const int y,
                              const int cellWidth, const int lineHeight, const bool black) {
  if (cellWidth <= 0 || lineHeight <= 0) {
    return false;
  }

  const int markSize = std::max(6, std::min(cellWidth, lineHeight) * 2 / 3);
  const int x0 = x + (cellWidth - markSize) / 2;
  const int x1 = x0 + markSize - 1;
  const int y0 = y + std::max(1, (lineHeight - markSize) / 2);
  const int y1 = y0 + markSize - 1;
  const int cx = x + cellWidth / 2;
  const int cy = y + lineHeight / 2;

  switch (cp) {
    case 0x2018:  // ‘ -> ﹁
    case 0x201C:  // “ -> ﹁
    case 0x300C:  // 「 -> ﹁
      drawThickLine(renderer, x0, y0, x1, y0, black);
      drawThickLine(renderer, x1, y0, x1, y1, black);
      return true;
    case 0x2019:  // ’ -> ﹂
    case 0x201D:  // ” -> ﹂
    case 0x300D:  // 」 -> ﹂
      drawThickLine(renderer, x0, y1, x1, y1, black);
      drawThickLine(renderer, x0, y0, x0, y1, black);
      return true;
    case 0x300E:  // 『 -> ﹃
      drawThickLine(renderer, x0, y0, x1, y0, black);
      drawThickLine(renderer, x1, y0, x1, y1, black);
      drawThickLine(renderer, x0 + 3, y0 + 3, x1 - 3, y0 + 3, black);
      drawThickLine(renderer, x1 - 3, y0 + 3, x1 - 3, y1 - 3, black);
      return true;
    case 0x300F:  // 』 -> ﹄
      drawThickLine(renderer, x0, y1, x1, y1, black);
      drawThickLine(renderer, x0, y0, x0, y1, black);
      drawThickLine(renderer, x0 + 3, y1 - 3, x1 - 3, y1 - 3, black);
      drawThickLine(renderer, x0 + 3, y0 + 3, x0 + 3, y1 - 3, black);
      return true;
    case 0x0028:  // ( -> ︵
    case 0xFF08:  // （ -> ︵
      drawThickLine(renderer, x0 + 2, y0, x1 - 2, y0, black);
      drawThickLine(renderer, x0, y0 + 2, x0, y0 + markSize / 2, black);
      drawThickLine(renderer, x1, y0 + 2, x1, y0 + markSize / 2, black);
      return true;
    case 0x0029:  // ) -> ︶
    case 0xFF09:  // ） -> ︶
      drawThickLine(renderer, x0 + 2, y1, x1 - 2, y1, black);
      drawThickLine(renderer, x0, y1 - markSize / 2, x0, y1 - 2, black);
      drawThickLine(renderer, x1, y1 - markSize / 2, x1, y1 - 2, black);
      return true;
    case 0x007B:  // { -> ︷
    case 0xFF5B:  // ｛ -> ︷
    case 0x3014:  // 〔 -> ︹
    case 0x3010:  // 【 -> ︻
    case 0x005B:  // [ -> ﹇
    case 0xFF3B:  // ［ -> ﹇
      drawThickLine(renderer, x0, y0, x1, y0, black);
      drawThickLine(renderer, x0, y0, x0, y1, black);
      drawThickLine(renderer, x1, y0, x1, y1, black);
      return true;
    case 0x007D:  // } -> ︸
    case 0xFF5D:  // ｝ -> ︸
    case 0x3015:  // 〕 -> ︺
    case 0x3011:  // 】 -> ︼
    case 0x005D:  // ] -> ﹈
    case 0xFF3D:  // ］ -> ﹈
      drawThickLine(renderer, x0, y1, x1, y1, black);
      drawThickLine(renderer, x0, y0, x0, y1, black);
      drawThickLine(renderer, x1, y0, x1, y1, black);
      return true;
    case 0x3008:  // 〈 -> ︿
      drawDiagonalLine(renderer, x0, y1, cx, y0, black);
      drawDiagonalLine(renderer, cx, y0, x1, y1, black);
      return true;
    case 0x3009:  // 〉 -> ﹀
      drawDiagonalLine(renderer, x0, y0, cx, y1, black);
      drawDiagonalLine(renderer, cx, y1, x1, y0, black);
      return true;
    case 0x300A:  // 《 -> ︽
      drawDiagonalLine(renderer, x0, y1, cx - 2, y0, black);
      drawDiagonalLine(renderer, cx - 2, y0, x1 - 4, y1, black);
      drawDiagonalLine(renderer, x0 + 4, y1, cx + 2, y0, black);
      drawDiagonalLine(renderer, cx + 2, y0, x1, y1, black);
      return true;
    case 0x300B:  // 》 -> ︾
      drawDiagonalLine(renderer, x0, y0, cx - 2, y1, black);
      drawDiagonalLine(renderer, cx - 2, y1, x1 - 4, y0, black);
      drawDiagonalLine(renderer, x0 + 4, y0, cx + 2, y1, black);
      drawDiagonalLine(renderer, cx + 2, y1, x1, y0, black);
      return true;
    case 0x2026:  // … -> ︙
      renderer.fillRect(cx - 1, cy - markSize / 3 - 1, 3, 3, black);
      renderer.fillRect(cx - 1, cy - 1, 3, 3, black);
      renderer.fillRect(cx - 1, cy + markSize / 3 - 1, 3, 3, black);
      return true;
    case 0x2014:  // — -> ︱
      drawThickLine(renderer, cx, y0, cx, y1, black);
      return true;
    case 0x007C:  // | -> ︳
    case 0xFF5C:  // ｜ -> ︳
      drawThickLine(renderer, cx, y0, cx, y1, black);
      return true;
    case 0x2016:  // ‖ -> ︴
      drawThickLine(renderer, cx - 3, y0, cx - 3, y1, black);
      drawThickLine(renderer, cx + 3, y0, cx + 3, y1, black);
      return true;
    default:
      return false;
  }
}
}  // namespace

void GfxRenderer::begin() {
  frameBuffer = display.getFrameBuffer();
  if (!frameBuffer) {
    Serial.printf("[%lu] [GFX] !! No framebuffer\n", millis());
    assert(false);
  }
}

void GfxRenderer::insertFont(const int fontId, EpdFontFamily font) { fontMap.insert({fontId, font}); }

void GfxRenderer::clearCustomFonts(const int startId) {
  for (auto it = fontMap.lower_bound(startId); it != fontMap.end();) {
    it = fontMap.erase(it);
  }
}
int GfxRenderer::getTextAdvance(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

  return fontMap.at(fontId).getTextAdvance(text, style);
}
// Translate logical (x,y) coordinates to physical panel coordinates based on current orientation
// This should always be inlined for better performance
static inline void rotateCoordinates(const GfxRenderer::Orientation orientation, const int x, const int y, int* phyX,
                                     int* phyY) {
  switch (orientation) {
    case GfxRenderer::Portrait: {
      // Logical portrait (480x800) → panel (800x480)
      // Rotation: 90 degrees clockwise
      *phyX = y;
      *phyY = HalDisplay::DISPLAY_HEIGHT - 1 - x;
      break;
    }
    case GfxRenderer::LandscapeClockwise: {
      // Logical landscape (800x480) rotated 180 degrees (swap top/bottom and left/right)
      *phyX = HalDisplay::DISPLAY_WIDTH - 1 - x;
      *phyY = HalDisplay::DISPLAY_HEIGHT - 1 - y;
      break;
    }
    case GfxRenderer::PortraitInverted: {
      // Logical portrait (480x800) → panel (800x480)
      // Rotation: 90 degrees counter-clockwise
      *phyX = HalDisplay::DISPLAY_WIDTH - 1 - y;
      *phyY = x;
      break;
    }
    case GfxRenderer::LandscapeCounterClockwise: {
      // Logical landscape (800x480) aligned with panel orientation
      *phyX = x;
      *phyY = y;
      break;
    }
  }
}

// IMPORTANT: This function is in critical rendering path and is called for every pixel. Please keep it as simple and
// efficient as possible.
void GfxRenderer::drawPixel(const int x, const int y, const bool state) const {
  int phyX = 0;
  int phyY = 0;

  // Note: this call should be inlined for better performance
  rotateCoordinates(orientation, x, y, &phyX, &phyY);

  // Bounds checking against physical panel dimensions
  if (phyX < 0 || phyX >= HalDisplay::DISPLAY_WIDTH || phyY < 0 || phyY >= HalDisplay::DISPLAY_HEIGHT) {
    Serial.printf("[%lu] [GFX] !! Outside range (%d, %d) -> (%d, %d)\n", millis(), x, y, phyX, phyY);
    return;
  }

  // Calculate byte position and bit position
  const uint16_t byteIndex = phyY * HalDisplay::DISPLAY_WIDTH_BYTES + (phyX / 8);
  const uint8_t bitPosition = 7 - (phyX % 8);  // MSB first

  if (state) {
    frameBuffer[byteIndex] &= ~(1 << bitPosition);  // Clear bit
  } else {
    frameBuffer[byteIndex] |= 1 << bitPosition;  // Set bit
  }
}

int GfxRenderer::getTextWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

  int w = 0, h = 0;
  fontMap.at(fontId).getTextDimensions(text, &w, &h, style);
  return w;
}

void GfxRenderer::drawCenteredText(const int fontId, const int y, const char* text, const bool black,
                                   const EpdFontFamily::Style style) const {
  const int x = (getScreenWidth() - getTextWidth(fontId, text, style)) / 2;
  drawText(fontId, x, y, text, black, style);
}

void GfxRenderer::drawText(const int fontId, const int x, const int y, const char* text, const bool black,
                           const EpdFontFamily::Style style) const {
  const int yPos = y + getFontAscenderSize(fontId);
  int xpos = x;

  // cannot draw a NULL / empty string
  if (text == nullptr || *text == '\0') {
    return;
  }

  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return;
  }
  const auto font = fontMap.at(fontId);

  // no printable characters
  if (!font.hasPrintableChars(text, style)) {
    return;
  }

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    renderChar(font, cp, &xpos, &yPos, black, style);
  }
}

bool GfxRenderer::getTextPixelBoundsY(const int fontId, const char* text, const int y, int* top, int* bottom,
                                      const EpdFontFamily::Style style) const {
  if (!top || !bottom || text == nullptr || *text == '\0') {
    return false;
  }
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return false;
  }

  const auto font = fontMap.at(fontId);
  const int baseline = y + getFontAscenderSize(fontId);
  int minY = 32767;
  int maxY = -32768;
  bool found = false;

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    const EpdGlyph* glyph = font.getGlyph(cp, style);
    if (!glyph) {
      glyph = font.getGlyph(REPLACEMENT_GLYPH, style);
    }
    if (!glyph || glyph->height == 0) {
      continue;
    }
    const int glyphTop = baseline - glyph->top;
    const int glyphBottom = glyphTop + glyph->height - 1;
    minY = std::min(minY, glyphTop);
    maxY = std::max(maxY, glyphBottom);
    found = true;
  }

  if (!found) {
    return false;
  }
  *top = minY;
  *bottom = maxY;
  return true;
}

int GfxRenderer::getVerticalTextCellHeight(const int fontId) const {
  static constexpr const char* samples[] = {"我", "國", "字", "高", "香", "菜", "書", "測", "一", "二", "三"};
  int minY = 32767;
  int maxY = -32768;
  bool found = false;

  for (const char* sample : samples) {
    int top = 0;
    int bottom = 0;
    if (getTextPixelBoundsY(fontId, sample, 0, &top, &bottom)) {
      minY = std::min(minY, top);
      maxY = std::max(maxY, bottom);
      found = true;
    }
  }

  if (!found) {
    return getLineHeight(fontId);
  }
  return std::max(1, maxY - minY + 1);
}

int GfxRenderer::getVerticalTextTopInset(const int fontId) const {
  static constexpr const char* samples[] = {"我", "國", "字", "高", "香", "菜", "書", "測", "一", "二", "三"};
  int minY = 32767;
  bool found = false;

  for (const char* sample : samples) {
    int top = 0;
    int bottom = 0;
    if (getTextPixelBoundsY(fontId, sample, 0, &top, &bottom)) {
      minY = std::min(minY, top);
      found = true;
    }
  }

  return found ? std::max(0, minY) : 0;
}

int GfxRenderer::getVerticalTextWidth(const int fontId, const char* text, const EpdFontFamily::Style style) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }
  if (text == nullptr || *text == '\0') {
    return 0;
  }

  const auto font = fontMap.at(fontId);
  int width = 0;
  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    const uint32_t mappedCp = mapVerticalCodepointIfAvailable(font, cp, style);
    const EpdGlyph* glyph = font.getGlyph(mappedCp, style);
    if (!glyph && fallbackVerticalSourceCodepoint(cp) != 0) {
      width += getLineHeight(fontId);
      continue;
    }
    glyph = getRenderableGlyph(font, mappedCp, style);
    if (!glyph) {
      continue;
    }
    width += glyph->advanceX;
  }
  return width;
}

void GfxRenderer::drawVerticalText(const int fontId, const int x, const int y, const char* text, const bool black,
                                   const EpdFontFamily::Style style) const {
  // stage15.8 修 BUG：SAM 原版只逐字畫但用 xpos += advanceX（往 X 推進、實際是橫排）
  //                  真正直排：每個字畫完往 Y 推進、X 留在欄位上
  //                  欄寬 = lineHeight（直排「行寬」= 字級），每字佔一個 charBoxH 高
  if (text == nullptr || *text == '\0') {
    return;
  }
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return;
  }
  const auto font = fontMap.at(fontId);

  const int lineHeight = getLineHeight(fontId);  // 字級高度（= 直排每字佔的格子高）
  const int colWidth = lineHeight;                // 直排欄寬 = 字格寬（粗略以方塊字為主）
  const int ascender = getFontAscenderSize(fontId);

  int cellTop = y;  // 目前要畫的字格頂部 Y
  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    const uint32_t mappedCp = mapVerticalCodepointIfAvailable(font, cp, style);
    const uint32_t fallbackCp = fallbackVerticalSourceCodepoint(cp);
    const EpdGlyph* glyph = font.getGlyph(mappedCp, style);

    // 直排標點 fallback：字型缺直排碼點、原始標點，或 EPUB 已給 FE** 直排碼點時都能畫。
    if (!glyph && fallbackCp != 0 &&
        drawFallbackVerticalForm(*this, fallbackCp, x, cellTop, colWidth, lineHeight, black)) {
      cellTop += lineHeight;
      continue;
    }
    glyph = getRenderableGlyph(font, mappedCp, style);

    // 一般字：在欄寬內水平置中、垂直靠 ascender 對齊
    if (glyph) {
      const int glyphCellX = x + std::max(0, (colWidth - static_cast<int>(glyph->advanceX)) / 2);
      // 標點符號：直排格子內再垂直置中（句號逗號這種小符號才不會貼在上邊緣）
      int glyphCellTop;
      if (shouldCenterFallbackVerticalPunctuation(cp) && mappedCp == cp) {
        glyphCellTop = cellTop + std::max(0, (lineHeight - static_cast<int>(glyph->height)) / 2);
      } else {
        glyphCellTop = cellTop;
      }
      int gx = glyphCellX;
      int gy = glyphCellTop + ascender;  // renderChar 用 baseline、所以加 ascender
      renderChar(font, mappedCp, &gx, &gy, black, style);
    }
    cellTop += lineHeight;
  }
}

void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const bool state) const {
  if (x1 == x2) {
    if (y2 < y1) {
      std::swap(y1, y2);
    }
    for (int y = y1; y <= y2; y++) {
      drawPixel(x1, y, state);
    }
  } else if (y1 == y2) {
    if (x2 < x1) {
      std::swap(x1, x2);
    }
    for (int x = x1; x <= x2; x++) {
      drawPixel(x, y1, state);
    }
  } else {
    // TODO: Implement
    Serial.printf("[%lu] [GFX] Line drawing not supported\n", millis());
  }
}

void GfxRenderer::drawLine(int x1, int y1, int x2, int y2, const int lineWidth, const bool state) const {
  for (int i = 0; i < lineWidth; i++) {
    drawLine(x1, y1 + i, x2, y2 + i, state);
  }
}

void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const bool state) const {
  drawLine(x, y, x + width - 1, y, state);
  drawLine(x + width - 1, y, x + width - 1, y + height - 1, state);
  drawLine(x + width - 1, y + height - 1, x, y + height - 1, state);
  drawLine(x, y, x, y + height - 1, state);
}

// Border is inside the rectangle
void GfxRenderer::drawRect(const int x, const int y, const int width, const int height, const int lineWidth,
                           const bool state) const {
  for (int i = 0; i < lineWidth; i++) {
    drawLine(x + i, y + i, x + width - i, y + i, state);
    drawLine(x + width - i, y + i, x + width - i, y + height - i, state);
    drawLine(x + width - i, y + height - i, x + i, y + height - i, state);
    drawLine(x + i, y + height - i, x + i, y + i, state);
  }
}

void GfxRenderer::drawArc(const int maxRadius, const int cx, const int cy, const int xDir, const int yDir,
                          const int lineWidth, const bool state) const {
  const int stroke = std::min(lineWidth, maxRadius);
  const int innerRadius = std::max(maxRadius - stroke, 0);
  const int outerRadiusSq = maxRadius * maxRadius;
  const int innerRadiusSq = innerRadius * innerRadius;
  for (int dy = 0; dy <= maxRadius; ++dy) {
    for (int dx = 0; dx <= maxRadius; ++dx) {
      const int distSq = dx * dx + dy * dy;
      if (distSq > outerRadiusSq || distSq < innerRadiusSq) {
        continue;
      }
      const int px = cx + xDir * dx;
      const int py = cy + yDir * dy;
      drawPixel(px, py, state);
    }
  }
};

// Border is inside the rectangle, rounded corners
void GfxRenderer::drawRoundedRect(const int x, const int y, const int width, const int height, const int lineWidth,
                                  const int cornerRadius, bool state) const {
  drawRoundedRect(x, y, width, height, lineWidth, cornerRadius, true, true, true, true, state);
}

// Border is inside the rectangle, rounded corners
void GfxRenderer::drawRoundedRect(const int x, const int y, const int width, const int height, const int lineWidth,
                                  const int cornerRadius, bool roundTopLeft, bool roundTopRight, bool roundBottomLeft,
                                  bool roundBottomRight, bool state) const {
  if (lineWidth <= 0 || width <= 0 || height <= 0) {
    return;
  }

  const int maxRadius = std::min({cornerRadius, width / 2, height / 2});
  if (maxRadius <= 0) {
    drawRect(x, y, width, height, lineWidth, state);
    return;
  }

  const int stroke = std::min(lineWidth, maxRadius);
  const int right = x + width - 1;
  const int bottom = y + height - 1;

  const int horizontalWidth = width - 2 * maxRadius;
  if (horizontalWidth > 0) {
    if (roundTopLeft || roundTopRight) {
      fillRect(x + maxRadius, y, horizontalWidth, stroke, state);
    }
    if (roundBottomLeft || roundBottomRight) {
      fillRect(x + maxRadius, bottom - stroke + 1, horizontalWidth, stroke, state);
    }
  }

  const int verticalHeight = height - 2 * maxRadius;
  if (verticalHeight > 0) {
    if (roundTopLeft || roundBottomLeft) {
      fillRect(x, y + maxRadius, stroke, verticalHeight, state);
    }
    if (roundTopRight || roundBottomRight) {
      fillRect(right - stroke + 1, y + maxRadius, stroke, verticalHeight, state);
    }
  }

  if (roundTopLeft) {
    drawArc(maxRadius, x + maxRadius, y + maxRadius, -1, -1, lineWidth, state);
  }
  if (roundTopRight) {
    drawArc(maxRadius, right - maxRadius, y + maxRadius, 1, -1, lineWidth, state);
  }
  if (roundBottomRight) {
    drawArc(maxRadius, right - maxRadius, bottom - maxRadius, 1, 1, lineWidth, state);
  }
  if (roundBottomLeft) {
    drawArc(maxRadius, x + maxRadius, bottom - maxRadius, -1, 1, lineWidth, state);
  }
}

void GfxRenderer::fillRect(const int x, const int y, const int width, const int height, const bool state) const {
  for (int fillY = y; fillY < y + height; fillY++) {
    drawLine(x, fillY, x + width - 1, fillY, state);
  }
}

// NOTE: Those are in critical path, and need to be templated to avoid runtime checks for every pixel.
// Any branching must be done outside the loops to avoid performance degradation.
template <>
void GfxRenderer::drawPixelDither<Color::Clear>(const int x, const int y) const {
  // Do nothing
}

template <>
void GfxRenderer::drawPixelDither<Color::Black>(const int x, const int y) const {
  drawPixel(x, y, true);
}

template <>
void GfxRenderer::drawPixelDither<Color::White>(const int x, const int y) const {
  drawPixel(x, y, false);
}

template <>
void GfxRenderer::drawPixelDither<Color::LightGray>(const int x, const int y) const {
  drawPixel(x, y, x % 2 == 0 && y % 2 == 0);
}

template <>
void GfxRenderer::drawPixelDither<Color::DarkGray>(const int x, const int y) const {
  drawPixel(x, y, (x + y) % 2 == 0);  // TODO: maybe find a better pattern?
}

void GfxRenderer::fillRectDither(const int x, const int y, const int width, const int height, Color color) const {
  if (color == Color::Clear) {
  } else if (color == Color::Black) {
    fillRect(x, y, width, height, true);
  } else if (color == Color::White) {
    fillRect(x, y, width, height, false);
  } else if (color == Color::LightGray) {
    for (int fillY = y; fillY < y + height; fillY++) {
      for (int fillX = x; fillX < x + width; fillX++) {
        drawPixelDither<Color::LightGray>(fillX, fillY);
      }
    }
  } else if (color == Color::DarkGray) {
    for (int fillY = y; fillY < y + height; fillY++) {
      for (int fillX = x; fillX < x + width; fillX++) {
        drawPixelDither<Color::DarkGray>(fillX, fillY);
      }
    }
  }
}

template <Color color>
void GfxRenderer::fillArc(const int maxRadius, const int cx, const int cy, const int xDir, const int yDir) const {
  const int radiusSq = maxRadius * maxRadius;
  for (int dy = 0; dy <= maxRadius; ++dy) {
    for (int dx = 0; dx <= maxRadius; ++dx) {
      const int distSq = dx * dx + dy * dy;
      const int px = cx + xDir * dx;
      const int py = cy + yDir * dy;
      if (distSq <= radiusSq) {
        drawPixelDither<color>(px, py);
      }
    }
  }
}

void GfxRenderer::fillRoundedRect(const int x, const int y, const int width, const int height, const int cornerRadius,
                                  const Color color) const {
  fillRoundedRect(x, y, width, height, cornerRadius, true, true, true, true, color);
}

void GfxRenderer::fillRoundedRect(const int x, const int y, const int width, const int height, const int cornerRadius,
                                  bool roundTopLeft, bool roundTopRight, bool roundBottomLeft, bool roundBottomRight,
                                  const Color color) const {
  if (width <= 0 || height <= 0) {
    return;
  }

  const int maxRadius = std::min({cornerRadius, width / 2, height / 2});
  if (maxRadius <= 0) {
    fillRectDither(x, y, width, height, color);
    return;
  }

  const int horizontalWidth = width - 2 * maxRadius;
  if (horizontalWidth > 0) {
    fillRectDither(x + maxRadius + 1, y, horizontalWidth - 2, height, color);
  }

  const int verticalHeight = height - 2 * maxRadius - 2;
  if (verticalHeight > 0) {
    fillRectDither(x, y + maxRadius + 1, maxRadius + 1, verticalHeight, color);
    fillRectDither(x + width - maxRadius - 1, y + maxRadius + 1, maxRadius + 1, verticalHeight, color);
  }

  auto fillArcTemplated = [this](int maxRadius, int cx, int cy, int xDir, int yDir, Color color) {
    switch (color) {
      case Color::Clear:
        break;
      case Color::Black:
        fillArc<Color::Black>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::White:
        fillArc<Color::White>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::LightGray:
        fillArc<Color::LightGray>(maxRadius, cx, cy, xDir, yDir);
        break;
      case Color::DarkGray:
        fillArc<Color::DarkGray>(maxRadius, cx, cy, xDir, yDir);
        break;
    }
  };

  if (roundTopLeft) {
    fillArcTemplated(maxRadius, x + maxRadius, y + maxRadius, -1, -1, color);
  } else {
    fillRectDither(x, y, maxRadius + 1, maxRadius + 1, color);
  }

  if (roundTopRight) {
    fillArcTemplated(maxRadius, x + width - maxRadius - 1, y + maxRadius, 1, -1, color);
  } else {
    fillRectDither(x + width - maxRadius - 1, y, maxRadius + 1, maxRadius + 1, color);
  }

  if (roundBottomRight) {
    fillArcTemplated(maxRadius, x + width - maxRadius - 1, y + height - maxRadius - 1, 1, 1, color);
  } else {
    fillRectDither(x + width - maxRadius - 1, y + height - maxRadius - 1, maxRadius + 1, maxRadius + 1, color);
  }

  if (roundBottomLeft) {
    fillArcTemplated(maxRadius, x + maxRadius, y + height - maxRadius - 1, -1, 1, color);
  } else {
    fillRectDither(x, y + height - maxRadius - 1, maxRadius + 1, maxRadius + 1, color);
  }
}

void GfxRenderer::drawImage(const uint8_t bitmap[], const int x, const int y, const int width, const int height) const {
  int rotatedX = 0;
  int rotatedY = 0;
  rotateCoordinates(orientation, x, y, &rotatedX, &rotatedY);
  // Rotate origin corner
  switch (orientation) {
    case Portrait:
      rotatedY = rotatedY - height;
      break;
    case PortraitInverted:
      rotatedX = rotatedX - width;
      break;
    case LandscapeClockwise:
      rotatedY = rotatedY - height;
      rotatedX = rotatedX - width;
      break;
    case LandscapeCounterClockwise:
      break;
  }
  // TODO: Rotate bits
  display.drawImage(bitmap, rotatedX, rotatedY, width, height);
}

void GfxRenderer::drawIcon(const uint8_t bitmap[], const int x, const int y, const int width, const int height) const {
  display.drawImage(bitmap, y, getScreenWidth() - width - x, height, width);
}

void GfxRenderer::drawBitmap(const Bitmap& bitmap, const int x, const int y, const int maxWidth, const int maxHeight,
                             const float cropX, const float cropY) const {
  // For 1-bit bitmaps, use optimized 1-bit rendering path (no crop support for 1-bit)
  if (bitmap.is1Bit() && cropX == 0.0f && cropY == 0.0f) {
    drawBitmap1Bit(bitmap, x, y, maxWidth, maxHeight);
    return;
  }

  float scale = 1.0f;
  bool isScaled = false;
  int cropPixX = std::floor(bitmap.getWidth() * cropX / 2.0f);
  int cropPixY = std::floor(bitmap.getHeight() * cropY / 2.0f);
  Serial.printf("[%lu] [GFX] Cropping %dx%d by %dx%d pix, is %s\n", millis(), bitmap.getWidth(), bitmap.getHeight(),
                cropPixX, cropPixY, bitmap.isTopDown() ? "top-down" : "bottom-up");

  if (maxWidth > 0 && (1.0f - cropX) * bitmap.getWidth() > maxWidth) {
    scale = static_cast<float>(maxWidth) / static_cast<float>((1.0f - cropX) * bitmap.getWidth());
    isScaled = true;
  }
  if (maxHeight > 0 && (1.0f - cropY) * bitmap.getHeight() > maxHeight) {
    scale = std::min(scale, static_cast<float>(maxHeight) / static_cast<float>((1.0f - cropY) * bitmap.getHeight()));
    isScaled = true;
  }
  Serial.printf("[%lu] [GFX] Scaling by %f - %s\n", millis(), scale, isScaled ? "scaled" : "not scaled");

  // Calculate output row size (2 bits per pixel, packed into bytes)
  // IMPORTANT: Use int, not uint8_t, to avoid overflow for images > 1020 pixels wide
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

  if (!outputRow || !rowBytes) {
    Serial.printf("[%lu] [GFX] !! Failed to allocate BMP row buffers\n", millis());
    free(outputRow);
    free(rowBytes);
    return;
  }

  for (int bmpY = 0; bmpY < (bitmap.getHeight() - cropPixY); bmpY++) {
    // The BMP's (0, 0) is the bottom-left corner (if the height is positive, top-left if negative).
    // Screen's (0, 0) is the top-left corner.
    int screenY = -cropPixY + (bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY);
    if (isScaled) {
      screenY = std::floor(screenY * scale);
    }
    screenY += y;  // the offset should not be scaled
    if (screenY >= getScreenHeight()) {
      break;
    }

    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      Serial.printf("[%lu] [GFX] Failed to read row %d from bitmap\n", millis(), bmpY);
      free(outputRow);
      free(rowBytes);
      return;
    }

    if (screenY < 0) {
      continue;
    }

    if (bmpY < cropPixY) {
      // Skip the row if it's outside the crop area
      continue;
    }

    for (int bmpX = cropPixX; bmpX < bitmap.getWidth() - cropPixX; bmpX++) {
      int screenX = bmpX - cropPixX;
      if (isScaled) {
        screenX = std::floor(screenX * scale);
      }
      screenX += x;  // the offset should not be scaled
      if (screenX >= getScreenWidth()) {
        break;
      }
      if (screenX < 0) {
        continue;
      }

      const uint8_t val = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

      if (renderMode == BW && val < 3) {
        drawPixel(screenX, screenY);
      } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
        drawPixel(screenX, screenY, false);
      } else if (renderMode == GRAYSCALE_LSB && val == 1) {
        drawPixel(screenX, screenY, false);
      }
    }
  }

  free(outputRow);
  free(rowBytes);
}

void GfxRenderer::drawBitmap1Bit(const Bitmap& bitmap, const int x, const int y, const int maxWidth,
                                 const int maxHeight) const {
  float scale = 1.0f;
  bool isScaled = false;
  if (maxWidth > 0 && bitmap.getWidth() > maxWidth) {
    scale = static_cast<float>(maxWidth) / static_cast<float>(bitmap.getWidth());
    isScaled = true;
  }
  if (maxHeight > 0 && bitmap.getHeight() > maxHeight) {
    scale = std::min(scale, static_cast<float>(maxHeight) / static_cast<float>(bitmap.getHeight()));
    isScaled = true;
  }

  // For 1-bit BMP, output is still 2-bit packed (for consistency with readNextRow)
  const int outputRowSize = (bitmap.getWidth() + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));

  if (!outputRow || !rowBytes) {
    Serial.printf("[%lu] [GFX] !! Failed to allocate 1-bit BMP row buffers\n", millis());
    free(outputRow);
    free(rowBytes);
    return;
  }

  for (int bmpY = 0; bmpY < bitmap.getHeight(); bmpY++) {
    // Read rows sequentially using readNextRow
    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      Serial.printf("[%lu] [GFX] Failed to read row %d from 1-bit bitmap\n", millis(), bmpY);
      free(outputRow);
      free(rowBytes);
      return;
    }

    // Calculate screen Y based on whether BMP is top-down or bottom-up
    const int bmpYOffset = bitmap.isTopDown() ? bmpY : bitmap.getHeight() - 1 - bmpY;
    int screenY = y + (isScaled ? static_cast<int>(std::floor(bmpYOffset * scale)) : bmpYOffset);
    if (screenY >= getScreenHeight()) {
      continue;  // Continue reading to keep row counter in sync
    }
    if (screenY < 0) {
      continue;
    }

    for (int bmpX = 0; bmpX < bitmap.getWidth(); bmpX++) {
      int screenX = x + (isScaled ? static_cast<int>(std::floor(bmpX * scale)) : bmpX);
      if (screenX >= getScreenWidth()) {
        break;
      }
      if (screenX < 0) {
        continue;
      }

      // Get 2-bit value (result of readNextRow quantization)
      const uint8_t val = outputRow[bmpX / 4] >> (6 - ((bmpX * 2) % 8)) & 0x3;

      // For 1-bit source: 0 or 1 -> map to black (0,1,2) or white (3)
      // val < 3 means black pixel (draw it)
      if (val < 3) {
        drawPixel(screenX, screenY, true);
      }
      // White pixels (val == 3) are not drawn (leave background)
    }
  }

  free(outputRow);
  free(rowBytes);
}

void GfxRenderer::fillPolygon(const int* xPoints, const int* yPoints, int numPoints, bool state) const {
  if (numPoints < 3) return;

  // Find bounding box
  int minY = yPoints[0], maxY = yPoints[0];
  for (int i = 1; i < numPoints; i++) {
    if (yPoints[i] < minY) minY = yPoints[i];
    if (yPoints[i] > maxY) maxY = yPoints[i];
  }

  // Clip to screen
  if (minY < 0) minY = 0;
  if (maxY >= getScreenHeight()) maxY = getScreenHeight() - 1;

  // Allocate node buffer for scanline algorithm
  auto* nodeX = static_cast<int*>(malloc(numPoints * sizeof(int)));
  if (!nodeX) {
    Serial.printf("[%lu] [GFX] !! Failed to allocate polygon node buffer\n", millis());
    return;
  }

  // Scanline fill algorithm
  for (int scanY = minY; scanY <= maxY; scanY++) {
    int nodes = 0;

    // Find all intersection points with edges
    int j = numPoints - 1;
    for (int i = 0; i < numPoints; i++) {
      if ((yPoints[i] < scanY && yPoints[j] >= scanY) || (yPoints[j] < scanY && yPoints[i] >= scanY)) {
        // Calculate X intersection using fixed-point to avoid float
        int dy = yPoints[j] - yPoints[i];
        if (dy != 0) {
          nodeX[nodes++] = xPoints[i] + (scanY - yPoints[i]) * (xPoints[j] - xPoints[i]) / dy;
        }
      }
      j = i;
    }

    // Sort nodes by X (simple bubble sort, numPoints is small)
    for (int i = 0; i < nodes - 1; i++) {
      for (int k = i + 1; k < nodes; k++) {
        if (nodeX[i] > nodeX[k]) {
          int temp = nodeX[i];
          nodeX[i] = nodeX[k];
          nodeX[k] = temp;
        }
      }
    }

    // Fill between pairs of nodes
    for (int i = 0; i < nodes - 1; i += 2) {
      int startX = nodeX[i];
      int endX = nodeX[i + 1];

      // Clip to screen
      if (startX < 0) startX = 0;
      if (endX >= getScreenWidth()) endX = getScreenWidth() - 1;

      // Draw horizontal line
      for (int x = startX; x <= endX; x++) {
        drawPixel(x, scanY, state);
      }
    }
  }

  free(nodeX);
}

// For performance measurement (using static to allow "const" methods)
static unsigned long start_ms = 0;

void GfxRenderer::clearScreen(const uint8_t color) const {
  start_ms = millis();
  display.clearScreen(color);
}

void GfxRenderer::invertScreen() const {
  for (int i = 0; i < HalDisplay::BUFFER_SIZE; i++) {
    frameBuffer[i] = ~frameBuffer[i];
  }
}

void GfxRenderer::displayBuffer(const HalDisplay::RefreshMode refreshMode) const {
  auto elapsed = millis() - start_ms;
  Serial.printf("[%lu] [GFX] Time = %lu ms from clearScreen to displayBuffer\n", millis(), elapsed);
  display.displayBuffer(refreshMode, fadingFix);
}

std::string GfxRenderer::truncatedText(const int fontId, const char* text, const int maxWidth,
                                       const EpdFontFamily::Style style) const {
  if (!text || maxWidth <= 0) return "";

  std::string item = text;
  const char* ellipsis = "...";
  int textWidth = getTextWidth(fontId, item.c_str(), style);
  if (textWidth <= maxWidth) {
    // Text fits, return as is
    return item;
  }

  while (!item.empty() && getTextWidth(fontId, (item + ellipsis).c_str(), style) >= maxWidth) {
    utf8RemoveLastChar(item);
  }

  return item.empty() ? ellipsis : item + ellipsis;
}

// Note: Internal driver treats screen in command orientation; this library exposes a logical orientation
int GfxRenderer::getScreenWidth() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 480px wide in portrait logical coordinates
      return HalDisplay::DISPLAY_HEIGHT;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 800px wide in landscape logical coordinates
      return HalDisplay::DISPLAY_WIDTH;
  }
  return HalDisplay::DISPLAY_HEIGHT;
}

int GfxRenderer::getScreenHeight() const {
  switch (orientation) {
    case Portrait:
    case PortraitInverted:
      // 800px tall in portrait logical coordinates
      return HalDisplay::DISPLAY_WIDTH;
    case LandscapeClockwise:
    case LandscapeCounterClockwise:
      // 480px tall in landscape logical coordinates
      return HalDisplay::DISPLAY_HEIGHT;
  }
  return HalDisplay::DISPLAY_WIDTH;
}

int GfxRenderer::getSpaceWidth(const int fontId) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

    const EpdGlyph* glyph = fontMap.at(fontId).getGlyph(' ', EpdFontFamily::REGULAR);
  if (!glyph) {
    // Serial.printf("[%lu] [GFX] Font %d (Regular) has no space glyph! Using fallback.\n", millis(), fontId);
    const EpdFontData* data = fontMap.at(fontId).getData(EpdFontFamily::REGULAR);
    if (data) {
      return data->ascender / 3;
    }
    return 0;
  }
  return glyph->advanceX;
}

int GfxRenderer::getTextAdvanceX(const int fontId, const char* text) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

  uint32_t cp;
  int width = 0;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    width += fontMap.at(fontId).getGlyph(cp, EpdFontFamily::REGULAR)->advanceX;
  }
  return width;
}

int GfxRenderer::getFontAscenderSize(const int fontId) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

    const EpdFontData* data = fontMap.at(fontId).getData(EpdFontFamily::REGULAR);
  if (!data) {
    Serial.printf("[%lu] [GFX] Font %d (Regular) has no data!\n", millis(), fontId);
    return 0;
  }

  return data->ascender;
}

int GfxRenderer::getLineHeight(const int fontId) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }

  const EpdFontData* data = fontMap.at(fontId).getData(EpdFontFamily::REGULAR);
  if (!data) {
    Serial.printf("[%lu] [GFX] Font %d (Regular) has no data!\n", millis(), fontId);
    return 0;
  }

  return data->advanceY;
}

int GfxRenderer::getTextHeight(const int fontId) const {
  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return 0;
  }
  return fontMap.at(fontId).getData(EpdFontFamily::REGULAR)->ascender;
}

void GfxRenderer::drawTextRotated90CW(const int fontId, const int x, const int y, const char* text, const bool black,
                                      const EpdFontFamily::Style style) const {
  // Cannot draw a NULL / empty string
  if (text == nullptr || *text == '\0') {
    return;
  }

  if (fontMap.count(fontId) == 0) {
    Serial.printf("[%lu] [GFX] Font %d not found\n", millis(), fontId);
    return;
  }
  const auto font = fontMap.at(fontId);

  // No printable characters
  if (!font.hasPrintableChars(text, style)) {
    return;
  }

  // For 90° clockwise rotation:
  // Original (glyphX, glyphY) -> Rotated (glyphY, -glyphX)
  // Text reads from bottom to top

  int yPos = y;  // Current Y position (decreases as we draw characters)

  uint32_t cp;
  while ((cp = utf8NextCodepoint(reinterpret_cast<const uint8_t**>(&text)))) {
    const EpdGlyph* glyph = font.getGlyph(cp, style);
    if (!glyph) {
      glyph = font.getGlyph(REPLACEMENT_GLYPH, style);
    }
    if (!glyph) {
      continue;
    }

    const int is2Bit = font.getData(style)->is2Bit;
    const uint32_t offset = glyph->dataOffset;
    const uint8_t width = glyph->width;
    const uint8_t height = glyph->height;
    const int left = glyph->left;
    const int top = glyph->top;

    // Use loadGlyphBitmap to support both static and custom (SD-based) fonts
    uint8_t* buffer = nullptr;  // Not used for now, as we expect a pointer or cache
    const uint8_t* bitmap = font.loadGlyphBitmap(glyph, buffer, style);

    if (bitmap != nullptr) {
      for (int glyphY = 0; glyphY < height; glyphY++) {
        for (int glyphX = 0; glyphX < width; glyphX++) {
          const int pixelPosition = glyphY * width + glyphX;

          // 90° clockwise rotation transformation:
          // screenX = x + (ascender - top + glyphY)
          // screenY = yPos - (left + glyphX)
          const int screenX = x + (font.getData(style)->ascender - top + glyphY);
          const int screenY = yPos - left - glyphX;

          if (is2Bit) {
            const uint8_t byte = bitmap[pixelPosition / 4];
            const uint8_t bit_index = (3 - pixelPosition % 4) * 2;
            const uint8_t bmpVal = 3 - (byte >> bit_index) & 0x3;

            if (renderMode == BW && bmpVal < 3) {
              drawPixel(screenX, screenY, black);
            } else if (renderMode == GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
              drawPixel(screenX, screenY, false);
            } else if (renderMode == GRAYSCALE_LSB && bmpVal == 1) {
              drawPixel(screenX, screenY, false);
            }
          } else {
            const uint8_t byte = bitmap[pixelPosition / 8];
            const uint8_t bit_index = 7 - (pixelPosition % 8);

            if ((byte >> bit_index) & 1) {
              drawPixel(screenX, screenY, black);
            }
          }
        }
      }
    }

    // Move to next character position (going up, so decrease Y)
    yPos -= glyph->advanceX;
  }
}

uint8_t* GfxRenderer::getFrameBuffer() const { return frameBuffer; }

size_t GfxRenderer::getBufferSize() { return HalDisplay::BUFFER_SIZE; }

// unused
// void GfxRenderer::grayscaleRevert() const { display.grayscaleRevert(); }

void GfxRenderer::copyGrayscaleLsbBuffers() const { display.copyGrayscaleLsbBuffers(frameBuffer); }

void GfxRenderer::copyGrayscaleMsbBuffers() const { display.copyGrayscaleMsbBuffers(frameBuffer); }

void GfxRenderer::displayGrayBuffer() const { display.displayGrayBuffer(fadingFix); }

void GfxRenderer::freeBwBufferChunks() {
  for (auto& bwBufferChunk : bwBufferChunks) {
    if (bwBufferChunk) {
      free(bwBufferChunk);
      bwBufferChunk = nullptr;
    }
  }
}

/**
 * This should be called before grayscale buffers are populated.
 * A `restoreBwBuffer` call should always follow the grayscale render if this method was called.
 * Uses chunked allocation to avoid needing 48KB of contiguous memory.
 * Returns true if buffer was stored successfully, false if allocation failed.
 */
bool GfxRenderer::storeBwBuffer() {
  // Allocate and copy each chunk
  for (size_t i = 0; i < BW_BUFFER_NUM_CHUNKS; i++) {
    // Check if any chunks are already allocated
    if (bwBufferChunks[i]) {
      Serial.printf("[%lu] [GFX] !! BW buffer chunk %zu already stored - this is likely a bug, freeing chunk\n",
                    millis(), i);
      free(bwBufferChunks[i]);
      bwBufferChunks[i] = nullptr;
    }

    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    bwBufferChunks[i] = static_cast<uint8_t*>(malloc(BW_BUFFER_CHUNK_SIZE));

    if (!bwBufferChunks[i]) {
      Serial.printf("[%lu] [GFX] !! Failed to allocate BW buffer chunk %zu (%zu bytes)\n", millis(), i,
                    BW_BUFFER_CHUNK_SIZE);
      // Free previously allocated chunks
      freeBwBufferChunks();
      return false;
    }

    memcpy(bwBufferChunks[i], frameBuffer + offset, BW_BUFFER_CHUNK_SIZE);
  }

  Serial.printf("[%lu] [GFX] Stored BW buffer in %zu chunks (%zu bytes each)\n", millis(), BW_BUFFER_NUM_CHUNKS,
                BW_BUFFER_CHUNK_SIZE);
  return true;
}

/**
 * This can only be called if `storeBwBuffer` was called prior to the grayscale render.
 * It should be called to restore the BW buffer state after grayscale rendering is complete.
 * Uses chunked restoration to match chunked storage.
 */
void GfxRenderer::restoreBwBuffer() {
  // Check if any all chunks are allocated
  bool missingChunks = false;
  for (const auto& bwBufferChunk : bwBufferChunks) {
    if (!bwBufferChunk) {
      missingChunks = true;
      break;
    }
  }

  if (missingChunks) {
    freeBwBufferChunks();
    return;
  }

  for (size_t i = 0; i < BW_BUFFER_NUM_CHUNKS; i++) {
    // Check if chunk is missing
    if (!bwBufferChunks[i]) {
      Serial.printf("[%lu] [GFX] !! BW buffer chunks not stored - this is likely a bug\n", millis());
      freeBwBufferChunks();
      return;
    }

    const size_t offset = i * BW_BUFFER_CHUNK_SIZE;
    memcpy(frameBuffer + offset, bwBufferChunks[i], BW_BUFFER_CHUNK_SIZE);
  }

  display.cleanupGrayscaleBuffers(frameBuffer);

  freeBwBufferChunks();
  Serial.printf("[%lu] [GFX] Restored and freed BW buffer chunks\n", millis());
}

/**
 * Cleanup grayscale buffers using the current frame buffer.
 * Use this when BW buffer was re-rendered instead of stored/restored.
 */
void GfxRenderer::cleanupGrayscaleWithFrameBuffer() const {
  if (frameBuffer) {
    display.cleanupGrayscaleBuffers(frameBuffer);
  }
}

void GfxRenderer::renderChar(const EpdFontFamily& fontFamily, const uint32_t cp, int* x, const int* y,
                             const bool pixelState, const EpdFontFamily::Style style) const {
  const EpdGlyph* glyph = fontFamily.getGlyph(cp, style);
  if (!glyph) {
    glyph = fontFamily.getGlyph(REPLACEMENT_GLYPH, style);
  }

  if (!glyph) {
    Serial.printf("[%lu] [GFX] No glyph for codepoint %d\n", millis(), cp);
    return;
  }

  const EpdFont* font = fontFamily.getFont(style);
  if (!font) return;
  const EpdFontData* data = font->getData(style);
  if (!data) return;

  const int is2Bit = data->is2Bit;
  const uint8_t width = glyph->width;
  const uint8_t height = glyph->height;
  const int left = glyph->left;

  const uint8_t* bitmap = font->loadGlyphBitmap(glyph, nullptr, style);

  if (bitmap != nullptr) {
    for (int glyphY = 0; glyphY < height; glyphY++) {
      const int screenY = *y - glyph->top + glyphY;
      for (int glyphX = 0; glyphX < width; glyphX++) {
        const int pixelPosition = glyphY * width + glyphX;
        const int screenX = *x + left + glyphX;

        if (is2Bit) {
          const uint8_t byte = bitmap[pixelPosition / 4];
          const uint8_t bit_index = (3 - pixelPosition % 4) * 2;
          // the direct bit from the font is 0 -> white, 1 -> light gray, 2 -> dark gray, 3 -> black
          // we swap this to better match the way images and screen think about colors:
          // 0 -> black, 1 -> dark grey, 2 -> light grey, 3 -> white
          const uint8_t bmpVal = 3 - (byte >> bit_index) & 0x3;

          if (renderMode == BW && bmpVal < 3) {
            // Black (also paints over the grays in BW mode)
            drawPixel(screenX, screenY, pixelState);
          } else if (renderMode == GRAYSCALE_MSB && (bmpVal == 1 || bmpVal == 2)) {
            // Light gray (also mark the MSB if it's going to be a dark gray too)
            // We have to flag pixels in reverse for the gray buffers, as 0 leave alone, 1 update
            drawPixel(screenX, screenY, false);
          } else if (renderMode == GRAYSCALE_LSB && bmpVal == 1) {
            // Dark gray
            drawPixel(screenX, screenY, false);
          }
        } else {
          const uint8_t byte = bitmap[pixelPosition / 8];
          const uint8_t bit_index = 7 - (pixelPosition % 8);

          if ((byte >> bit_index) & 1) {
            drawPixel(screenX, screenY, pixelState);
          }
        }
      }
    }
  }

  *x += glyph->advanceX;
}



void GfxRenderer::getOrientedViewableTRBL(int* outTop, int* outRight, int* outBottom, int* outLeft) const {
  switch (orientation) {
    case Portrait:
      *outTop = VIEWABLE_MARGIN_TOP;
      *outRight = VIEWABLE_MARGIN_RIGHT;
      *outBottom = VIEWABLE_MARGIN_BOTTOM;
      *outLeft = VIEWABLE_MARGIN_LEFT;
      break;
    case LandscapeClockwise:
      *outTop = VIEWABLE_MARGIN_LEFT;
      *outRight = VIEWABLE_MARGIN_TOP;
      *outBottom = VIEWABLE_MARGIN_RIGHT;
      *outLeft = VIEWABLE_MARGIN_BOTTOM;
      break;
    case PortraitInverted:
      *outTop = VIEWABLE_MARGIN_BOTTOM;
      *outRight = VIEWABLE_MARGIN_LEFT;
      *outBottom = VIEWABLE_MARGIN_TOP;
      *outLeft = VIEWABLE_MARGIN_RIGHT;
      break;
    case LandscapeCounterClockwise:
      *outTop = VIEWABLE_MARGIN_RIGHT;
      *outRight = VIEWABLE_MARGIN_BOTTOM;
      *outBottom = VIEWABLE_MARGIN_LEFT;
      *outLeft = VIEWABLE_MARGIN_TOP;
      break;
  }
}
void GfxRenderer::drawPngFromTxtpng(const char* txtpng_file_path) const {
    // 固定txtpng分辨率：480×800，与屏幕物理分辨率匹配
    const int pngWidth = 480;
    const int pngHeight = 800;

    // 1. 打开txtpng文件（复用你的SdMan文件操作）
    FsFile txtpng_file;
    if (!SdMan.openFileForRead("GFD", txtpng_file_path, txtpng_file)) {
        Serial.printf("[%lu] [GFX] Failed to open txtpng: %s\n", millis(), txtpng_file_path);
        return;
    }

    // 2. 单行缓冲区：适配480个数值+空格，2048字节足够
    char line_buffer[2048];
    // pngY：txtpng的行号（对应原始y坐标 0~799）
    int pngY = 0;

    // 3. 逐行读取txtpng（一行对应一个y坐标）
    while (txtpng_file.available() && pngY < pngHeight) {
        // 读取一行并补结束符，避免乱码
        int line_len = txtpng_file.readBytesUntil('\n', line_buffer, sizeof(line_buffer) - 1);
        line_buffer[line_len] = '\0';

        // 计算屏幕实际Y坐标：绘制偏移y + txtpng原始y
        int screenY = pngY;
        // 超出屏幕高度，直接终止绘制
        if (screenY >= getScreenHeight()) {
            break;
        }

        // 4. 分割当前行的灰度值（一个数值对应一个x坐标）
        char* token = strtok(line_buffer, " ");
        // pngX：txtpng的列号（对应原始x坐标 0~479）
        int pngX = 0;

        while (token != nullptr && pngX < pngWidth) {
            // 计算屏幕实际X坐标：绘制偏移x + txtpng原始x
            int screenX =  pngX;
            // 超出屏幕宽度，跳过当前行剩余像素
            if (screenX >= getScreenWidth()) {
                break;
            }

            // 5. 解析灰度值：-1=透明（跳过），0~255=有效灰度
            int gray_255 = atoi(token);
            uint8_t val = 0; // 映射为4级灰阶值（0~3），对齐drawBitmap的val

            // 有效像素判断+灰度值映射（0=白，3=黑，1=浅灰，2=深灰）
            if (gray_255 != -1 && gray_255 >= 0 && gray_255 <= 255) {
                if (gray_255 < 64)      val = 3;
                else if (gray_255 < 128) val = 2;
                else if (gray_255 < 192) val = 1;
                else                    val = 0;
            } else {
                // 透明/无效像素：跳过，解析下一个
                token = strtok(nullptr, " ");
                pngX++;
                continue;
            }

            // 6. 按当前渲染模式绘制：与drawBitmap判断逻辑完全一致
            // 清空区域
            if (renderMode == BW && val < 4) {
              drawPixel(screenX, screenY,false);
            }

          if (renderMode == BW && val < 3) {
            drawPixel(screenX, screenY);
          } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
            drawPixel(screenX, screenY, false);
          } else if (renderMode == GRAYSCALE_LSB && val == 1) {
            drawPixel(screenX, screenY, false);
          }

            // 解析下一个灰度值，x坐标+1
            token = strtok(nullptr, " ");
            pngX++;
        }

        // 读取下一行，y坐标+1
        pngY++;
    }

    // 7. 关闭文件，释放资源
    txtpng_file.close();
    Serial.printf("[%lu] [GFX] Png draw completed (mode: %d)\n", millis(), renderMode);
}

// 圓角矩形外角遮罩（RoundedRaff theme 用）— 從 Carousel 移植
// 把矩形的 4 個外角（圓弧外的部分）填成 color，讓書封呈現圓角
void GfxRenderer::maskRoundedRectOutsideCorners(const int x, const int y, const int width, const int height,
                                                const int radius, const Color color) const {
  if (radius <= 0 || color == Color::Clear) {
    return;
  }

  const int rr = radius - 1;
  const int rr2 = rr * rr;
  for (int dy = 0; dy < radius; dy++) {
    for (int dx = 0; dx < radius; dx++) {
      const int tx = rr - dx;
      const int ty = rr - dy;
      if (tx * tx + ty * ty > rr2) {
        if (color == Color::White || color == Color::Black) {
          bool state = color == Color::Black;
          drawPixel(x + dx, y + dy, state);
          drawPixel(x + width - 1 - dx, y + dy, state);
          drawPixel(x + dx, y + height - 1 - dy, state);
          drawPixel(x + width - 1 - dx, y + height - 1 - dy, state);
        } else if (color == Color::LightGray) {
          drawPixelDither<Color::LightGray>(x + dx, y + dy);
          drawPixelDither<Color::LightGray>(x + width - 1 - dx, y + dy);
          drawPixelDither<Color::LightGray>(x + dx, y + height - 1 - dy);
          drawPixelDither<Color::LightGray>(x + width - 1 - dx, y + height - 1 - dy);
        } else if (color == Color::DarkGray) {
          drawPixelDither<Color::DarkGray>(x + dx, y + dy);
          drawPixelDither<Color::DarkGray>(x + width - 1 - dx, y + dy);
          drawPixelDither<Color::DarkGray>(x + dx, y + height - 1 - dy);
          drawPixelDither<Color::DarkGray>(x + width - 1 - dx, y + height - 1 - dy);
        }
      }
    }
  }
}

// 透視梯形繪圖（Flow theme 輪播用）— 從 Carousel 移植，砍掉 fontCacheManager_ 檢查
void GfxRenderer::drawPerspectiveBitmap(const Bitmap& bitmap, const int x, const int y, const int w, const int hL,
                                        const int hR) const {
  if (w <= 0 || hL <= 0 || hR <= 0) return;

  const int srcW = bitmap.getWidth();
  const int srcH = bitmap.getHeight();
  if (srcW <= 0 || srcH <= 0) return;

  const int hMax = (hL > hR) ? hL : hR;
  const int screenW = getScreenWidth();
  const int screenH = getScreenHeight();
  const bool topDown = bitmap.isTopDown();

  // Same row buffer pattern as drawBitmap (2 bits per pixel quantized).
  const int outputRowSize = (srcW + 3) / 4;
  auto* outputRow = static_cast<uint8_t*>(malloc(outputRowSize));
  auto* rowBytes = static_cast<uint8_t*>(malloc(bitmap.getRowBytes()));
  if (!outputRow || !rowBytes) {
    Serial.printf("[%lu] [GFX] !! Failed to allocate perspective row buffers\n", millis());
    free(outputRow);
    free(rowBytes);
    return;
  }

  for (int srcY = 0; srcY < srcH; srcY++) {
    if (bitmap.readNextRow(outputRow, rowBytes) != BmpReaderError::Ok) {
      Serial.printf("[%lu] [GFX] Failed to read row %d from bitmap (perspective)\n", millis(), srcY);
      free(outputRow);
      free(rowBytes);
      return;
    }
    const int srcRowIndex = topDown ? srcY : (srcH - 1 - srcY);

    for (int dx = 0; dx < w; dx++) {
      const int colH = (w == 1) ? hL : (hL + (hR - hL) * dx / (w - 1));
      if (colH <= 0) continue;
      const int colTop = (hMax - colH) / 2;
      const int dy = (srcRowIndex * colH) / srcH;
      const int screenX = x + dx;
      const int screenY = y + colTop + dy;
      if (screenX < 0 || screenX >= screenW) continue;
      if (screenY < 0 || screenY >= screenH) continue;

      const int srcX = (dx * srcW) / w;
      const uint8_t val = (outputRow[srcX / 4] >> (6 - ((srcX * 2) % 8))) & 0x3;

      if (renderMode == BW && val < 3) {
        drawPixel(screenX, screenY);
      } else if (renderMode == GRAYSCALE_MSB && (val == 1 || val == 2)) {
        drawPixel(screenX, screenY, false);
      } else if (renderMode == GRAYSCALE_LSB && val == 1) {
        drawPixel(screenX, screenY, false);
      }
    }
  }

  free(outputRow);
  free(rowBytes);
}
