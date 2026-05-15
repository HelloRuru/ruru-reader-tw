#pragma once

#include <vector>

#include "CrossPointSettings.h"
// stage10: KOReaderCredentialStore 砍掉
#include "activities/settings/SettingsActivity.h"

// Shared settings list used by both the device settings UI and the web settings API.
// Each entry has a key (for JSON API) and category (for grouping).
// ACTION-type entries and entries without a key are device-only.

inline std::vector<SettingInfo> getSettingsList() {
  return {
      // --- Display ---
      SettingInfo::Enum("休眠屏", &CrossPointSettings::sleepScreen,
                      {"預設黑", "預設白", "自定義", "書籍封面", "透明桌布","透明桌布2","無", "封面 + 自定義"}, "sleepScreen", "Display"),
    SettingInfo::Enum("休眠屏封面模式", &CrossPointSettings::sleepScreenCoverMode, {"適配", "裁剪"},
                      "sleepScreenCoverMode", "Display"),
    SettingInfo::Enum("休眠屏封面濾鏡", &CrossPointSettings::sleepScreenCoverFilter,
                      {"無", "增強對比度", "反色"}, "sleepScreenCoverFilter", "Display"),
    SettingInfo::Enum(
        "狀態列", &CrossPointSettings::statusBar,
        {"無", "不顯示進度", "完整+百分比", "完整+書籍條", "僅書籍條", "完整+章節條"}, "statusBar", "Display"),
    SettingInfo::Enum("隱藏電池百分比", &CrossPointSettings::hideBatteryPercentage, {"從不", "僅閱讀", "總是"},
                      "hideBatteryPercentage", "Display"),
    SettingInfo::Enum("重新整理頻率", &CrossPointSettings::refreshFrequency,
                      {"1 page", "5 pages", "10 pages", "15 pages", "30 pages"},"refreshFrequency","Display"),
    // stage15.11: 全部 theme 合併進 Flow（內含圖書館卡風裝飾）、UI 主題選單砍掉
    SettingInfo::Toggle("抗陽光褪色", &CrossPointSettings::fadingFix,"Sunlight Fading Compensation","Display"),

      // --- Reader ---
      // stage15.32: 字型標籤誠實標示
      //   3 個 enum 值（BOOKERLY/NOTOSANS/OPENDYSLEXIC）實際全部都綁同一個 jfopenhuninn 粉圓體
      //   見 main.cpp:45-48 全用 jfopenhuninn_17_regular
      //   FONT_CUSTOM 才是真的不同字型（從 SD 卡 .epdfont 載入）
      SettingInfo::Enum("字型", &CrossPointSettings::fontFamily, {"粉圓", "粉圓", "粉圓", "自定義"}, "字型", "Reader"),
      SettingInfo::Enum("字號", &CrossPointSettings::fontSize, {"小", "中", "大", "特大"}, "字號", "Reader"),
    SettingInfo::Enum("行間距", &CrossPointSettings::lineSpacing,  {"Tight", "Normal", "Wide"}, "行間距", "Reader"),
    SettingInfo::Toggle("首行縮排", &CrossPointSettings::firstlineintented, "首行縮排","Reader"),
    SettingInfo::Value("字間距", &CrossPointSettings::wordSpacing, 0,10,2, "字間距", "Reader"),
    SettingInfo::Value("上邊距", &CrossPointSettings::screenMargin_Top, 0,80,5, "上邊距", "Reader"),
    SettingInfo::Value("下邊距", &CrossPointSettings::screenMargin_Bottom, 0,80,5, "下邊距", "Reader"),
    SettingInfo::Value("左邊距", &CrossPointSettings::screenMargin_Left, 0,40,5,"左邊距", "Reader"),
    SettingInfo::Value("右邊距", &CrossPointSettings::screenMargin_Right, 0,40,5, "右邊距", "Reader"),
    SettingInfo::Toggle("閱讀背景", &CrossPointSettings::ReadingScreenEnabled,"閱讀背景","Reader"),
    SettingInfo::Toggle("劃線", &CrossPointSettings::extraline,"劃線","Reader"),
    SettingInfo::Enum("橫排劃線位置", &CrossPointSettings::horizontalLinePosition,
                      {"下方", "上方"}, "horizontalLinePosition", "Reader"),
    SettingInfo::Value("下方劃線間距", &CrossPointSettings::underlineBelowOffset, 0,24,1, "underlineBelowOffset", "Reader"),
    SettingInfo::Value("上方劃線間距", &CrossPointSettings::underlineAboveOffset, 0,24,1, "underlineAboveOffset", "Reader"),
    SettingInfo::Value("直排劃線間距", &CrossPointSettings::verticalLineOffset, 0,24,1, "verticalLineOffset", "Reader"),
    SettingInfo::Enum("對齊方式", &CrossPointSettings::paragraphAlignment,
                      {"兩邊對齊", "左對齊", "居中", "右對齊", "書本樣式"}, "對齊方式", "Reader"),
    // stage15.4: 直排支援
    SettingInfo::Enum("文字排列", &CrossPointSettings::textLayout,
                      {"橫排", "直排"}, "文字排列", "Reader"),
    // stage15.32: 「直排翻頁反轉」拿掉、選項存在但無實裝（程式碼預留沒串接、開了沒效果、誤導使用者）
    // SettingInfo::Toggle("直排翻頁反轉", &CrossPointSettings::verticalPageReverse,
    //                     "直排翻頁反轉", "Reader"),
    SettingInfo::Toggle("是否使用書籍內嵌樣式", &CrossPointSettings::embeddedStyle,"是否使用書籍內嵌樣式","Reader"),
    SettingInfo::Toggle("連字元", &CrossPointSettings::hyphenationEnabled,"連字元","Reader"),
    SettingInfo::Enum("閱讀方向", &CrossPointSettings::orientation,
                      {"預設方向", "按鈕在左邊", "按鈕在上邊", "按鈕在右邊"},"閱讀方向","Reader"),
    SettingInfo::Toggle("額外段間距", &CrossPointSettings::extraParagraphSpacing,"額外段間距","Reader"),
    SettingInfo::Toggle("抗鋸齒", &CrossPointSettings::textAntiAliasing,"抗鋸齒","Reader"),

      // --- Controls ---
      SettingInfo::Enum("側邊按鈕設定（僅閱讀）", &CrossPointSettings::sideButtonLayout,
                        {"上, 下", "下, 上"}, "sideButtonLayout", "Controls"),
      SettingInfo::Toggle("長按跳章節", &CrossPointSettings::longPressChapterSkip, "longPressChapterSkip",
                          "Controls"),
      SettingInfo::Enum("短按電源鍵", &CrossPointSettings::shortPwrBtn, {"忽略", "休眠", "翻頁"},
                        "shortPwrBtn", "Controls"),

      // --- System ---
      SettingInfo::Enum("休眠時間", &CrossPointSettings::sleepTimeout,
                        {"1 min", "5 min", "10 min", "15 min", "30 min"}, "sleepTimeout", "System"),
      //SettingInfo::Toggle("bluetoothEnabled", &CrossPointSettings::bluetoothEnabled, "bluetoothEnabled", "System"),

      // stage10: KOReader Sync 砍掉
      // stage10: 堅果雲配置 砍掉

      // --- OPDS Browser (web-only, uses CrossPointSettings char arrays) ---
      SettingInfo::String("OPDS Server URL", SETTINGS.opdsServerUrl, sizeof(SETTINGS.opdsServerUrl), "opdsServerUrl",
                          "OPDS Browser"),
      SettingInfo::String("OPDS Username", SETTINGS.opdsUsername, sizeof(SETTINGS.opdsUsername), "opdsUsername",
                          "OPDS Browser"),
      SettingInfo::String("OPDS Password", SETTINGS.opdsPassword, sizeof(SETTINGS.opdsPassword), "opdsPassword",
                          "OPDS Browser"),
  };
}
