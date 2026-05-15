#include "LyraTheme.h"

#include <GfxRenderer.h>
#include <SDCardManager.h>

#include <cstdint>
#include <string>

#include "Battery.h"
#include "RecentBooksStore.h"
#include "components/UITheme.h"
#include "components/icons/cover.h"  // stage15.28: CoverIcon placeholder
#include "fontIds.h"
#include "util/StringUtils.h"

// Internal constants
namespace {
constexpr int batteryPercentSpacing = 4;
constexpr int hPaddingInSelection = 8;
constexpr int cornerRadius = 6;
constexpr int topHintButtonY = 345;
}  // namespace

void LyraTheme::drawBattery(const GfxRenderer& renderer, Rect rect, const bool showPercentage) const {
  // Left aligned battery icon and percentage
  const uint16_t percentage = battery.readPercentage();
  if (showPercentage) {
    const auto percentageText = std::to_string(percentage) + "%";
    renderer.drawText(SMALL_FONT_ID, rect.x + batteryPercentSpacing + LyraMetrics::values.batteryWidth, rect.y,
                      percentageText.c_str());
  }
  // 1 column on left, 2 columns on right, 5 columns of battery body
  const int x = rect.x;
  const int y = rect.y + 6;
  const int battWidth = LyraMetrics::values.batteryWidth;

  // Top line
  renderer.drawLine(x + 1, y, x + battWidth - 3, y);
  // Bottom line
  renderer.drawLine(x + 1, y + rect.height - 1, x + battWidth - 3, y + rect.height - 1);
  // Left line
  renderer.drawLine(x, y + 1, x, y + rect.height - 2);
  // Battery end
  renderer.drawLine(x + battWidth - 2, y + 1, x + battWidth - 2, y + rect.height - 2);
  renderer.drawPixel(x + battWidth - 1, y + 3);
  renderer.drawPixel(x + battWidth - 1, y + rect.height - 4);
  renderer.drawLine(x + battWidth - 0, y + 4, x + battWidth - 0, y + rect.height - 5);

  // Draw bars
  if (percentage > 10) {
    renderer.fillRect(x + 2, y + 2, 3, rect.height - 4);
  }
  if (percentage > 40) {
    renderer.fillRect(x + 6, y + 2, 3, rect.height - 4);
  }
  if (percentage > 70) {
    renderer.fillRect(x + 10, y + 2, 3, rect.height - 4);
  }
}

void LyraTheme::drawHeader(const GfxRenderer& renderer, Rect rect, const char* title,
                           const char* /*subtitle*/) const {
  renderer.fillRect(rect.x, rect.y, rect.width, rect.height, false);

  const bool showBatteryPercentage =
      SETTINGS.hideBatteryPercentage != CrossPointSettings::HIDE_BATTERY_PERCENTAGE::HIDE_ALWAYS;
  int batteryX = rect.x + rect.width - LyraMetrics::values.contentSidePadding - LyraMetrics::values.batteryWidth;
  if (showBatteryPercentage) {
    const uint16_t percentage = battery.readPercentage();
    const auto percentageText = std::to_string(percentage) + "%";
    batteryX -= renderer.getTextWidth(SMALL_FONT_ID, percentageText.c_str());
  }
  drawBattery(renderer,
              Rect{batteryX, rect.y + 10, LyraMetrics::values.batteryWidth, LyraMetrics::values.batteryHeight},
              showBatteryPercentage);

  if (title) {
    auto truncatedTitle = renderer.truncatedText(
        UI_12_FONT_ID, title, rect.width - LyraMetrics::values.contentSidePadding * 2, EpdFontFamily::BOLD);
    renderer.drawText(UI_12_FONT_ID, rect.x + LyraMetrics::values.contentSidePadding,
                      rect.y + LyraMetrics::values.batteryBarHeight + 3, truncatedTitle.c_str(), true,
                      EpdFontFamily::BOLD);
    // stage15.20 (借書卡感)：底部從單一粗線改成雙線（上 1px 細線 + 下 2px 粗線）
    //   類似登記簿 header 的雙線分隔、跟首頁 LIBRARY CARD 雙圈呼應
    renderer.drawLine(rect.x, rect.y + rect.height - 5, rect.x + rect.width, rect.y + rect.height - 5, 1, true);
    renderer.drawLine(rect.x, rect.y + rect.height - 2, rect.x + rect.width, rect.y + rect.height - 2, 2, true);
  }
}

