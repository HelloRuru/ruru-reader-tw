#include "BootActivity.h"

#include <GfxRenderer.h>

#include "fontIds.h"
#include "images/RabbitLarge.h"  // stage15.35: HelloRuru 兔子 Logo

void BootActivity::onEnter() {
  Activity::onEnter();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();
  // stage15.35 (嚕寶改 boot 畫面): 中央兔子 + RURU-READER 文字
  //   兔子 logo 取代原 CrossLarge、文字 CrossPoint → RURU-READER、BOOTING → 啟動中...
  renderer.drawImage(RabbitLarge, (pageWidth - 128) / 2, (pageHeight - 128) / 2, 128, 128);
  renderer.drawCenteredText(UI_10_FONT_ID, pageHeight / 2 + 70, "RURU-READER", true, EpdFontFamily::BOLD);
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 95, "啟動中...");
  renderer.drawCenteredText(SMALL_FONT_ID, pageHeight - 30, CROSSPOINT_VERSION);
  renderer.displayBuffer();
}
