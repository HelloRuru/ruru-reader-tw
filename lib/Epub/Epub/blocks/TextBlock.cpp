#include "TextBlock.h"

#include <GfxRenderer.h>
#include <Serialization.h>

#include "../../../../src/CrossPointSettings.h"

void TextBlock::render(const GfxRenderer& renderer, const int fontId, const int x, const int y) const {
  // Validate iterator bounds before rendering
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size()) {
    Serial.printf("[%lu] [TXB] Render skipped: size mismatch (words=%u, xpos=%u, styles=%u)\n", millis(),
                  (uint32_t)words.size(), (uint32_t)wordXpos.size(), (uint32_t)wordStyles.size());
    return;
  }

  auto wordIt = words.begin();
  auto wordStylesIt = wordStyles.begin();
  auto wordXposIt = wordXpos.begin();
  // stage15.14 (SAM 移植): 直排分支 — wordXpos 重用為 Y 座標、x 是欄中心
  // stage15.15: 拿掉 getScreenHeight() break 條件
  // stage15.18 修 bug: 用 drawVerticalText 不是 drawText
  //   drawText 沒處理 Unicode 直排碼點對照（句號、逗號等標點直排版）
  //   會造成標點位置不對 + 對不上原文
  if (blockStyle.verticalLayout) {
    // PageLine.xPos 是「欄中心 X」、drawVerticalText 要的是「欄左邊 X」
    // colWidth = lineHeight（直排每字格寬 = 字級高）
    const int colWidth = renderer.getLineHeight(fontId);
    const int colLeft = x - colWidth / 2;
    const int topInset = renderer.getVerticalTextTopInset(fontId);
    for (size_t i = 0; i < words.size(); i++) {
      const int drawY = y + *wordXposIt - topInset;  // 直排：wordXpos 存 Y；扣掉字形上方空白讓欄頂貼齊
      const EpdFontFamily::Style currentStyle = *wordStylesIt;
      // drawVerticalText 處理直排碼點對照 + 標點置中
      // 每個 word 只有一個 codepoint、畫完不會推進到下一個（後面字座標由 wordYpos 控制）
      renderer.drawVerticalText(fontId, colLeft, drawY, wordIt->c_str(), true, currentStyle);
      std::advance(wordIt, 1);
      std::advance(wordStylesIt, 1);
      std::advance(wordXposIt, 1);
    }
    return;
  }
  for (size_t i = 0; i < words.size(); i++) {
    const int wordX = *wordXposIt + x;
    const EpdFontFamily::Style currentStyle = *wordStylesIt;
    renderer.drawText(fontId, wordX, y, wordIt->c_str(), true, currentStyle);

    if ((currentStyle & EpdFontFamily::UNDERLINE) != 0) {
      const std::string& w = *wordIt;
      const int fullWordWidth = renderer.getTextWidth(fontId, w.c_str(), currentStyle);
      // y is the top of the text line; add ascender to reach baseline, then apply reader setting offset.
      const int underlineY = y + renderer.getFontAscenderSize(fontId) + SETTINGS.underlineBelowOffset;

      int startX = wordX;
      int underlineWidth = fullWordWidth;

      // if word starts with em-space ("\xe2\x80\x83"), account for the additional indent before drawing the line
      if (w.size() >= 3 && static_cast<uint8_t>(w[0]) == 0xE2 && static_cast<uint8_t>(w[1]) == 0x80 &&
          static_cast<uint8_t>(w[2]) == 0x83) {
        const char* visiblePtr = w.c_str() + 3;
        const int prefixWidth = renderer.getTextAdvanceX(fontId, std::string("\xe2\x80\x83").c_str());
        const int visibleWidth = renderer.getTextWidth(fontId, visiblePtr, currentStyle);
        startX = wordX + prefixWidth;
        underlineWidth = visibleWidth;
      }

      renderer.drawLine(startX, underlineY, startX + underlineWidth, underlineY, true);
    }

    std::advance(wordIt, 1);
    std::advance(wordStylesIt, 1);
    std::advance(wordXposIt, 1);
  }
}

