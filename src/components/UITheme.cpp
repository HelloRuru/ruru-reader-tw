#include "UITheme.h"

#include <GfxRenderer.h>

#include <memory>

#include "RecentBooksStore.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "components/themes/lyra/LyraFlowTheme.h"
// stage5.5/stage12.5: RoundedRaff 圓角 theme 砍掉
// stage15: Lyra（基本款）和 Lyra3Covers（三封面）砍掉
// stage15.11: LibraryCardTheme 砍掉、合併進 LyraFlowTheme（Flow + 卡風）
//             只剩一個 theme = LyraFlow（內含卡號 + 撕邊裝飾）

UITheme UITheme::instance;

UITheme::UITheme() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::reload() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::setTheme(CrossPointSettings::UI_THEME /*type*/) {
  // stage15.29 (嚕寶要求回復原本 FLOW THEME):
  //   回到 LyraFlowTheme（FLOW carousel 斜邊 + ICON menu）
  Serial.printf("[%lu] [UI] Using Lyra Flow theme (FLOW carousel + ICON menu)\n", millis());
  currentTheme = new LyraFlowTheme();
  currentMetrics = &LyraFlowMetrics::values;
}

int UITheme::getNumberOfItemsPerPage(const GfxRenderer& renderer, bool hasHeader, bool hasTabBar, bool hasButtonHints,
                                     bool hasSubtitle) {
  const ThemeMetrics& metrics = UITheme::getInstance().getMetrics();
  int reservedHeight = metrics.topPadding;
  if (hasHeader) {
    reservedHeight += metrics.headerHeight + metrics.verticalSpacing;
  }
  if (hasTabBar) {
    reservedHeight += metrics.tabBarHeight;
  }
  if (hasButtonHints) {
    reservedHeight += metrics.verticalSpacing + metrics.buttonHintsHeight;
  }
  const int availableHeight = renderer.getScreenHeight() - reservedHeight;
  int rowHeight = hasSubtitle ? metrics.listWithSubtitleRowHeight : metrics.listRowHeight;
  return availableHeight / rowHeight;
}

std::string UITheme::getCoverThumbPath(std::string coverBmpPath, int coverHeight) {
  size_t pos = coverBmpPath.find("[HEIGHT]", 0);
  if (pos != std::string::npos) {
    coverBmpPath.replace(pos, 8, std::to_string(coverHeight));
  }
  return coverBmpPath;
}