void LyraTheme::drawTabBar(const GfxRenderer& renderer, Rect rect, const std::vector<TabInfo>& tabs,
                           bool selected) const {
  // stage15.20 (米蘭達方案 A 借書卡分類標籤):
  //   拿掉整條 dither 灰底、拿掉選中黑底反白塊
  //   改成「選中那個 tab 字變粗 + 下方 3px 黑線標記」、像書架掛的分類標籤
  //   底下用 1+2px 雙線、跟 drawHeader 呼應
  int currentX = rect.x + LyraMetrics::values.contentSidePadding;

  for (const auto& tab : tabs) {
    const EpdFontFamily::Style tabStyle = tab.selected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR;
    const int textWidth = renderer.getTextWidth(UI_10_FONT_ID, tab.label, tabStyle);

    renderer.drawText(UI_10_FONT_ID, currentX + hPaddingInSelection, rect.y + 6, tab.label, true, tabStyle);

    // 選中那個 tab：下方畫 3px 粗短線標記、僅當「整個 tabBar 是 focused」狀態才畫
    if (tab.selected && selected) {
      const int markY = rect.y + rect.height - 6;
      renderer.drawLine(currentX + hPaddingInSelection, markY,
                        currentX + hPaddingInSelection + textWidth, markY, 3, true);
    } else if (tab.selected) {
      // tabBar 未 focused 時、選中 tab 用 1px 細線（淡化）
      const int markY = rect.y + rect.height - 6;
      renderer.drawLine(currentX + hPaddingInSelection, markY,
                        currentX + hPaddingInSelection + textWidth, markY, 1, true);
    }

    currentX += textWidth + LyraMetrics::values.tabSpacing + 2 * hPaddingInSelection;
  }

  // 底部雙線分隔（跟 drawHeader 呼應）
  renderer.drawLine(rect.x, rect.y + rect.height - 3, rect.x + rect.width, rect.y + rect.height - 3, 1, true);
  renderer.drawLine(rect.x, rect.y + rect.height - 1, rect.x + rect.width, rect.y + rect.height - 1, 1, true);
}

