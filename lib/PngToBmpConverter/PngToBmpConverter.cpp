#include "PngToBmpConverter.h"

#include <PNGdec.h>
#include <SDCardManager.h>
#include <HardwareSerial.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace PngToBmpConverter {

namespace {

// 共用 PNGdec callback (從 PngToFramebufferConverter 抄)
void* pngOpen(const char* filename, int32_t* size) {
  FsFile* f = new FsFile();
  if (!SdMan.openFileForRead("PB", std::string(filename), *f)) {
    delete f;
    return nullptr;
  }
  *size = f->size();
  return f;
}

void pngClose(void* handle) {
  FsFile* f = reinterpret_cast<FsFile*>(handle);
  if (f) {
    f->close();
    delete f;
  }
}

int32_t pngRead(PNGFILE* pFile, uint8_t* pBuf, int32_t len) {
  FsFile* f = reinterpret_cast<FsFile*>(pFile->fHandle);
  if (!f) return 0;
  return f->read(pBuf, len);
}

int32_t pngSeek(PNGFILE* pFile, int32_t pos) {
  FsFile* f = reinterpret_cast<FsFile*>(pFile->fHandle);
  if (!f) return -1;
  return f->seek(pos);
}

// 解碼 context
struct DecodeContext {
  uint8_t* grayBuffer;  // 灰階完整影像 buffer（srcWidth × srcHeight）
  int srcWidth;
  int srcHeight;
};

// 把每行 RGBA 轉灰階存進 grayBuffer
int drawCallback(PNGDRAW* pDraw) {
  DecodeContext* ctx = reinterpret_cast<DecodeContext*>(pDraw->pUser);
  if (!ctx || !ctx->grayBuffer) return 0;

  if (pDraw->iWidth > 2048) return 0;  // 太大跳過

  // 直接從 pDraw->pPixels 取 raw data，用 pDraw->iPixelType 判斷格式
  const int y = pDraw->y;
  if (y < 0 || y >= ctx->srcHeight) return 0;

  uint8_t* dst = ctx->grayBuffer + y * ctx->srcWidth;
  const uint8_t* src = pDraw->pPixels;
  const int pixelType = pDraw->iPixelType;
  // PNG_PIXEL_GRAYSCALE = 0, PNG_PIXEL_TRUECOLOR = 2, PNG_PIXEL_INDEXED = 3,
  // PNG_PIXEL_GRAY_ALPHA = 4, PNG_PIXEL_TRUECOLOR_ALPHA = 6
  for (int x = 0; x < pDraw->iWidth && x < ctx->srcWidth; x++) {
    uint8_t lum = 255;  // default white
    if (pixelType == 0) {  // grayscale
      lum = src[x];
    } else if (pixelType == 4) {  // gray + alpha
      lum = src[x * 2];
    } else if (pixelType == 2) {  // RGB
      lum = (src[x * 3] * 30 + src[x * 3 + 1] * 59 + src[x * 3 + 2] * 11) / 100;
    } else if (pixelType == 6) {  // RGBA
      // alpha < 128 當白色（透明區）
      const uint8_t alpha = src[x * 4 + 3];
      if (alpha < 128) {
        lum = 255;
      } else {
        lum = (src[x * 4] * 30 + src[x * 4 + 1] * 59 + src[x * 4 + 2] * 11) / 100;
      }
    }
    dst[x] = lum;
  }
  return 1;  // continue decoding
}

}  // namespace

bool pngFileTo1BitBmpStreamWithSize(FsFile& /*pngFile*/, Print& bmpOut, int targetMaxWidth, int targetMaxHeight) {
  // PNGdec 需要重新 open by filename，不能直接用 FsFile 物件
  // 為了相容 jpegFileTo1BitBmpStreamWithSize 簽名，我們暫時要求呼叫端先把 PNG 寫到固定路徑
  // 實際上 Epub::generateThumbBmp 已經這樣做（寫到 .cover.jpg 暫存），所以 filename 是已知的
  // 但這個簽名沒傳 filename → 改造 Epub.cpp 直接呼叫支援 filename 的版本

  (void)bmpOut;
  (void)targetMaxWidth;
  (void)targetMaxHeight;
  Serial.printf("[%lu] [P2B] pngFileTo1BitBmpStreamWithSize: not implemented (filename version needed)\n", millis());
  return false;
}

