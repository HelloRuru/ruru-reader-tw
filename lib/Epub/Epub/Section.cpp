#include "Section.h"

#include <SDCardManager.h>
#include <Serialization.h>
#include <ctype.h>

#include "Page.h"
#include "hyphenation/Hyphenator.h"
#include "parsers/ChapterHtmlSlimParser.h"

// =====================================================================
// stage11: HTML 鬆散標籤自閉合補正（XHTML void-element fixer）
// =====================================================================
// 為什麼需要：
//   EPUB 章節檔案常常使用「鬆散 HTML」語法（瀏覽器寬鬆解析），例如:
//       <br>            <img src="x.jpg">             <hr>
//   但本 reader 用 expat（嚴格 XML 解析器）來讀章節 HTML，
//   碰到沒自閉的 void element 會丟「mismatched tag」錯誤，
//   整個章節解析失敗 → 章節打不開 / 大書點目錄崩潰。
//
// 解法：parser 處理章節前，先 stream 過一次原檔，把
//   <br>           → <br/>
//   <img src="x">  → <img src="x"/>
//   <hr>           → <hr/>
// 等 void element 補上自閉斜線，然後丟給 expat 就能順利解析。
//
// 設計重點：
//   1. 純文字 → 一字節一字節寫出（在 tag 外維持原樣）
//   2. 進入 '<' → 切到 IN_TAG 狀態，記錄 tag 名稱
//   3. tag 名稱結束（遇到空白/'/'/'>'）→ 判斷是不是 void element
//   4. 遇到 '>' 收尾，若是 void element 且尚未 self-close → 補 '/'
//   5. 跳過 <! ... > 與 <? ... ?>（comment / DOCTYPE / PI / CDATA）
//   6. 雙重 buffer 一次 1024 byte，省 SD IO 次數（風眼用 512，我加倍）
// =====================================================================
namespace {

constexpr int kHtmlPreprocessBufBytes = 1024;
constexpr int kHtmlMaxTagNameLen = 32;

// HTML5 規範定義的 void element 清單（不能有結束標籤的元素）。
// ref: https://html.spec.whatwg.org/multipage/syntax.html#void-elements
const char* const kHtmlVoidElements[] = {
    "area", "base",  "br",     "col",    "embed", "hr",  "img",
    "input", "link", "meta",   "param",  "source", "track", "wbr"};
constexpr size_t kHtmlVoidElementCount = sizeof(kHtmlVoidElements) / sizeof(kHtmlVoidElements[0]);

bool isHtmlVoidElementName(const char* name, int len) {
  for (size_t i = 0; i < kHtmlVoidElementCount; ++i) {
    const char* candidate = kHtmlVoidElements[i];
    int candidateLen = static_cast<int>(strlen(candidate));
    if (candidateLen == len && strncasecmp(name, candidate, len) == 0) {
      return true;
    }
  }
  return false;
}

// 預處理 HTML 檔案：把 src 路徑的檔案讀進來，補完 void element 自閉斜線後寫到 dst。
// 失敗時 dst 內容可能不完整（呼叫端應檢查回傳值並清掉殘留檔）。
bool selfCloseVoidElementsInHtmlFile(const std::string& src, const std::string& dst) {
  FsFile inFile, outFile;
  if (!SdMan.openFileForRead("SCT", src, inFile)) {
    return false;
  }
  if (!SdMan.openFileForWrite("SCT", dst, outFile)) {
    inFile.close();
    return false;
  }

  uint8_t inBuf[kHtmlPreprocessBufBytes];
  uint8_t outBuf[kHtmlPreprocessBufBytes];
  int outLen = 0;

  // 解析狀態
  enum class ScanState { OutsideTag, InsideTag };
  ScanState scanState = ScanState::OutsideTag;

  char tagName[kHtmlMaxTagNameLen] = {};
  int tagNameLen = 0;
  bool collectingTagName = false;
  bool isClosingTag = false;        // </xxx>
  bool currentTagIsVoid = false;
  bool inDoctypeOrComment = false;  // <! ... >  或  <? ... ?>
  char prevChar = 0;
  bool ok = true;

  // 寫一個 byte 到 outBuf；滿了就 flush
  auto emitByte = [&](uint8_t b) -> bool {
    outBuf[outLen++] = b;
    if (outLen >= kHtmlPreprocessBufBytes) {
      if (outFile.write(outBuf, kHtmlPreprocessBufBytes) != kHtmlPreprocessBufBytes) return false;
      outLen = 0;
    }
    return true;
  };

  // tag 名稱結束的共用邏輯（不是 closing tag 才需要判斷 void）
  auto finalizeTagName = [&]() {
    if (collectingTagName && tagNameLen > 0) {
      tagName[tagNameLen] = '\0';
      if (!isClosingTag) {
        currentTagIsVoid = isHtmlVoidElementName(tagName, tagNameLen);
      }
      collectingTagName = false;
    }
  };

  while (ok) {
    const int readLen = inFile.read(inBuf, kHtmlPreprocessBufBytes);
    if (readLen <= 0) break;

    for (int i = 0; i < readLen && ok; ++i) {
      const char ch = static_cast<char>(inBuf[i]);

      if (scanState == ScanState::OutsideTag) {
        ok = emitByte(static_cast<uint8_t>(ch));
        if (ch == '<') {
          // 進入 tag，初始化所有狀態
          scanState = ScanState::InsideTag;
          tagNameLen = 0;
          collectingTagName = true;
          isClosingTag = false;
          currentTagIsVoid = false;
          inDoctypeOrComment = false;
          prevChar = '<';
        }
        continue;
      }

      // scanState == InsideTag
      if (ch == '>') {
        finalizeTagName();
        // 補 self-close：是 void、不是 doctype/註解、結尾不是 '/'
        if (!inDoctypeOrComment && currentTagIsVoid && prevChar != '/') {
          ok = emitByte('/');
        }
        if (ok) ok = emitByte('>');
        scanState = ScanState::OutsideTag;
        continue;
      }

      // 還在 tag 內
      ok = emitByte(static_cast<uint8_t>(ch));
      if (!ok) break;

      if (collectingTagName) {
        if (tagNameLen == 0 && ch == '/') {
          // </xxx>
          isClosingTag = true;
        } else if (tagNameLen == 0 && (ch == '!' || ch == '?')) {
          // <! ... > or <? ... ?> → 不要動內容
          inDoctypeOrComment = true;
          collectingTagName = false;
        } else if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
          finalizeTagName();
        } else if (ch == '/') {
          // 已經寫出 self-close 標記如 <br/> 中間的 '/'
          finalizeTagName();
        } else {
          if (tagNameLen < kHtmlMaxTagNameLen - 1) {
            tagName[tagNameLen++] = static_cast<char>(tolower(ch));
          }
        }
      }
      prevChar = ch;
    }
  }