void LyraTheme::drawList(const GfxRenderer& renderer, Rect rect, int itemCount, int selectedIndex,
                         const std::function<std::string(int index)>& rowTitle,
                         const std::function<std::string(int index)>& rowSubtitle,
                         const std::function<UIIcon(int index)>& /*rowIcon*/,
                         const std::function<std::string(int index)>& rowValue, bool /*highlightValue*/,
                         const std::function<bool(int index)>& /*isHeader*/) const {
  int rowHeight =
      (rowSubtitle != nullptr) ? LyraMetrics::values.listWithSubtitleRowHeight : LyraMetrics::values.listRowHeight;
  int pageItems = rect.height / rowHeight;

  const int totalPages = (itemCount + pageItems - 1) / pageItems;
  if (totalPages > 1) {
    const int scrollAreaHeight = rect.height;

    // Draw scroll bar
    const int scrollBarHeight = (scrollAreaHeight * pageItems) / itemCount;
    const int currentPage = selectedIndex / pageItems;
    const int scrollBarY = rect.y + ((scrollAreaHeight - scrollBarHeight) * currentPage) / (totalPages - 1);
    const int scrollBarX = rect.x + rect.width - LyraMetrics::values.scrollBarRightOffset;
    renderer.drawLine(scrollBarX, rect.y, scrollBarX, rect.y + scrollAreaHeight, true);
    renderer.fillRect(scrollBarX - LyraMetrics::values.scrollBarWidth, scrollBarY, LyraMetrics::values.scrollBarWidth,
                      scrollBarHeight, true);
  }

  // Draw selection
  int contentWidth =
      rect.width -
      (totalPages > 1 ? (LyraMetrics::values.scrollBarWidth + LyraMetrics::values.scrollBarRightOffset) : 1);
  // stage15.20 (米蘭達方案 A 借書卡): 拿掉整列灰底、選中那列改畫「‣」項目符號 + 左側 2px 黑短線
  //   原本整列灰底（fillRoundedRect Color::LightGray）= AI app 風、跟借書登記簿感衝突
  //   現在只在文字左側畫實心三角符號表示「選中這一項」、保留輕鬆閱讀感

  // Draw all items
  const auto pageStartIndex = selectedIndex / pageItems * pageItems;
  for (int i = pageStartIndex; i < itemCount && i < pageStartIndex + pageItems; i++) {
    const int itemY = rect.y + (i % pageItems) * rowHeight;
    const bool isSelected = (i == selectedIndex);

    // 借書登記簿格線：每列上緣畫 dotted line（每 8px 一點）
    // 放在 itemY-1 = 上一列底邊、避開文字、視覺上是「列與列之間」的分隔
    if (i > pageStartIndex) {
      const int dotY = itemY - 1;
      const int dotStartX = rect.x + LyraMetrics::values.contentSidePadding;
      const int dotEndX = rect.x + contentWidth - LyraMetrics::values.contentSidePadding;
      for (int dx = dotStartX; dx < dotEndX; dx += 8) {
        renderer.drawPixel(dx, dotY);
        renderer.drawPixel(dx + 1, dotY);
      }
    }

    // 選中：左側畫「‣」實心三角（手寫登記感）+ 左邊 3px 粗短直線
    if (isSelected) {
      const int markX = rect.x + LyraMetrics::values.contentSidePadding;
      const int markY = itemY + rowHeight / 2;
      // 實心三角：3 個 fillRect 模擬
      renderer.fillRect(markX, markY - 4, 2, 8, true);
      renderer.fillRect(markX + 2, markY - 3, 2, 6, true);
      renderer.fillRect(markX + 4, markY - 2, 2, 4, true);
      renderer.fillRect(markX + 6, markY - 1, 2, 2, true);
    }

    // Draw name
    // stage15.20: textX 左推 12 留給「‣」標記、textWidth 同步往內縮、避免跑版撞到 value 區
    const int textX = rect.x + LyraMetrics::values.contentSidePadding + 12;
    int textWidth = contentWidth - (textX - rect.x) - LyraMetrics::values.contentSidePadding -
                    (rowValue != nullptr ? 60 : 0);
    auto itemName = rowTitle(i);
    auto item = renderer.truncatedText(UI_10_FONT_ID, itemName.c_str(), textWidth);
    renderer.drawText(UI_10_FONT_ID, textX, itemY + 6, item.c_str(), true,
                      isSelected ? EpdFontFamily::BOLD : EpdFontFamily::REGULAR);

    if (rowSubtitle != nullptr) {
      // Draw subtitle（跟標題對齊、同樣左推位置）
      std::string subtitleText = rowSubtitle(i);
      auto subtitle = renderer.truncatedText(SMALL_FONT_ID, subtitleText.c_str(), textWidth);
      renderer.drawText(SMALL_FONT_ID, textX, itemY + 30, subtitle.c_str(), true);
    }

    if (rowValue != nullptr) {
      // Draw value
      std::string valueText = rowValue(i);
      if (!valueText.empty()) {
        const auto valueTextWidth = renderer.getTextWidth(UI_10_FONT_ID, valueText.c_str());

        if (i == selectedIndex) {
          renderer.fillRoundedRect(
              contentWidth - LyraMetrics::values.contentSidePadding - hPaddingInSelection * 2 - valueTextWidth, itemY,
              valueTextWidth + hPaddingInSelection * 2, rowHeight, cornerRadius, Color::Black);
        }

        renderer.drawText(UI_10_FONT_ID,
                          contentWidth - LyraMetrics::values.contentSidePadding - hPaddingInSelection - valueTextWidth,
                          itemY + 6, valueText.c_str(), i != selectedIndex);
      }
    }
  }
}

