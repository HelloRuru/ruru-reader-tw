// stage15.37: 直排 layout 演算法單元測試
// 目的：在 WSL/Linux x86 上不用 ESP32 硬體、直接測 layoutAndExtractVerticalColumns 的字數正確性
//
// 編譯：
//   wsl bash -c "cd /mnt/d/RURU-ALL/Library/工具/閱星曈刷機/Carousel-繁中版/tests && \
//     g++ -std=c++17 -O0 -g vertical_layout_test.cpp -o vertical_layout_test && \
//     ./vertical_layout_test"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>
#include <algorithm>

// ===== Mock 必要的 type =====

namespace EpdFontFamily {
enum Style { REGULAR = 0, BOLD = 1, ITALIC = 2, UNDERLINE = 4 };
}

struct BlockStyle {
  uint8_t alignment = 0;
  bool textAlignDefined = false;
  uint16_t marginTop = 0;
  uint16_t marginBottom = 0;
  uint16_t marginLeft = 0;
  uint16_t marginRight = 0;
  uint16_t paddingTop = 0;
  uint16_t paddingBottom = 0;
  uint16_t paddingLeft = 0;
  uint16_t paddingRight = 0;
  uint16_t textIndent = 0;
  bool textIndentDefined = false;
  bool verticalLayout = false;
};

struct TextBlock {
  std::list<std::string> words;
  std::list<uint16_t> wordXpos;
  std::list<EpdFontFamily::Style> wordStyles;
  BlockStyle blockStyle;
  TextBlock(std::list<std::string> w, std::list<uint16_t> xp,
            std::list<EpdFontFamily::Style> ws, BlockStyle bs)
      : words(std::move(w)), wordXpos(std::move(xp)),
        wordStyles(std::move(ws)), blockStyle(bs) {}
};

struct GfxRenderer {
  int lineHeight = 22;
  int getLineHeight(int /*fontId*/) const { return lineHeight; }
};

// ===== 把 ParsedText::layoutAndExtractVerticalColumns 的邏輯複製過來 =====
// （以 stage15.34 版本為基準）

namespace {
bool isCJKCodepoint(unsigned int c) {
  if (c >= 0x4E00 && c <= 0x9FFF) return true;
  if (c >= 0x3400 && c <= 0x4DBF) return true;
  if (c >= 0x3000 && c <= 0x303F) return true;
  if (c >= 0xFF00 && c <= 0xFFEF) return true;
  if (c >= 0x3100 && c <= 0x312F) return true;
  return false;
}

int decodeUtf8(const char* s, size_t maxLen, unsigned int& out) {
  if (maxLen == 0) return 0;
  const unsigned char c = static_cast<unsigned char>(s[0]);
  if (c < 0x80) { out = c; return 1; }
  if ((c & 0xE0) == 0xC0 && maxLen >= 2) {
    out = (c & 0x1F) << 6 | (static_cast<unsigned char>(s[1]) & 0x3F);
    return 2;
  }
  if ((c & 0xF0) == 0xE0 && maxLen >= 3) {
    out = (c & 0x0F) << 12 | (static_cast<unsigned char>(s[1]) & 0x3F) << 6
        | (static_cast<unsigned char>(s[2]) & 0x3F);
    return 3;
  }
  if ((c & 0xF8) == 0xF0 && maxLen >= 4) {
    out = (c & 0x07) << 18 | (static_cast<unsigned char>(s[1]) & 0x3F) << 12
        | (static_cast<unsigned char>(s[2]) & 0x3F) << 6 | (static_cast<unsigned char>(s[3]) & 0x3F);
    return 4;
  }
  out = c;
  return 1;
}
}  // namespace

struct VerticalColumnState {
  std::list<std::string> columnWords;
  std::list<uint16_t> columnYpos;
  std::list<EpdFontFamily::Style> columnStyles;
  uint16_t nextY = 0;
  bool firstColumn = true;
};