  // flush 剩餘 buffer
  if (ok && outLen > 0) {
    ok = (outFile.write(outBuf, outLen) == outLen);
  }

  inFile.close();
  outFile.close();
  return ok;
}

}  // namespace (HTML void-element preprocessor)

namespace {
constexpr uint8_t SECTION_FILE_VERSION = 13;
constexpr uint32_t HEADER_SIZE = sizeof(uint8_t) + sizeof(int) + sizeof(float) + sizeof(bool) + sizeof(uint8_t) +
                                 sizeof(uint16_t) + sizeof(uint16_t) + sizeof(uint16_t) + sizeof(bool) +
                                 sizeof(uint8_t)+sizeof(bool)+ sizeof(bool) +
                                 sizeof(uint32_t);
}  // namespace

uint32_t Section::onPageComplete(std::unique_ptr<Page> page) {
  if (!file) {
    Serial.printf("[%lu] [SCT] File not open for writing page %d\n", millis(), pageCount);
    return 0;
  }

  const uint32_t position = file.position();
  if (!page->serialize(file)) {
    Serial.printf("[%lu] [SCT] Failed to serialize page %d\n", millis(), pageCount);
    return 0;
  }
  Serial.printf("[%lu] [SCT] Page %d processed\n", millis(), pageCount);

  pageCount++;
  return position;
}