void LyraTheme::drawButtonHints(GfxRenderer& renderer, const char* btn1, const char* btn2, const char* btn3,
                                const char* btn4) const {
  // stage15.20: 嚕寶要求拿掉所有按鍵導航 UI（底部 hints + 右側 hints）
  //   實體按鍵還在、只是不畫提示框 → 全部 activities 直接 no-op
  (void)renderer;
  (void)btn1;
  (void)btn2;
  (void)btn3;
  (void)btn4;
  return;
  // ---- 以下保留原邏輯做為參考、不會執行 ----
  const GfxRenderer::Orientation orig_orientation = renderer.getOrientation();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  const int pageHeight = renderer.getScreenHeight();
  constexpr int buttonWidth = 80;
  constexpr int smallButtonHeight = 15;
  constexpr int buttonHeight = LyraMetrics::values.buttonHintsHeight;
  constexpr int buttonY = LyraMetrics::values.buttonHintsHeight;  // Distance from bottom
  constexpr int textYOffset = 7;                                  // Distance from top of button to text baseline
  constexpr int buttonPositions[] = {58, 146, 254, 342};
  const char* labels[] = {btn1, btn2, btn3, btn4};

  for (int i = 0; i < 4; i++) {
    // Only draw if the label is non-empty
    const int x = buttonPositions[i];
    renderer.fillRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, false);
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      renderer.drawRoundedRect(x, pageHeight - buttonY, buttonWidth, buttonHeight, 1, cornerRadius, true, true, false,
                               false, true);
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
      const int textX = x + (buttonWidth - 1 - textWidth) / 2;
      renderer.drawText(SMALL_FONT_ID, textX, pageHeight - buttonY + textYOffset, labels[i]);
    } else {
      renderer.drawRoundedRect(x, pageHeight - smallButtonHeight, buttonWidth, smallButtonHeight, 1, cornerRadius, true,
                               true, false, false, true);
    }
  }

  renderer.setOrientation(orig_orientation);
}

void LyraTheme::drawSideButtonHints(const GfxRenderer& renderer, const char* topBtn, const char* bottomBtn) const {
  // stage15.20: 嚕寶要求拿掉所有畫面右邊的「向上 / 向下」提示鈕
  //   實體按鍵還在、只是不畫提示框 → 全部 activities 直接 no-op
  (void)renderer;
  (void)topBtn;
  (void)bottomBtn;
  return;
  // ---- 以下保留原邏輯做為參考、不會執行 ----
  const int screenWidth = renderer.getScreenWidth();
  constexpr int buttonWidth = LyraMetrics::values.sideButtonHintsWidth;  // Width on screen (height when rotated)
  constexpr int buttonHeight = 78;                                       // Height on screen (width when rotated)
  // Position for the button group - buttons share a border so they're adjacent

  const char* labels[] = {topBtn, bottomBtn};

  // Draw the shared border for both buttons as one unit
  const int x = screenWidth - buttonWidth;

  // Draw top button outline
  if (topBtn != nullptr && topBtn[0] != '\0') {
    renderer.drawRoundedRect(x, topHintButtonY, buttonWidth, buttonHeight, 1, cornerRadius, true, false, true, false,
                             true);
  }

  // Draw bottom button outline
  if (bottomBtn != nullptr && bottomBtn[0] != '\0') {
    renderer.drawRoundedRect(x, topHintButtonY + buttonHeight + 5, buttonWidth, buttonHeight, 1, cornerRadius, true,
                             false, true, false, true);
  }

  // Draw text for each button
  for (int i = 0; i < 2; i++) {
    if (labels[i] != nullptr && labels[i][0] != '\0') {
      const int y = topHintButtonY + (i * buttonHeight + 5);

      // Draw rotated text centered in the button
      const int textWidth = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);

      renderer.drawTextRotated90CW(SMALL_FONT_ID, x, y + (buttonHeight + textWidth) / 2, labels[i]);
    }
  }
}

