#include "HomeActivity.h"

#include <Bitmap.h>
#include <Epub.h>
#include <GfxRenderer.h>
#include <SDCardManager.h>
#include <Utf8.h>
#include <Xtc.h>

#include <cstring>
#include <vector>

#include "Battery.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/StringUtils.h"
//清理字体内存
#include "CustomEpdFont.h"

void HomeActivity::taskTrampoline(void* param) {
  auto* self = static_cast<HomeActivity*>(param);
  self->displayTaskLoop();
}

int HomeActivity::getMenuItemCount() const {
  int count = 4;  // My Library, Recents, File transfer, Settings
  if (!recentBooks.empty()) {
    count += recentBooks.size();
  }
  if (hasOpdsUrl) {
    count++;
  }
  if (hasjianguoUrl) count++;
  return count;

}


void HomeActivity::loadRecentBooks(int maxBooks) {
  recentBooks.clear();
  const auto& books = RECENT_BOOKS.getBooks();
  recentBooks.reserve(std::min(static_cast<int>(books.size()), maxBooks));

  for (const RecentBook& book : books) {
    // Limit to maximum number of recent books
    if (recentBooks.size() >= maxBooks) {
      break;
    }

    // Skip if file no longer exists
    if (!SdMan.exists(book.path.c_str())) {
      continue;
    }

    recentBooks.push_back(book);
  }
}

void HomeActivity::loadRecentCovers(int coverHeight) {
  recentsLoading = true;
  bool showingLoading = false;
  Rect popupRect;

  // stage15.25 (嚕寶要求：書本不用點進去也要先抓封面)
  //   原本 if (!coverBmpPath.empty()) 才抓 → 沒點進去過的書永遠拿不到 coverBmpPath
  //   現在改：不管 coverBmpPath 有沒有、只要 cache 不存在就主動 load epub 拿 metadata + 生 thumb
  int progress = 0;
  for (RecentBook& book : recentBooks) {
    // 先看「已知 coverBmpPath」對應的 cache 在不在
    bool needBuildThumb = true;
    if (!book.coverBmpPath.empty()) {
      std::string coverPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      if (SdMan.exists(coverPath.c_str())) {
        needBuildThumb = false;  // 已有 cache、不用建
      }
    }

    if (!needBuildThumb) {
      progress++;
      continue;
    }

    // 進來表示：要嘛 coverBmpPath 空（沒點進去過）、要嘛 cache 不在
    // 兩種狀況都要 load epub/xtc 拿 metadata + 生 thumb
    if (StringUtils::checkFileExtension(book.path, ".epub")) {
      Epub epub(book.path, "/.crosspoint");
      epub.load(false, true);  // Skip CSS、只要 metadata

      if (!showingLoading) {
        showingLoading = true;
        popupRect = GUI.drawPopup(renderer, "Loading...");
      }
      GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
      bool success = epub.generateThumbBmp(coverHeight);
      if (success) {
        // 拿到 thumb 後、回寫 metadata 到 RECENT_BOOKS（含 coverBmpPath、title、author）
        const std::string thumbPath = epub.getThumbBmpPath();
        const std::string title = epub.getTitle();
        const std::string author = epub.getAuthor();
        RECENT_BOOKS.updateBook(book.path, title, author, thumbPath);
        book.coverBmpPath = thumbPath;
        if (!book.title.empty() && title.empty()) {
          // 保留既有 title（避免 metadata 為空時清空）
        } else if (!title.empty()) {
          book.title = title;
          book.author = author;
        }
      } else {
        RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
        book.coverBmpPath = "";
      }
      coverRendered = false;
      updateRequired = true;
    } else if (StringUtils::checkFileExtension(book.path, ".xtch") ||
               StringUtils::checkFileExtension(book.path, ".xtc")) {
      Xtc xtc(book.path, "/.crosspoint");
      if (xtc.load()) {
        if (!showingLoading) {
          showingLoading = true;
          popupRect = GUI.drawPopup(renderer, "Loading...");
        }
        GUI.fillPopupProgress(renderer, popupRect, 10 + progress * (90 / recentBooks.size()));
        bool success = xtc.generateThumbBmp(coverHeight);
        if (success) {
          const std::string thumbPath = xtc.getThumbBmpPath();
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, thumbPath);
          book.coverBmpPath = thumbPath;
        } else {
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, "");
          book.coverBmpPath = "";
        }
        coverRendered = false;
        updateRequired = true;
      }
    }
    progress++;
  }

  recentsLoaded = true;
  recentsLoading = false;
}