void Section::writeSectionFileHeader(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                     const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                     const uint16_t viewportHeight, const bool hyphenationEnabled,const uint8_t wordSpacing,
                                     const bool firstlineintented,
                                     const bool embeddedStyle) {
  if (!file) {
    Serial.printf("[%lu] [SCT] File not open for writing header\n", millis());
    return;
  }
  static_assert(HEADER_SIZE == sizeof(SECTION_FILE_VERSION) + sizeof(fontId) + sizeof(lineCompression) +
                                   sizeof(extraParagraphSpacing) + sizeof(paragraphAlignment) + sizeof(viewportWidth) +
                                   sizeof(viewportHeight) + sizeof(pageCount) + sizeof(hyphenationEnabled) +
                                   sizeof(firstlineintented) +sizeof(wordSpacing)+
                                   sizeof(embeddedStyle) + sizeof(uint32_t),
                "Header size mismatch");
  serialization::writePod(file, SECTION_FILE_VERSION);
  serialization::writePod(file, fontId);
  serialization::writePod(file, lineCompression);
  serialization::writePod(file, extraParagraphSpacing);
  serialization::writePod(file, paragraphAlignment);
  serialization::writePod(file, viewportWidth);
  serialization::writePod(file, viewportHeight);
  serialization::writePod(file, hyphenationEnabled);
  serialization::writePod(file, firstlineintented);
  serialization::writePod(file, wordSpacing);
  serialization::writePod(file, embeddedStyle);
  serialization::writePod(file, pageCount);  // Placeholder for page count (will be initially 0 when written)
  serialization::writePod(file, static_cast<uint32_t>(0));  // Placeholder for LUT offset
}

bool Section::loadSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                              const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                              const uint16_t viewportHeight, const bool hyphenationEnabled,const uint8_t wordSpacing
                              , const bool firstlineintented, const bool embeddedStyle) {
  if (!SdMan.openFileForRead("SCT", filePath, file)) {
    return false;
  }

  // Match parameters
  {
    uint8_t version;
    serialization::readPod(file, version);
    if (version != SECTION_FILE_VERSION) {
      file.close();
      Serial.printf("[%lu] [SCT] Deserialization failed: Unknown version %u\n", millis(), version);
      clearCache();
      return false;
    }

    int fileFontId;
    uint16_t fileViewportWidth, fileViewportHeight;
    float fileLineCompression;
    bool fileExtraParagraphSpacing;
    uint8_t fileParagraphAlignment;
    bool fileHyphenationEnabled;
    bool fileFirstlineintented;
    uint8_t fileWordSpacing;
    bool fileEmbeddedStyle;
    serialization::readPod(file, fileFontId);
    serialization::readPod(file, fileLineCompression);
    serialization::readPod(file, fileExtraParagraphSpacing);
    serialization::readPod(file, fileParagraphAlignment);
    serialization::readPod(file, fileViewportWidth);
    serialization::readPod(file, fileViewportHeight);
    serialization::readPod(file, fileHyphenationEnabled);
    serialization::readPod(file, fileFirstlineintented);
    serialization::readPod(file, fileWordSpacing);
    serialization::readPod(file, fileEmbeddedStyle);

    if (fontId != fileFontId || lineCompression != fileLineCompression ||
        extraParagraphSpacing != fileExtraParagraphSpacing || paragraphAlignment != fileParagraphAlignment ||
        viewportWidth != fileViewportWidth || viewportHeight != fileViewportHeight ||
        hyphenationEnabled != fileHyphenationEnabled || wordSpacing != fileWordSpacing||
        firstlineintented != fileFirstlineintented|| embeddedStyle != fileEmbeddedStyle) {
      file.close();
      Serial.printf("[%lu] [SCT] Deserialization failed: Parameters do not match\n", millis());
      clearCache();
      return false;
    }
  }

  serialization::readPod(file, pageCount);
  file.close();
  Serial.printf("[%lu] [SCT] Deserialization succeeded: %d pages\n", millis(), pageCount);
  return true;
}