bool TextBlock::getHorizontalPixelBoundsY(const GfxRenderer& renderer, const int fontId, const int y, int* top,
                                          int* bottom) const {
  if (!top || !bottom || blockStyle.verticalLayout) {
    return false;
  }

  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  int minY = 32767;
  int maxY = -32768;
  bool found = false;

  while (wordIt != words.end() && styleIt != wordStyles.end()) {
    int wordTop = 0;
    int wordBottom = 0;
    if (renderer.getTextPixelBoundsY(fontId, wordIt->c_str(), y, &wordTop, &wordBottom, *styleIt)) {
      minY = std::min(minY, wordTop);
      maxY = std::max(maxY, wordBottom);
      found = true;
    }
    std::advance(wordIt, 1);
    std::advance(styleIt, 1);
  }

  if (!found) {
    return false;
  }
  *top = minY;
  *bottom = maxY;
  return true;
}

bool TextBlock::serialize(FsFile& file) const {
  if (words.size() != wordXpos.size() || words.size() != wordStyles.size()) {
    Serial.printf("[%lu] [TXB] Serialization failed: size mismatch (words=%u, xpos=%u, styles=%u)\n", millis(),
                  words.size(), wordXpos.size(), wordStyles.size());
    return false;
  }

  // Word data
  serialization::writePod(file, static_cast<uint16_t>(words.size()));
  for (const auto& w : words) serialization::writeString(file, w);
  for (auto x : wordXpos) serialization::writePod(file, x);
  for (auto s : wordStyles) serialization::writePod(file, s);

  // Style (alignment + margins/padding/indent)
  serialization::writePod(file, blockStyle.alignment);
  serialization::writePod(file, blockStyle.textAlignDefined);
  serialization::writePod(file, blockStyle.marginTop);
  serialization::writePod(file, blockStyle.marginBottom);
  serialization::writePod(file, blockStyle.marginLeft);
  serialization::writePod(file, blockStyle.marginRight);
  serialization::writePod(file, blockStyle.paddingTop);
  serialization::writePod(file, blockStyle.paddingBottom);
  serialization::writePod(file, blockStyle.paddingLeft);
  serialization::writePod(file, blockStyle.paddingRight);
  serialization::writePod(file, blockStyle.textIndent);
  serialization::writePod(file, blockStyle.textIndentDefined);
  // stage15.14: 直排支援
  serialization::writePod(file, blockStyle.verticalLayout);

  return true;
}

std::unique_ptr<TextBlock> TextBlock::deserialize(FsFile& file) {
  uint16_t wc;
  std::list<std::string> words;
  std::list<uint16_t> wordXpos;
  std::list<EpdFontFamily::Style> wordStyles;
  BlockStyle blockStyle;

  // Word count
  serialization::readPod(file, wc);

  // Sanity check: prevent allocation of unreasonably large lists (max 10000 words per block)
  if (wc > 10000) {
    Serial.printf("[%lu] [TXB] Deserialization failed: word count %u exceeds maximum\n", millis(), wc);
    return nullptr;
  }

  // Word data
  words.resize(wc);
  wordXpos.resize(wc);
  wordStyles.resize(wc);
  //for (auto& w : words) serialization::readString(file, w);
  wordStyles.resize(wc);
  int i = 0;
  for (auto& w : words) {
    if (i % 100 == 0 && i > 0) Serial.printf("[%lu] [TXB] Reading word %d/%d\n", millis(), i, wc);
    serialization::readString(file, w);
    i++;
  }
  for (auto& x : wordXpos) serialization::readPod(file, x);
  for (auto& s : wordStyles) serialization::readPod(file, s);

  // Style (alignment + margins/padding/indent)
  serialization::readPod(file, blockStyle.alignment);
  serialization::readPod(file, blockStyle.textAlignDefined);
  serialization::readPod(file, blockStyle.marginTop);
  serialization::readPod(file, blockStyle.marginBottom);
  serialization::readPod(file, blockStyle.marginLeft);
  serialization::readPod(file, blockStyle.marginRight);
  serialization::readPod(file, blockStyle.paddingTop);
  serialization::readPod(file, blockStyle.paddingBottom);
  serialization::readPod(file, blockStyle.paddingLeft);
  serialization::readPod(file, blockStyle.paddingRight);
  serialization::readPod(file, blockStyle.textIndent);
  serialization::readPod(file, blockStyle.textIndentDefined);
  // stage15.14: 直排支援
  serialization::readPod(file, blockStyle.verticalLayout);

  return std::unique_ptr<TextBlock>(
      new TextBlock(std::move(words), std::move(wordXpos), std::move(wordStyles), blockStyle));
}
