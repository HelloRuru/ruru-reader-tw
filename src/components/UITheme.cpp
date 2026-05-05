#include "UITheme.h"

#include <GfxRenderer.h>

#include <memory>

#include "RecentBooksStore.h"
#include "components/themes/BaseTheme.h"
#include "components/themes/lyra/LyraTheme.h"
#include "components/themes/lyra/LyraFlowTheme.h"
#include "components/themes/lyra/Lyra3CoversTheme.h"
// stage5.5: 圓角 theme 暫時砍掉（選單空白 bug 還沒修）
// #include "components/themes/roundedraff/RoundedRaffTheme.h"

UITheme UITheme::instance;

UITheme::UITheme() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::reload() {
  auto themeType = static_cast<CrossPointSettings::UI_THEME>(SETTINGS.uiTheme);
  setTheme(themeType);
}

void UITheme::setTheme(CrossPointSettings::UI_THEME type) {
  switch (type) {
    // stage10: CLASSIC 砍掉，舊設定值 0 會 fallback 到 default (LYRA_FLOW)
    case CrossPointSettings::UI_THEME::LYRA:
      Serial.printf("[%lu] [UI] Using Lyra theme\n", millis());
      currentTheme = new LyraTheme();
      currentMetrics = &LyraMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA_3COVERS:
      Serial.printf("[%lu] [UI] Using Lyra 3Covers theme\n", millis());
      currentTheme = new Lyra3CoversTheme();
      currentMetrics = &Lyra3CoversMetrics::values;
      break;
    case CrossPointSettings::UI_THEME::LYRA_FLOW:
    default:
      Serial.printf("[%lu] [UI] Using Lyra Flow theme (carousel)\n", millis());
      currentTheme = new LyraFlowTheme();
      currentMetrics = &LyraFlowMetrics::values;
      break;
    // stage5.5: 圓角 theme 暫時砍掉（選單空白 bug 還沒修）
    // case CrossPointSettings::UI_THEME::ROUNDEDRAFF:
    //   ...
  }
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
