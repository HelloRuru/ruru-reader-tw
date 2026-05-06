#pragma once

#include <SdFat.h>
#include <Print.h>

namespace PngToBmpConverter {

// 把 PNG 檔轉成 1-bit BMP 流（縮放到 targetMaxWidth × targetMaxHeight，aspect-fit）
// 用法跟 JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize 相同
// 給 Epub::generateThumbBmp 用，補 ChineseType 原版只支援 JPG cover 的限制
bool pngFileTo1BitBmpStreamWithSize(FsFile& pngFile, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);
bool pngFilenameTo1BitBmpStreamWithSize(const char* pngFilename, Print& bmpOut, int targetMaxWidth, int targetMaxHeight);

}  // namespace PngToBmpConverter