bool pngFilenameTo1BitBmpStreamWithSize(const char* pngFilename, Print& bmpOut, int targetMaxWidth,
                                        int targetMaxHeight) {
  PNG* png = new (std::nothrow) PNG();
  if (!png) {
    Serial.printf("[%lu] [P2B] Failed to allocate PNG decoder\n", millis());
    return false;
  }

  int rc = png->open(pngFilename, pngOpen, pngClose, pngRead, pngSeek, drawCallback);
  if (rc != PNG_SUCCESS) {
    Serial.printf("[%lu] [P2B] PNG open failed (rc=%d): %s\n", millis(), rc, pngFilename);
    delete png;
    return false;
  }

  const int srcWidth = png->getWidth();
  const int srcHeight = png->getHeight();
  if (srcWidth <= 0 || srcHeight <= 0 || srcWidth > 2048 || srcHeight > 3072) {
    Serial.printf("[%lu] [P2B] PNG dims invalid: %dx%d\n", millis(), srcWidth, srcHeight);
    delete png;
    return false;
  }

  // 分配灰階完整影像 buffer
  const size_t bufSize = static_cast<size_t>(srcWidth) * static_cast<size_t>(srcHeight);
  uint8_t* grayBuffer = static_cast<uint8_t*>(malloc(bufSize));
  if (!grayBuffer) {
    Serial.printf("[%lu] [P2B] Failed to alloc gray buffer (%u bytes) for PNG %dx%d\n", millis(),
                  static_cast<unsigned>(bufSize), srcWidth, srcHeight);
    delete png;
    return false;
  }
  memset(grayBuffer, 255, bufSize);

  DecodeContext ctx;
  ctx.grayBuffer = grayBuffer;
  ctx.srcWidth = srcWidth;
  ctx.srcHeight = srcHeight;

  rc = png->decode(&ctx, 0);
  if (rc != PNG_SUCCESS) {
    Serial.printf("[%lu] [P2B] PNG decode failed (rc=%d)\n", millis(), rc);
    free(grayBuffer);
    delete png;
    return false;
  }
  delete png;

  // 計算縮放：aspect-fit
  float scaleX = static_cast<float>(targetMaxWidth) / srcWidth;
  float scaleY = static_cast<float>(targetMaxHeight) / srcHeight;
  float scale = (scaleX < scaleY) ? scaleX : scaleY;
  if (scale > 1.0f) scale = 1.0f;
  int dstWidth = static_cast<int>(srcWidth * scale);
  int dstHeight = static_cast<int>(srcHeight * scale);
  if (dstWidth < 1) dstWidth = 1;
  if (dstHeight < 1) dstHeight = 1;

  // 寫 BMP 表頭（1-bit BMP）
  // BMP 1-bit row 是 4-byte aligned
  const int rowBytes = ((dstWidth + 31) / 32) * 4;
  const uint32_t pixelDataSize = rowBytes * dstHeight;
  const uint32_t paletteSize = 8;  // 2 colors × 4 bytes
  const uint32_t headerSize = 14 + 40 + paletteSize;
  const uint32_t fileSize = headerSize + pixelDataSize;

  // BMP file header (14 bytes)
  bmpOut.write('B'); bmpOut.write('M');
  bmpOut.write(reinterpret_cast<const uint8_t*>(&fileSize), 4);
  bmpOut.write(static_cast<uint8_t>(0)); bmpOut.write(static_cast<uint8_t>(0));  // reserved
  bmpOut.write(static_cast<uint8_t>(0)); bmpOut.write(static_cast<uint8_t>(0));  // reserved
  bmpOut.write(reinterpret_cast<const uint8_t*>(&headerSize), 4);  // bfOffBits

  // DIB header (40 bytes BITMAPINFOHEADER)
  uint32_t biSize = 40; bmpOut.write(reinterpret_cast<const uint8_t*>(&biSize), 4);
  int32_t biWidth = dstWidth; bmpOut.write(reinterpret_cast<const uint8_t*>(&biWidth), 4);
  int32_t biHeight = -dstHeight; bmpOut.write(reinterpret_cast<const uint8_t*>(&biHeight), 4);  // top-down
  uint16_t planes = 1; bmpOut.write(reinterpret_cast<const uint8_t*>(&planes), 2);
  uint16_t bpp = 1; bmpOut.write(reinterpret_cast<const uint8_t*>(&bpp), 2);
  uint32_t comp = 0; bmpOut.write(reinterpret_cast<const uint8_t*>(&comp), 4);
  uint32_t imgSize = pixelDataSize; bmpOut.write(reinterpret_cast<const uint8_t*>(&imgSize), 4);
  int32_t xpp = 2835; bmpOut.write(reinterpret_cast<const uint8_t*>(&xpp), 4);
  int32_t ypp = 2835; bmpOut.write(reinterpret_cast<const uint8_t*>(&ypp), 4);
  uint32_t clrUsed = 2; bmpOut.write(reinterpret_cast<const uint8_t*>(&clrUsed), 4);
  uint32_t clrImp = 0; bmpOut.write(reinterpret_cast<const uint8_t*>(&clrImp), 4);

  // Color palette: index 0 = black, index 1 = white (BGRA)
  bmpOut.write(static_cast<uint8_t>(0)); bmpOut.write(static_cast<uint8_t>(0));
  bmpOut.write(static_cast<uint8_t>(0)); bmpOut.write(static_cast<uint8_t>(0));  // black
  bmpOut.write(static_cast<uint8_t>(255)); bmpOut.write(static_cast<uint8_t>(255));
  bmpOut.write(static_cast<uint8_t>(255)); bmpOut.write(static_cast<uint8_t>(0));  // white

  // 寫像素：每行縮放、threshold 50% 變 1-bit
  std::vector<uint8_t> rowBuf(rowBytes, 0);
  for (int dy = 0; dy < dstHeight; dy++) {
    memset(rowBuf.data(), 0, rowBytes);
    const int srcY = static_cast<int>(dy / scale);
    if (srcY >= srcHeight) continue;
    const uint8_t* srcRow = grayBuffer + srcY * srcWidth;
    for (int dx = 0; dx < dstWidth; dx++) {
      const int srcX = static_cast<int>(dx / scale);
      if (srcX >= srcWidth) continue;
      const uint8_t lum = srcRow[srcX];
      // threshold 128: 高於 = 白 (bit=1), 低於 = 黑 (bit=0)
      if (lum >= 128) {
        rowBuf[dx >> 3] |= (1 << (7 - (dx & 7)));
      }
    }
    bmpOut.write(rowBuf.data(), rowBytes);
  }

  free(grayBuffer);
  Serial.printf("[%lu] [P2B] PNG->BMP success: %dx%d -> %dx%d\n", millis(), srcWidth, srcHeight, dstWidth, dstHeight);
  return true;
}

}  // namespace PngToBmpConverter