// stage15.28 (嚕寶要求 laird 首頁風):
//   3 本書直列、每本一橫排：左大封面 + 右書名 + 作者
//   選中那本整列灰底反白
//   參考 laird/crosspoint-claw 首頁截圖
void LyraTheme::drawRecentBookCover(GfxRenderer& renderer, Rect rect, const std::vector<RecentBook>& recentBooks,
                                    const int selectorIndex, bool& coverRendered, bool& coverBufferStored,
                                    bool& bufferRestored, std::function<bool()> storeCoverBuffer,
                                    const BookReadingStats* /*stats*/, float /*progressPercent*/) const {
  if (recentBooks.empty()) return;

  const int rowCount = std::min(static_cast<int>(recentBooks.size()), LyraMetrics::values.homeRecentBooksCount);
  const int rowHeight = rect.height / rowCount;  // 每列平均分配高度
  const int innerCoverH = rowHeight - hPaddingInSelection * 2;
  const int coverWidthFixed = innerCoverH * 0.7;  // 書封寬高比約 0.7
  const int tileWidth = rect.width - 2 * LyraMetrics::values.contentSidePadding;

  if (!coverRendered) {
    for (int i = 0; i < rowCount; i++) {
      const RecentBook& book = recentBooks[i];
      const int tileX = LyraMetrics::values.contentSidePadding;
      const int tileY = rect.y + i * rowHeight;
      bool hasCover = false;
      int actualCoverW = coverWidthFixed;

      if (!book.coverBmpPath.empty()) {
        const std::string coverBmpPath = UITheme::getCoverThumbPath(book.coverBmpPath, innerCoverH);
        FsFile file;
        if (SdMan.openFileForRead("HOME", coverBmpPath, file)) {
          Bitmap bitmap(file);
          if (bitmap.parseHeaders() == BmpReaderError::Ok) {
            // 依書封實際寬高比縮放
            const int bmpW = bitmap.getWidth();
            const int bmpH = bitmap.getHeight();
            if (bmpW > 0 && bmpH > 0) {
              actualCoverW = std::min(coverWidthFixed, innerCoverH * bmpW / bmpH);
            }
            renderer.drawBitmap(bitmap, tileX + hPaddingInSelection, tileY + hPaddingInSelection,
                                actualCoverW, innerCoverH);
            hasCover = true;
          }
          file.close();
        }
      }

      // cover 邊框
      renderer.drawRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection,
                        actualCoverW, innerCoverH, true);

      if (!hasCover) {
        // 沒書封 placeholder
        renderer.fillRect(tileX + hPaddingInSelection, tileY + hPaddingInSelection + innerCoverH / 3,
                          actualCoverW, 2 * innerCoverH / 3, true);
        renderer.drawIcon(CoverIcon, tileX + hPaddingInSelection + actualCoverW / 2 - 16,
                          tileY + hPaddingInSelection + innerCoverH / 2 - 16, 32, 32);
      }
    }
    coverBufferStored = storeCoverBuffer();
    coverRendered = coverBufferStored;
  }

  // 第二輪：畫選中底色 + 書名 + 作者
  for (int i = 0; i < rowCount; i++) {
    const RecentBook& book = recentBooks[i];
    const bool selected = (selectorIndex == i);
    const int tileX = LyraMetrics::values.contentSidePadding;
    const int tileY = rect.y + i * rowHeight;

    if (selected) {
      // 選中整列灰底（cover 區除外、不要蓋住書封）
      renderer.fillRectDither(tileX + hPaddingInSelection + coverWidthFixed + LyraMetrics::values.verticalSpacing,
                              tileY + hPaddingInSelection,
                              tileWidth - hPaddingInSelection - coverWidthFixed - LyraMetrics::values.verticalSpacing,
                              innerCoverH, Color::LightGray);
    }

    const int textX = tileX + hPaddingInSelection + coverWidthFixed + LyraMetrics::values.verticalSpacing + 8;
    const int textWidth = tileWidth - 2 * hPaddingInSelection - LyraMetrics::values.verticalSpacing - coverWidthFixed - 16;

    auto titleStr = renderer.truncatedText(UI_12_FONT_ID, book.title.c_str(), textWidth, EpdFontFamily::BOLD);
    const int titleLineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int authorLineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    const int blockH = book.author.empty() ? titleLineHeight : (titleLineHeight + authorLineHeight + 4);
    int textY = tileY + (rowHeight - blockH) / 2;
    renderer.drawText(UI_12_FONT_ID, textX, textY, titleStr.c_str(), true, EpdFontFamily::BOLD);

    if (!book.author.empty()) {
      auto author = renderer.truncatedText(UI_10_FONT_ID, book.author.c_str(), textWidth);
      renderer.drawText(UI_10_FONT_ID, textX, textY + titleLineHeight + 4, author.c_str(), true);
    }
  }
}

