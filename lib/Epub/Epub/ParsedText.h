#pragma once

#include <EpdFontFamily.h>

#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

#include "blocks/BlockStyle.h"
#include "blocks/TextBlock.h"

class GfxRenderer;

class ParsedText {
  std::list<std::string> words;
  std::list<EpdFontFamily::Style> wordStyles;
  std::list<bool> wordContinues;  // true = word attaches to previous (no space before it)
  BlockStyle blockStyle;
  bool extraParagraphSpacing;
  bool hyphenationEnabled;
  bool firstlineintented;
  uint8_t wordSpacing;

  void applyParagraphIndent();
  std::vector<size_t> computeLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth, int spaceWidth,
                                        std::vector<uint16_t>& wordWidths, std::vector<bool>& continuesVec);
  std::vector<size_t> computeHyphenatedLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth,
                                                  int spaceWidth, std::vector<uint16_t>& wordWidths,
                                                  std::vector<bool>& continuesVec);
  bool hyphenateWordAtIndex(size_t wordIndex, int availableWidth, const GfxRenderer& renderer, int fontId,
                            std::vector<uint16_t>& wordWidths, bool allowFallbackBreaks,
                            std::vector<bool>* continuesVec = nullptr);
  void extractLine(size_t breakIndex, int pageWidth, int spaceWidth, const std::vector<uint16_t>& wordWidths,
                   const std::vector<bool>& continuesVec, const std::vector<size_t>& lineBreakIndices,
                   const std::function<void(std::shared_ptr<TextBlock>)>& processLine,const GfxRenderer& renderer, int fontId);
  std::vector<uint16_t> calculateWordWidths(const GfxRenderer& renderer, int fontId);


 public:
  explicit ParsedText(const bool extraParagraphSpacing, const bool hyphenationEnabled = false, const uint8_t wordSpacing = 0,
                      const bool firstlineintented = false,
                      const BlockStyle& blockStyle = BlockStyle())
      : blockStyle(blockStyle), extraParagraphSpacing(extraParagraphSpacing), hyphenationEnabled(hyphenationEnabled),
        wordSpacing(wordSpacing), firstlineintented(firstlineintented) {}
  ~ParsedText() = default;

  void addWord(std::string word, EpdFontFamily::Style fontStyle, bool underline = false, bool attachToPrevious = false);
  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  BlockStyle& getBlockStyle() { return blockStyle; }
  size_t size() const { return words.size(); }
  bool isEmpty() const { return words.empty(); }
  void layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                             bool includeLastLine = true);
  // stage15.14 (SAM 移植): 直排切欄、按 viewportHeight 切、字一個一個塞、滿就換欄
  // stage15.16: 加 columnState 參數讓 caller 控制跨段續塞、不留段落間空白
  struct VerticalColumnState {
    std::list<std::string> columnWords;
    std::list<uint16_t> columnYpos;
    std::list<EpdFontFamily::Style> columnStyles;
    uint16_t nextY = 0;
    uint16_t lineHeight = 0;
    uint16_t charAdvance = 0;
    uint16_t viewportHeight = 0;
    CssTextAlign alignment = CssTextAlign::Justify;
    bool firstColumn = true;
  };
  void layoutAndExtractVerticalColumns(const GfxRenderer& renderer, int fontId, uint16_t viewportHeight,
                                       float lineCompression,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processColumn,
                                       VerticalColumnState* state = nullptr);
  // 把 state 內殘留字 flush 出去（章節結束時呼叫）
  static void flushVerticalColumnState(VerticalColumnState* state, const BlockStyle& blockStyle,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processColumn);
};