void HomeActivity::onEnter() {
  Activity::onEnter();





  renderingMutex = xSemaphoreCreateMutex();
  // Check if OPDS browser URL is configured
  hasOpdsUrl = strlen(SETTINGS.opdsServerUrl) > 0;
  hasjianguoUrl = strlen(SETTINGS.jgUsername) > 0;

  selectorIndex = 0;

  auto metrics = UITheme::getInstance().getMetrics();
  loadRecentBooks(metrics.homeRecentBooksCount);

  // stage15.6: 封面預載 — 在 onEnter 階段就把 cover thumb 載 RAM
  //            原本要等第一次 render 完才開始載、會看到「先空白、後跳出圖」的閃
  //            預載後第一次 render 就直接畫出來
  if (!recentBooks.empty()) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }

  // Trigger first update
  updateRequired = true;

  xTaskCreate(&HomeActivity::taskTrampoline, "HomeActivityTask",
              8192,               // Stack size
              this,               // Parameters
              1,                  // Priority
              &displayTaskHandle  // Task handle
  );
}

void HomeActivity::onExit() {
  Activity::onExit();

  // Wait until not rendering to delete task to avoid killing mid-instruction to EPD
  xSemaphoreTake(renderingMutex, portMAX_DELAY);
  if (displayTaskHandle) {
    vTaskDelete(displayTaskHandle);
    displayTaskHandle = nullptr;
  }
  vSemaphoreDelete(renderingMutex);
  renderingMutex = nullptr;

  // Free the stored cover buffer if any
  freeCoverBuffer();
}

bool HomeActivity::storeCoverBuffer() {
  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  // Free any existing buffer first
  freeCoverBuffer();

  const size_t bufferSize = GfxRenderer::getBufferSize();
  coverBuffer = static_cast<uint8_t*>(malloc(bufferSize));
  if (!coverBuffer) {
    return false;
  }

  memcpy(coverBuffer, frameBuffer, bufferSize);
  return true;
}

bool HomeActivity::restoreCoverBuffer() {
  if (!coverBuffer) {
    return false;
  }

  uint8_t* frameBuffer = renderer.getFrameBuffer();
  if (!frameBuffer) {
    return false;
  }

  const size_t bufferSize = GfxRenderer::getBufferSize();
  memcpy(frameBuffer, coverBuffer, bufferSize);
  return true;
}

void HomeActivity::freeCoverBuffer() {
  if (coverBuffer) {
    free(coverBuffer);
    coverBuffer = nullptr;
  }
  coverBufferStored = false;
}

