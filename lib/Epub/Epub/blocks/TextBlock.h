#pragma once
#include <EpdFontFamily.h>
#include <SdFat.h>

#include <list>
#include <memory>
#include <string>

#include "Block.h"
#include "BlockStyle.h"

// Represents a line of text on a page
class TextBlock final : public Block {
 private:
  std::list<std::string> words;
  std::list<uint16_t> wordXpos;
  std::list<EpdFontFamily::Style> wordStyles;
  BlockStyle blockStyle;

 public:
  explicit TextBlock(std::list<std::string> words, std::list<uint16_t> word_xpos,
                     std::list<EpdFontFamily::Style> word_styles, const BlockStyle& blockStyle = BlockStyle())
      : words(std::move(words)),
        wordXpos(std::move(word_xpos)),
        wordStyles(std::move(word_styles)),
        blockStyle(blockStyle) {}
  ~TextBlock() override = default;
  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  const BlockStyle& getBlockStyle() const { return blockStyle; }
  bool isEmpty() override { return words.empty(); }
  // stage15.5: 直排支援 — 給上層拿到整行文字內容（不洩漏橫排座標）
  std::string concatText() const {
    std::string result;
    for (const auto& w : words) result += w;
    return result;
  }
  bool getHorizontalPixelBoundsY(const GfxRenderer& renderer, int fontId, int y, int* top, int* bottom) const;
  //void layout(GfxRenderer& renderer) override {};
  // given a renderer works out where to break the words into lines
  void render(const GfxRenderer& renderer, int fontId, int x, int y) const;
  BlockType getType() override { return TEXT_BLOCK; }
  bool serialize(FsFile& file) const;
  static std::unique_ptr<TextBlock> deserialize(FsFile& file);
};