void layoutAndExtractVerticalColumns(
    const std::list<std::string>& words,
    const std::list<EpdFontFamily::Style>& wordStyles,
    bool firstlineintented,
    const BlockStyle& blockStyle,
    const GfxRenderer& renderer, int fontId, uint16_t viewportHeight, float lineCompression,
    const std::function<void(std::shared_ptr<TextBlock>)>& processColumn,
    VerticalColumnState* state) {
  if (words.empty()) return;

  const int lineHeight = static_cast<int>(renderer.getLineHeight(fontId) * lineCompression);
  const int charAdvance = lineHeight;
  if (charAdvance <= 0 || viewportHeight < lineHeight) return;

  // Step 1: 把所有 words 串接成 codepoint 流
  struct CpEntry {
    std::string utf8;
    EpdFontFamily::Style style;
    unsigned int codepoint;
  };
  std::vector<CpEntry> codepoints;
  codepoints.reserve(words.size() * 2);

  auto wordIt = words.begin();
  auto styleIt = wordStyles.begin();
  while (wordIt != words.end()) {
    const std::string& word = *wordIt;
    size_t pos = 0;
    while (pos < word.size()) {
      unsigned int cp = 0;
      int len = decodeUtf8(word.c_str() + pos, word.size() - pos, cp);
      if (len <= 0) { pos++; continue; }
      CpEntry entry;
      entry.utf8 = word.substr(pos, len);
      entry.style = *styleIt;
      entry.codepoint = cp;
      codepoints.push_back(std::move(entry));
      pos += len;
    }
    ++wordIt;
    ++styleIt;
  }

  // Step 2: 1 codepoint = 1 unit、只跳過空白
  struct VerticalUnit {
    std::string utf8;
    EpdFontFamily::Style style;
  };
  std::vector<VerticalUnit> units;
  units.reserve(codepoints.size());

  for (const auto& cpe : codepoints) {
    const unsigned int cp = cpe.codepoint;
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\r') continue;
    units.push_back({cpe.utf8, cpe.style});
  }

  // Step 3: 塞欄
  VerticalColumnState localState;
  VerticalColumnState* st = state ? state : &localState;
  if (st->firstColumn && st->columnWords.empty() && firstlineintented) {
    st->nextY = static_cast<uint16_t>(std::min(lineHeight * 2, static_cast<int>(viewportHeight - lineHeight)));
  }

  auto flushColumnLocal = [&]() {
    if (st->columnWords.empty()) return;
    BlockStyle columnStyle = blockStyle;
    columnStyle.verticalLayout = true;
    processColumn(std::make_shared<TextBlock>(st->columnWords, st->columnYpos, st->columnStyles, columnStyle));
    st->columnWords.clear();
    st->columnYpos.clear();
    st->columnStyles.clear();
    st->nextY = 0;
    st->firstColumn = false;
  };

  for (const auto& unit : units) {
    if (st->nextY + lineHeight > viewportHeight) {
      flushColumnLocal();
    }
    st->columnWords.push_back(unit.utf8);
    st->columnYpos.push_back(st->nextY);
    st->columnStyles.push_back(unit.style);
    st->nextY = static_cast<uint16_t>(st->nextY + charAdvance);
  }

  if (!state) flushColumnLocal();
}

// ===== 測試用工具 =====

int countCodepoints(const std::string& s) {
  int count = 0;
  size_t pos = 0;
  while (pos < s.size()) {
    unsigned int cp = 0;
    int len = decodeUtf8(s.c_str() + pos, s.size() - pos, cp);
    if (len <= 0) { pos++; continue; }
    // 跳過 ASCII 空白
    if (cp != ' ' && cp != '\t' && cp != '\n' && cp != '\r') count++;
    pos += len;
  }
  return count;
}

// Mock processColumn：收集所有 column 的字
struct TestResult {
  std::vector<std::vector<std::string>> columns;  // 每欄的 utf8 list
  int totalUnits = 0;
};

TestResult runLayout(const std::string& input, int viewportHeight,
                     float lineCompression = 1.0f, int lineHeight = 22,
                     bool firstlineintented = false) {
  TestResult result;
  GfxRenderer renderer;
  renderer.lineHeight = lineHeight;
  BlockStyle bs;

  // 把 input 切成 word（按空白分）
  std::list<std::string> words;
  std::list<EpdFontFamily::Style> styles;
  std::string current;
  for (char c : input) {
    if (c == ' ') {
      if (!current.empty()) {
        words.push_back(current);
        styles.push_back(EpdFontFamily::REGULAR);
        current.clear();
      }
      words.push_back(" ");
      styles.push_back(EpdFontFamily::REGULAR);
    } else {
      current += c;
    }
  }
  if (!current.empty()) {
    words.push_back(current);
    styles.push_back(EpdFontFamily::REGULAR);
  }

  auto processColumn = [&](std::shared_ptr<TextBlock> tb) {
    std::vector<std::string> col;
    for (const auto& w : tb->words) {
      col.push_back(w);
      result.totalUnits++;
    }
    result.columns.push_back(std::move(col));
  };

  layoutAndExtractVerticalColumns(
      words, styles, firstlineintented, bs, renderer, 0,
      viewportHeight, lineCompression, processColumn, nullptr);

  return result;
}

