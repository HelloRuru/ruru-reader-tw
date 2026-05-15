#pragma once
#include <Epub.h>
#include <Epub/Section.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

#include "EpubReaderMenuActivity.h"
#include "activities/ActivityWithSubactivity.h"

#include "../lib/Epub/Epub/converters/PngToFramebufferConverter.h"

class EpubReaderActivity final : public ActivityWithSubactivity {
  enum class EPUBState {
      READING,
      SETTING,
      LEFT_MARGIN_SETTING,
      RIGHT_MARGIN_SETTING,
      TOP_MARGIN_SETTING, 
      BOTTOM_MARGIN_SETTING
  };


  std::shared_ptr<Epub> epub;
  std::unique_ptr<Section> section = nullptr;
  TaskHandle_t displayTaskHandle = nullptr;
  SemaphoreHandle_t renderingMutex = nullptr;
  int currentSpineIndex = 0;
  int nextPageNumber = 0;
  int pagesUntilFullRefresh = 0;
  int cachedSpineIndex = 0;
  int cachedChapterTotalPageCount = 0;
  // Signals that the next render should reposition within the newly loaded section
  // based on a cross-book percentage jump.
  bool pendingPercentJump = false;
  // Normalized 0.0-1.0 progress within the target spine item, computed from book percentage.
  float pendingSpineProgress = 0.0f;
  bool updateRequired = false;
  bool pendingSubactivityExit = false;  // Defer subactivity exit to avoid use-after-free
  bool pendingGoHome = false;           // Defer go home to avoid race condition with display task
  bool skipNextButtonCheck = false;     // Skip button processing for one frame after subactivity exit
  bool pendingMarginRelayout = false;   // Defer heavy section relayout until margin-setting is confirmed
  // stage15.30 (嚕寶要求: 切換字體後文本要重新讀取 EPDFONT + 重排版):
  //   每次 render 前比對「當前 fontId/lineCompression/wordSpacing」跟「section 建時用的」
  //   不一致就 section.reset() 重建 cache、確保字體切換後 layout 重算
  int lastRenderedFontId = -1;
  float lastRenderedLineCompression = 0.0f;
  uint8_t lastRenderedWordSpacing = 255;
  const std::function<void()> onGoBack;
  const std::function<void()> onGoHome;

  static void taskTrampoline(void* param);
  [[noreturn]] void displayTaskLoop();
  // stage15.21 (嚕寶要求全書預讀):
  //   打開書時、先把所有 spine 的 section cache 都建好、再進閱讀畫面
  //   有 cache 就跳過、沒 cache 才建、會顯示進度條
  //   大書（spine > 50）可能要 1-5 分鐘、但之後翻章節即時不卡
  void preScanAllChapters();
  void renderScreen();
  void renderContents(std::unique_ptr<Page> page, int orientedMarginTop, int orientedMarginRight,
                      int orientedMarginBottom, int orientedMarginLeft);
  // stage15.14: 撤回 stage15.12-13 補丁、改用 SAM 路徑（ChapterHtmlSlimParser 直接切直排頁）
  // collectVerticalTextFromPage 仍保留宣告（cpp 內仍有實作、無害）
  std::string collectVerticalTextFromPage(const class Page* page);
  void renderStatusBar(int orientedMarginRight, int orientedMarginBottom,int orientedMarginTop, int orientedMarginLeft) const;
  void saveProgress(int spineIndex, int currentPage, int pageCount);
  // Jump to a percentage of the book (0-100), mapping it to spine and page.
  void jumpToPercent(int percent);
  void onReaderMenuBack(uint8_t orientation);
  void onReaderMenuConfirm(EpubReaderMenuActivity::MenuAction action);
  void applyOrientation(uint8_t orientation);

  void renderPngSleepScreen(GfxRenderer& renderer) const;

  static EPUBState state;

 public:
  explicit EpubReaderActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Epub> epub,
                              const std::function<void()>& onGoBack, const std::function<void()>& onGoHome)
      : ActivityWithSubactivity("EpubReader", renderer, mappedInput),
        epub(std::move(epub)),
        onGoBack(onGoBack),
        onGoHome(onGoHome) {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
};
