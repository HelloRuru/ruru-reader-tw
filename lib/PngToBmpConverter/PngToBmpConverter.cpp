#include "PngToBmpConverter.h"

#include <PNGdec.h>
#include <SDCardManager.h>
#include <HardwareSerial.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace PngToBmpConverter {

namespace {

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

// Streaming context：不存整張、邊解碼邊寫 BMP
struct DecodeContext {
  Print* bmpOut;
  int srcWidth;
  int srcHeight;
  int dstWidth;
  int dstHeight;
  float scale;
  int rowBytes;       // BMP 每行 byte 數（4-byte aligned）
  int nextDstY;       // 下一個要寫的 dst row（0-based）
  int nextSrcY;       // 下一個 dst row 對應的 src y
  uint8_t* rowBuf;    // BMP 每行的 1-bit buffer（rowBytes bytes）
  bool failed;
};

// 把目前 src row 縮放成 BMP 1-bit row，立刻寫出
void emitDstRow(DecodeContext* ctx, const uint8_t* srcLuminance) {
  memset(ctx->rowBuf, 0, ctx->rowBytes);
  for (int dx = 0; dx < ctx->dstWidth; dx++) {
    int srcX = static_cast<int>(dx / ctx->scale);
    if (srcX >= ctx->srcWidth) srcX = ctx->srcWidth - 1;
    const uint8_t lum = srcLuminance[srcX];
    // threshold 128：白 = bit 1，黑 = bit 0
    if (lum >= 128) {
      ctx->rowBuf[dx >> 3] |= (1 << (7 - (dx & 7)));
    }
  }
  ctx->bmpOut->write(ctx->rowBuf, ctx->rowBytes);
  ctx->nextDstY++;
  // 算下一個 dst row 對應的 src y
  ctx->nextSrcY = static_cast<int>(ctx->nextDstY / ctx->scale);
  if (ctx->nextSrcY >= ctx->srcHeight) ctx->nextSrcY = ctx->srcHeight - 1;
}

// 解碼 callback：每一行 PNG 解出來都呼叫一次
// 我們判斷「這行 srcY 是不是某個 dst row 需要的」、是就立刻轉並寫
int drawCallback(PNGDRAW* pDraw) {
  DecodeContext* ctx = reinterpret_cast<DecodeContext*>(pDraw->pUser);
  if (!ctx || ctx->failed) return 0;

  const int srcY = pDraw->y;
  const int width = pDraw->iWidth;
  const uint8_t* src = pDraw->pPixels;
  const int pixelType = pDraw->iPixelType;

  // 已經寫完所有 dst row → 後面的 src row 直接丟
  if (ctx->nextDstY >= ctx->dstHeight) return 1;

  // 這個 srcY 還沒到 nextSrcY → 跳過（nearest-neighbor 不需要中間的源行）
  if (srcY < ctx->nextSrcY) return 1;

  // 此 srcY 是 nextSrcY → 把它轉灰階再寫成 1-bit BMP row
  // 為了避免動態分配，用 stack/static buffer（每次 callback 局部）
  static uint8_t lumLine[2048];  // 限 srcWidth <= 2048
  if (width > 2048) {
    ctx->failed = true;
    return 0;
  }

  for (int x = 0; x < width; x++) {
    uint8_t lum = 255;
    if (pixelType == 0) {  // grayscale
      lum = src[x];
    } else if (pixelType == 4) {  // gray + alpha
      lum = src[x * 2];
    } else if (pixelType == 2) {  // RGB
      lum = (src[x * 3] * 30 + src[x * 3 + 1] * 59 + src[x * 3 + 2] * 11) / 100;
    } else if (pixelType == 6) {  // RGBA
      const uint8_t alpha = src[x * 4 + 3];
      lum = (alpha < 128) ? 255
                          : (src[x * 4] * 30 + src[x * 4 + 1] * 59 + src[x * 4 + 2] * 11) / 100;
    } else if (pixelType == 3) {  // indexed — 不支援精準解碼，當白
      lum = 255;
    }
    lumLine[x] = lum;
  }

  // 同一個 srcY 可能要寫多個 dst row（放大時 scale > 1，但我們限 scale <= 1，所以最多 1 行）
  // 為防萬一還是 while
  while (ctx->nextDstY < ctx->dstHeight && ctx->nextSrcY == srcY) {
    emitDstRow(ctx, lumLine);
  }

  return 1;
}

void writeLE32(Print& out, uint32_t v) {
  out.write(static_cast<uint8_t>(v & 0xFF));
  out.write(static_cast<uint8_t>((v >> 8) & 0xFF));
  out.write(static_cast<uint8_t>((v >> 16) & 0xFF));
  out.write(static_cast<uint8_t>((v >> 24) & 0xFF));
}
void writeLE16(Print& out, uint16_t v) {
  out.write(static_cast<uint8_t>(v & 0xFF));
  out.write(static_cast<uint8_t>((v >> 8) & 0xFF));
}

void writeBmpHeader(Print& out, int dstWidth, int dstHeight, int rowBytes) {
  const uint32_t pixelDataSize = static_cast<uint32_t>(rowBytes) * static_cast<uint32_t>(dstHeight);
  const uint32_t paletteSize = 8;
  const uint32_t headerSize = 14 + 40 + paletteSize;
  const uint32_t fileSize = headerSize + pixelDataSize;

  // BMP file header (14 bytes)
  out.write('B'); out.write('M');
  writeLE32(out, fileSize);
  writeLE16(out, 0); writeLE16(out, 0);  // reserved
  writeLE32(out, headerSize);

  // DIB header (40 bytes)
  writeLE32(out, 40);                                  // biSize
  writeLE32(out, static_cast<uint32_t>(dstWidth));     // biWidth
  writeLE32(out, static_cast<uint32_t>(-dstHeight));   // biHeight (negative = top-down)
  writeLE16(out, 1);                                   // planes
  writeLE16(out, 1);                                   // bpp
  writeLE32(out, 0);                                   // BI_RGB
  writeLE32(out, pixelDataSize);
  writeLE32(out, 2835);                                // X pixels per meter
  writeLE32(out, 2835);                                // Y pixels per meter
  writeLE32(out, 2);                                   // colors used
  writeLE32(out, 0);                                   // colors important

  // Color palette: index 0 = black BGRA, index 1 = white BGRA
  out.write(static_cast<uint8_t>(0)); out.write(static_cast<uint8_t>(0));
  out.write(static_cast<uint8_t>(0)); out.write(static_cast<uint8_t>(0));
  out.write(static_cast<uint8_t>(255)); out.write(static_cast<uint8_t>(255));
  out.write(static_cast<uint8_t>(255)); out.write(static_cast<uint8_t>(0));
}

}  // namespace