// ===== 測試 =====

struct TestCase {
  const char* name;
  std::string input;
  int viewportHeight;
  float lineCompression;
  int lineHeight;
  bool firstlineintented;
};

void runTest(const TestCase& tc, int& passed, int& failed) {
  TestResult r = runLayout(tc.input, tc.viewportHeight, tc.lineCompression,
                           tc.lineHeight, tc.firstlineintented);
  int expected = countCodepoints(tc.input);

  bool ok = (r.totalUnits == expected);
  printf("[%s] %s: input=%d cp, output=%d cp, columns=%zu (lineH=%d, lc=%.2f, vh=%d)\n",
         ok ? "PASS" : "FAIL",
         tc.name, expected, r.totalUnits, r.columns.size(),
         tc.lineHeight, tc.lineCompression, tc.viewportHeight);

  if (!ok) {
    printf("  >>> 漏字 %d 個 <<<\n", expected - r.totalUnits);
    // 印出每欄內容
    for (size_t i = 0; i < r.columns.size(); i++) {
      printf("  col[%zu]: ", i);
      for (const auto& u : r.columns[i]) printf("[%s]", u.c_str());
      printf("\n");
    }
    failed++;
  } else {
    passed++;
  }
}

int main() {
  printf("=== 直排 layout 單元測試 (stage15.37) ===\n\n");

  int passed = 0, failed = 0;

  std::vector<TestCase> cases = {
    // 嚕寶實例："我是王小明很愛吃蛋糕"
    {"嚕寶範例-我是王小明很愛吃蛋糕(viewport大)",
     "我是王小明很愛吃蛋糕", 500, 1.0f, 22, false},
    {"嚕寶範例-我是王小明很愛吃蛋糕(viewport剛好)",
     "我是王小明很愛吃蛋糕", 220, 1.0f, 22, false},
    {"嚕寶範例-我是王小明很愛吃蛋糕(換頁邊界)",
     "我是王小明很愛吃蛋糕", 110, 1.0f, 22, false},

    // 字級行距變化
    {"小字、緊湊行距",
     "中文古典詩詞清明時節雨紛紛路上行人欲斷魂",
     400, 0.8f, 14, false},
    {"大字、寬鬆行距",
     "中文古典詩詞清明時節雨紛紛路上行人欲斷魂",
     800, 1.2f, 28, false},
    {"特大字 24pt",
     "中文古典詩詞清明時節雨紛紛路上行人欲斷魂",
     800, 1.0f, 50, false},

    // 中英混排
    {"中英混排-空白應跳過",
     "我喜歡 reading 書本 today 是好日子",
     400, 1.0f, 22, false},

    // 首行縮排
    {"首行縮排",
     "我是王小明很愛吃蛋糕", 500, 1.0f, 22, true},

    // 邊界情境
    {"剛好填滿一欄",
     "一二三四五六七八九十", 220, 1.0f, 22, false},
    {"剛好填滿兩欄",
     "一二三四五六七八九十甲乙丙丁戊己庚辛壬癸", 220, 1.0f, 22, false},

    // 多空白
    {"多個連續空白",
     "我  喜  歡  讀  書", 500, 1.0f, 22, false},

    // 標點符號
    {"中文標點",
     "你好，世界！這是測試。", 400, 1.0f, 22, false},

    // 長段落（粗香菜 20pt 情境）
    {"長段落-粗香菜20pt",
     "從前從前有一個小女孩她住在森林裡每天都要去打水井邊有一隻兔子總是來陪她聊天",
     800, 1.0f, 42, false},
  };

  for (const auto& tc : cases) {
    runTest(tc, passed, failed);
    printf("\n");
  }

  printf("\n=== 結果 ===\n");
  printf("PASSED: %d\n", passed);
  printf("FAILED: %d\n", failed);
  return failed > 0 ? 1 : 0;
}