void HomeActivity::loop() {
  const bool prevPressed = mappedInput.wasPressed(MappedInputManager::Button::Up) ||
                           mappedInput.wasPressed(MappedInputManager::Button::Left);
  const bool nextPressed = mappedInput.wasPressed(MappedInputManager::Button::Down) ||
                           mappedInput.wasPressed(MappedInputManager::Button::Right);

  const int menuCount = getMenuItemCount();

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    // Calculate dynamic indices based on which options are available
    int idx = 0;
    int menuSelectedIndex = selectorIndex - static_cast<int>(recentBooks.size());
    const int myLibraryIdx = idx++;
    const int recentsIdx = idx++;
    const int opdsLibraryIdx = hasOpdsUrl ? idx++ : -1;
    const int jgLibraryIdx = hasjianguoUrl ? idx++ : -1;
    const int fileTransferIdx = idx++;
    const int settingsIdx = idx;

    if (selectorIndex < recentBooks.size()) {
      onSelectBook(recentBooks[selectorIndex].path);
    } else if (menuSelectedIndex == myLibraryIdx) {
      onMyLibraryOpen();
    } else if (menuSelectedIndex == recentsIdx) {
      onRecentsOpen();
    } else if (menuSelectedIndex == opdsLibraryIdx) {
      onOpdsBrowserOpen();
    } else if (menuSelectedIndex == jgLibraryIdx) {
      onJianGuoYunOpen();
    } else if (menuSelectedIndex == fileTransferIdx) {
      onFileTransferOpen();
    } else if (menuSelectedIndex == settingsIdx) {
      onSettingsOpen();
    }
  } else if (prevPressed) {
    selectorIndex = (selectorIndex + menuCount - 1) % menuCount;
    updateRequired = true;
  } else if (nextPressed) {
    selectorIndex = (selectorIndex + 1) % menuCount;
    updateRequired = true;
  }
}

void HomeActivity::displayTaskLoop() {
  while (true) {
    if (updateRequired) {
      updateRequired = false;
      xSemaphoreTake(renderingMutex, portMAX_DELAY);
      render();
      xSemaphoreGive(renderingMutex);
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

void HomeActivity::render() {
  auto metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  bool bufferRestored = coverBufferStored && restoreCoverBuffer();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.homeTopPadding}, nullptr);

  GUI.drawRecentBookCover(renderer, Rect{0, metrics.homeTopPadding, pageWidth, metrics.homeCoverTileHeight},
                          recentBooks, selectorIndex, coverRendered, coverBufferStored, bufferRestored,
                          std::bind(&HomeActivity::storeCoverBuffer, this));

  // Build menu items dynamically (stage15.8: 加 menuIcons 對應、給選單畫 icon)
std::vector<const char*> menuItems = {"挑選一本書", "最近閱讀"};
std::vector<UIIcon> menuIcons = {UIIcon::Folder, UIIcon::Recent};

if (hasOpdsUrl) {
    menuItems.push_back("OPDS 瀏覽器");
    menuIcons.push_back(UIIcon::Library);
}
if (hasjianguoUrl) {
    menuItems.push_back("堅果雲");
    menuIcons.push_back(UIIcon::Library);
}


menuItems.push_back("wifi功能");
menuIcons.push_back(UIIcon::Wifi);
menuItems.push_back("設定");
menuIcons.push_back(UIIcon::Settings);

  // stage15.25 (修正螢幕尺寸誤判 + 嚕寶要 menu 置底):
  //   螢幕實際 800px、menu 緊接 cover tile 下方、貼螢幕真實底邊
  //   menu 高度 = 螢幕剩餘空間 - 底部 10px 邊距
  const int menuRectY = metrics.homeTopPadding + metrics.homeCoverTileHeight + 20;  // 緊接 cover 區、留 20px 呼吸
  const int menuRectH = pageHeight - menuRectY - 10;
  GUI.drawButtonMenu(
      renderer,
      Rect{0, menuRectY, pageWidth, menuRectH},
      static_cast<int>(menuItems.size()), selectorIndex - recentBooks.size(),
      [&menuItems](int index) { return std::string(menuItems[index]); },
      [&menuIcons](int index) { return menuIcons[index]; });

  // stage15.20: 嚕寶不要底下「選擇 向上 向下」那排提示、首頁直接拿掉
  // const auto labels = mappedInput.mapLabels("", "選擇", "向上", "向下");
  // GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();

  if (!firstRenderDone) {
    firstRenderDone = true;
    updateRequired = true;
  } else if (!recentsLoaded && !recentsLoading) {
    recentsLoading = true;
    loadRecentCovers(metrics.homeCoverHeight);
  }
}