// Your updated class method (assuming you are using the 'SD' object, which is a wrapper for a specific filesystem)
bool Section::clearCache() const {
  if (!SdMan.exists(filePath.c_str())) {
    Serial.printf("[%lu] [SCT] Cache does not exist, no action needed\n", millis());
    return true;
  }

  if (!SdMan.remove(filePath.c_str())) {
    Serial.printf("[%lu] [SCT] Failed to clear cache\n", millis());
    return false;
  }

  Serial.printf("[%lu] [SCT] Cache cleared successfully\n", millis());
  return true;
}

bool Section::createSectionFile(const int fontId, const float lineCompression, const bool extraParagraphSpacing,
                                const uint8_t paragraphAlignment, const uint16_t viewportWidth,
                                const uint16_t viewportHeight, const bool hyphenationEnabled,const uint8_t wordSpacing,
                                const bool firstlineintented, const bool embeddedStyle,
                                const std::function<void()>& popupFn) {
  const auto localPath = epub->getSpineItem(spineIndex).href;
  const auto tmpHtmlPath = epub->getCachePath() + "/.tmp_" + std::to_string(spineIndex) + ".html";

  // Create cache directory if it doesn't exist
  {
    const auto sectionsDir = epub->getCachePath() + "/sections";
    SdMan.mkdir(sectionsDir.c_str());
  }

  // Retry logic for SD card timing issues
  bool success = false;
  uint32_t fileSize = 0;
  for (int attempt = 0; attempt < 3 && !success; attempt++) {
    if (attempt > 0) {
      Serial.printf("[%lu] [SCT] Retrying stream (attempt %d)...\n", millis(), attempt + 1);
      delay(50);  // Brief delay before retry
    }

    // Remove any incomplete file from previous attempt before retrying
    if (SdMan.exists(tmpHtmlPath.c_str())) {
      SdMan.remove(tmpHtmlPath.c_str());
    }

    FsFile tmpHtml;
    if (!SdMan.openFileForWrite("SCT", tmpHtmlPath, tmpHtml)) {
      continue;
    }
    success = epub->readItemContentsToStream(localPath, tmpHtml, 1024);
    fileSize = tmpHtml.size();
    tmpHtml.close();

    // If streaming failed, remove the incomplete file immediately
    if (!success && SdMan.exists(tmpHtmlPath.c_str())) {
      SdMan.remove(tmpHtmlPath.c_str());
      Serial.printf("[%lu] [SCT] Removed incomplete temp file after failed attempt\n", millis());
    }
  }

  if (!success) {
    Serial.printf("[%lu] [SCT] Failed to stream item contents to temp file after retries\n", millis());
    return false;
  }

  Serial.printf("[%lu] [SCT] Streamed temp HTML to %s (%d bytes)\n", millis(), tmpHtmlPath.c_str(), fileSize);

  // stage11: HTML 鬆散標籤自閉合補正
  // 章節 HTML 常含未自閉的 <br>、<img>、<hr>，expat 嚴格 XML 會 reject 整個檔，
  // 導致大書某些章節打不開（特別是網文目錄崩潰的根因）。先把 tmpHtml 過一遍補上自閉斜線，
  // 再餵給 parser，相容鬆散 HTML 同時保留嚴格 XML 解析速度。
  const auto tmpHtmlPrePath = epub->getCachePath() + "/.tmp_pre_" + std::to_string(spineIndex) + ".html";
  if (SdMan.exists(tmpHtmlPrePath.c_str())) {
    SdMan.remove(tmpHtmlPrePath.c_str());
  }
  const uint32_t prepStart = millis();
  bool prepOk = selfCloseVoidElementsInHtmlFile(tmpHtmlPath, tmpHtmlPrePath);
  const std::string& parserInputPath = prepOk ? tmpHtmlPrePath : tmpHtmlPath;
  if (prepOk) {
    Serial.printf("[%lu] [SCT] Self-closed void elements in %lu ms (using preprocessed file)\n",
                  millis(), millis() - prepStart);
  } else {
    Serial.printf("[%lu] [SCT] Preprocess failed, falling back to raw HTML\n", millis());
    if (SdMan.exists(tmpHtmlPrePath.c_str())) {
      SdMan.remove(tmpHtmlPrePath.c_str());
    }
  }

  if (!SdMan.openFileForWrite("SCT", filePath, file)) {
    if (prepOk) SdMan.remove(tmpHtmlPrePath.c_str());
    return false;
  }
  writeSectionFileHeader(fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
                         viewportHeight, hyphenationEnabled,wordSpacing,firstlineintented, embeddedStyle);
  std::vector<uint32_t> lut = {};
  // Derive the content base directory and image cache path prefix for the parser
  size_t lastSlash = localPath.find_last_of('/');
  std::string contentBase = (lastSlash != std::string::npos) ? localPath.substr(0, lastSlash + 1) : "";
  std::string imageBasePath = epub->getCachePath() + "/img_" + std::to_string(spineIndex) + "_";



  ChapterHtmlSlimParser visitor(
      epub,parserInputPath, renderer, fontId, lineCompression, extraParagraphSpacing, paragraphAlignment, viewportWidth,
      viewportHeight, hyphenationEnabled,wordSpacing,firstlineintented,
      [this, &lut](std::unique_ptr<Page> page) { lut.emplace_back(this->onPageComplete(std::move(page))); },
      embeddedStyle,contentBase, imageBasePath,  popupFn, embeddedStyle ? epub->getCssParser() : nullptr);
  Hyphenator::setPreferredLanguage(epub->getLanguage());
  success = visitor.parseAndBuildPages();

  // stage11: 清理兩個 tmp（原始 + 預處理）
  SdMan.remove(tmpHtmlPath.c_str());
  if (prepOk) {
    SdMan.remove(tmpHtmlPrePath.c_str());
  }
  if (!success) {
    Serial.printf("[%lu] [SCT] Failed to parse XML and build pages\n", millis());
    file.close();
    SdMan.remove(filePath.c_str());
    return false;
  }

  const uint32_t lutOffset = file.position();
  bool hasFailedLutRecords = false;
  // Write LUT
  for (const uint32_t& pos : lut) {
    if (pos == 0) {
      hasFailedLutRecords = true;
      break;
    }
    serialization::writePod(file, pos);
  }

  if (hasFailedLutRecords) {
    Serial.printf("[%lu] [SCT] Failed to write LUT due to invalid page positions\n", millis());
    file.close();
    SdMan.remove(filePath.c_str());
    return false;
  }

  // Go back and write LUT offset
  file.seek(HEADER_SIZE - sizeof(uint32_t) - sizeof(pageCount));
  serialization::writePod(file, pageCount);
  serialization::writePod(file, lutOffset);
  file.close();
  return true;
}

std::unique_ptr<Page> Section::loadPageFromSectionFile() {
  if (!SdMan.openFileForRead("SCT", filePath, file)) {
    return nullptr;
  }

  file.seek(HEADER_SIZE - sizeof(uint32_t));
  uint32_t lutOffset;
  serialization::readPod(file, lutOffset);
  file.seek(lutOffset + sizeof(uint32_t) * currentPage);
  uint32_t pagePos;
  serialization::readPod(file, pagePos);
  file.seek(pagePos);

  auto page = Page::deserialize(file);
  file.close();
  return page;
}