bool pngFileTo1BitBmpStreamWithSize(FsFile& /*pngFile*/, Print& /*bmpOut*/, int /*targetMaxWidth*/,
                                    int /*targetMaxHeight*/) {
  // 不再支援 — Epub.cpp 已改用 filename 版本
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
  if (srcWidth <= 0 || srcHeight <= 0 || srcWidth > 2048 || srcHeight > 4096) {
    Serial.printf("[%lu] [P2B] PNG dims invalid: %dx%d\n", millis(), srcWidth, srcHeight);
    delete png;
    return false;
  }

  // aspect-fit scale
  const float scaleX = static_cast<float>(targetMaxWidth) / srcWidth;
  const float scaleY = static_cast<float>(targetMaxHeight) / srcHeight;
  float scale = (scaleX < scaleY) ? scaleX : scaleY;
  if (scale > 1.0f) scale = 1.0f;
  int dstWidth = static_cast<int>(srcWidth * scale);
  int dstHeight = static_cast<int>(srcHeight * scale);
  if (dstWidth < 1) dstWidth = 1;
  if (dstHeight < 1) dstHeight = 1;

  const int rowBytes = ((dstWidth + 31) / 32) * 4;

  // 分配只需要的小 buffer：BMP 一行
  uint8_t* rowBuf = static_cast<uint8_t*>(malloc(rowBytes));
  if (!rowBuf) {
    Serial.printf("[%lu] [P2B] Failed to alloc row buffer (%d bytes)\n", millis(), rowBytes);
    delete png;
    return false;
  }

  // 寫 BMP header
  writeBmpHeader(bmpOut, dstWidth, dstHeight, rowBytes);

  // 設 streaming context
  DecodeContext ctx;
  ctx.bmpOut = &bmpOut;
  ctx.srcWidth = srcWidth;
  ctx.srcHeight = srcHeight;
  ctx.dstWidth = dstWidth;
  ctx.dstHeight = dstHeight;
  ctx.scale = scale;
  ctx.rowBytes = rowBytes;
  ctx.nextDstY = 0;
  ctx.nextSrcY = 0;  // 第一個 dst row 對應 src y 0
  ctx.rowBuf = rowBuf;
  ctx.failed = false;

  rc = png->decode(&ctx, 0);
  free(rowBuf);
  delete png;

  if (rc != PNG_SUCCESS || ctx.failed) {
    Serial.printf("[%lu] [P2B] PNG decode failed (rc=%d, failed=%d)\n", millis(), rc, ctx.failed);
    return false;
  }

  // 防呆：萬一 nearest-neighbor 算下來最後幾 row 沒寫到，補白行
  while (ctx.nextDstY < ctx.dstHeight) {
    uint8_t* blank = static_cast<uint8_t*>(malloc(rowBytes));
    if (!blank) break;
    memset(blank, 0xFF, rowBytes);  // 全白
    bmpOut.write(blank, rowBytes);
    free(blank);
    ctx.nextDstY++;
  }

  Serial.printf("[%lu] [P2B] PNG->BMP success (stream): %dx%d -> %dx%d\n", millis(), srcWidth, srcHeight, dstWidth,
                dstHeight);
  return true;
}

}  // namespace PngToBmpConverter