void LyraTheme::drawButtonMenu(GfxRenderer& renderer, Rect rect, int buttonCount, int selectedIndex,
                               const std::function<std::string(int index)>& buttonLabel,
                               const std::function<UIIcon(int index)>& /*rowIcon*/) const {
  for (int i = 0; i < buttonCount; ++i) {
    int tileWidth = (rect.width - LyraMetrics::values.contentSidePadding * 2 - LyraMetrics::values.menuSpacing) / 2;
    Rect tileRect =
        Rect{rect.x + LyraMetrics::values.contentSidePadding + (LyraMetrics::values.menuSpacing + tileWidth) * (i % 2),
             rect.y + static_cast<int>(i / 2) * (LyraMetrics::values.menuRowHeight + LyraMetrics::values.menuSpacing),
             tileWidth, LyraMetrics::values.menuRowHeight};

    const bool selected = selectedIndex == i;

    if (selected) {
      renderer.fillRoundedRect(tileRect.x, tileRect.y, tileRect.width, tileRect.height, cornerRadius, Color::LightGray);
    }

    const char* label = buttonLabel(i).c_str();
    const int textX = tileRect.x + 16;
    const int lineHeight = renderer.getLineHeight(UI_12_FONT_ID);
    const int textY = tileRect.y + (LyraMetrics::values.menuRowHeight - lineHeight) / 2;

    // Invert text when the tile is selected, to contrast with the filled background
    renderer.drawText(UI_12_FONT_ID, textX, textY, label, true);
  }
}

Rect LyraTheme::drawPopup(const GfxRenderer& renderer, const char* message) const {
  constexpr int margin = 15;
  constexpr int y = 60;
  const int textWidth = renderer.getTextWidth(UI_12_FONT_ID, message, EpdFontFamily::REGULAR);
  const int textHeight = renderer.getLineHeight(UI_12_FONT_ID);
  const int w = textWidth + margin * 2;
  const int h = textHeight + margin * 2;
  const int x = (renderer.getScreenWidth() - w) / 2;

  renderer.fillRect(x - 5, y - 5, w + 10, h + 10, false);
  renderer.drawRect(x, y, w, h, true);

  const int textX = x + (w - textWidth) / 2;
  const int textY = y + margin - 2;
  renderer.drawText(UI_12_FONT_ID, textX, textY, message, true, EpdFontFamily::REGULAR);
  renderer.displayBuffer();
  return Rect{x, y, w, h};
}