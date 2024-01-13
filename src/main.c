#include <ace/generic/main.h>
#include <ace/utils/font.h>
#include <ace/managers/joy.h>
#include <ace/managers/key.h>
#include <ace/managers/state.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/palette.h>
#include <ace/utils/custom.h>
#include <ace/managers/blit.h>
#include <ace/macros.h>

#include "stfont.h"

#define FONTHEIGHT 14
#define FONTMAXCHAR 122
#define FONTMINCHAR 31
#define CONFIGFILE "cdtvlauncher.config"
#define SCRIPTNAME "cdtvlauncher.script"

static tView *s_pView;
static tVPort *s_pScreenshotVPort;
static tSimpleBufferManager *s_pScreenshotBufferManager;
static tCopBlock *s_pScreenshotCopBlock;
static UBYTE s_screenshotCopBlockColor0;
static tCopBlock *s_pOffCopBlock;
static tVPort *s_pListVPort;
static tSimpleBufferManager *s_pListBufferManager;
static tCopBlock *s_pListCopBlock;
static tFont *s_pFont;

static ULONG s_ulTimer = 0;

static UBYTE s_ubGameCount = 0;
static UBYTE s_ubSelectedGame = 0;
static char **s_ppGameNames;
static char **s_ppGameCommandLines;
static char **s_ppGameImages;

static char *memReallocFast(char *ptr, UWORD oldSize, UWORD size) {
  char *newPtr = memAllocFast(size);
  memcpy(newPtr, ptr, size);
  memFree(ptr, oldSize);
  return newPtr;
}

static UBYTE loadConfig(void) {
  LONG lSize = fileGetSize(CONFIGFILE);
  tFile *config = fileOpen(CONFIGFILE, "r");
  if (!config || lSize < 0) {
    logWrite("Failed to open config file " CONFIGFILE "\n");
    gameExit();
    return 0;
  }
  char *contents = memAllocFastClear(lSize + 1);
  do {
     lSize -= fileRead(config, contents, lSize);
  } while (lSize > 0);
  fileClose(config);

  char line[256];
  UBYTE state = 0;
  UBYTE i = 0;
  char c = 0;
  do {
    c = *contents++;
    if (c > FONTMAXCHAR) {
      c = 31;
    }
    if (c == '\r') {
      continue;
    } else if (c == '\n' || c == 0) {
      if (i > 0) {
        line[i] = '\0';
        if (line[0] == '#') {
          i = 0;
        } else if (state == 0) {
          if (s_ubGameCount == 0) {
            s_ppGameNames = memAllocFast(sizeof(char *));
            s_ppGameCommandLines = memAllocFast(sizeof(char *));
            s_ppGameImages = memAllocFast(sizeof(char *));
          } else {
            s_ppGameNames = (char**)memReallocFast((char*)s_ppGameNames, sizeof(char *) * s_ubGameCount, sizeof(char *) * (s_ubGameCount + 1));
            s_ppGameCommandLines = (char**)memReallocFast((char*)s_ppGameCommandLines, sizeof(char *) * s_ubGameCount, sizeof(char *) * (s_ubGameCount + 1));
            s_ppGameImages = (char**)memReallocFast((char*)s_ppGameImages, sizeof(char *) * s_ubGameCount, sizeof(char *) * (s_ubGameCount + 1));
          }
          s_ppGameNames[s_ubGameCount] = memAllocFast(sizeof(char) * (i + 1));
          strcpy(s_ppGameNames[s_ubGameCount], line);
          s_ubGameCount++;
          i = 0;
          state = 1;
        } else if (state == 1) {
          s_ppGameCommandLines[s_ubGameCount - 1] = memAllocFast(sizeof(char) * (i + 1));
          strcpy(s_ppGameCommandLines[s_ubGameCount - 1], line);
          i = 0;
          state = 2;
        } else if (state == 2) {
          s_ppGameImages[s_ubGameCount - 1] = memAllocFast(sizeof(char) * (i + 1));
          strcpy(s_ppGameImages[s_ubGameCount - 1], line);
          i = 0;
          state = 0;
        }
      }
    } else if (i < sizeof(line) - 1) {
      if (c == ';') {
        c = '\n';
      }
      line[i++] = c;
    } else {
      continue;
    }
  } while (c);
  return 1;
}

// ILBM parsing structures taken directly from spec
typedef struct __attribute__((__packed__)) {
  UWORD       w, h;             /* raster width & height in pixels      */
  WORD        x, y;             /* pixel position for this image        */
  UBYTE       nPlanes;          /* # source bitplanes                   */
  UBYTE     masking;
  UBYTE compression;
  UBYTE       Flags;            /* CMAP flags (formerly pad1, unused)   */
  UWORD       transparentColor; /* transparent "color number" (sort of) */
  UBYTE       xAspect, yAspect; /* pixel aspect, a ratio width : height */
  WORD        pageWidth, pageHeight; /* source "page" size in pixels    */
} BitMapHeader;

typedef struct __attribute__((__packed__)) {
  UBYTE red, green, blue;           /* color intensities 0..255 */
} ColorRegister;                  /* size = 3 bytes           */

typedef struct  __attribute__((__packed__)) {
  unsigned pad1 :4, red :4, green :4, blue :4;
} Color4;

static UBYTE loadIlbm(const char *filename) {
  UBYTE retval = 0;
  tFile *f = fileOpen(filename, "r");
  char *chunk = NULL;
  if (!f) {
    logWrite("Cannot open file: %s\n", filename);
    goto error;
  }
  fileSeek(f, 0, SEEK_END);
	LONG lSize = fileGetPos(f);
  fileSeek(f, 0, SEEK_SET);
	char *fileData = memAllocFast(lSize);
  if (!fileData) {
    logWrite("Cannot allocate %ld bytes of memory for file %s\n", lSize, filename);
    goto error;
  }
  chunk = fileData;
  for (LONG rem = lSize; rem > 0;) {
    ULONG bytesRead = fileRead(f, chunk + (lSize - rem), rem);
    rem -= bytesRead;
    if (!bytesRead) {
      break;
    }
  }
  ULONG size = 0;
  BitMapHeader bmhd;
  ColorRegister cr;
  Color4 c4;
  if (memcmp("FORM", chunk, 4)) {
    logWrite("Expected a FORM IFF file\n");
    goto error;
  }
  chunk += 4;
  chunk += 4; // skip size
  if (memcmp("ILBM", chunk, 4)) {
    logWrite("Expected an ILBM FORM file\n");
    goto error;
  }
  chunk += 4;
  if (memcmp("BMHD", chunk, 4)) {
    logWrite("Expected a BMHD bitmap header\n");
    goto error;
  }
  chunk += 4;
  memcpy(&size, chunk, 4);
  chunk += 4;
  if (size != sizeof(BitMapHeader)) {
    logWrite("Expected a bitmap header of size %ld\n", sizeof(BitMapHeader));
    goto error;
  }
  memcpy(&bmhd, chunk, size);
  chunk += size;
  if (memcmp("CMAP", chunk, 4)) {
    logWrite("Expected a CMAP colormap\n");
    goto error;
  }
  chunk += 4;
  memcpy(&size, chunk, 4);
  chunk += 4;
  for (UBYTE i = 0; i < size / 3; i++) {
    memcpy(&cr, chunk, sizeof(ColorRegister));
    chunk += sizeof(ColorRegister);
    c4.red = cr.red >> 4;
    c4.green = cr.green >> 4;
    c4.blue = cr.blue >> 4;
    copSetMoveVal(
      &s_pScreenshotCopBlock->pCmds[s_screenshotCopBlockColor0 + i].sMove,
      ((UWORD)c4.red << 8) | ((UWORD)c4.green << 4) | c4.blue
    );
  }
  s_pScreenshotCopBlock->ubUpdated = 2;
  s_pView->pCopList->ubStatus |= STATUS_UPDATE;
  if (size % 2 != 0) {
    chunk += 1; // skip pad byte for 16-bit alignment
  }
  if (memcmp("BODY", chunk, 4)) {
    logWrite("Expected a raster image BODY\n");
    goto error;
  }
  chunk += 4;
  memcpy(&size, chunk, 4);
  chunk += 4;
  if (size < bmhd.nPlanes * bmhd.w * bmhd.h / 8) {
    logWrite("Expected at least %d bytes of raster data, not %ld\n", bmhd.nPlanes * bmhd.w * bmhd.h / 8, size);
    goto error;
  }
  if (bmhd.compression != 0) {
    logWrite("Compressed IFF files are not supported\n");
    goto error;
  }
  blitUnsafeRect(s_pScreenshotBufferManager->pBack, 0, 0, s_pScreenshotBufferManager->uBfrBounds.uwX, s_pScreenshotBufferManager->uBfrBounds.uwY, 0);
  UWORD height = MIN(bmhd.h, s_pScreenshotBufferManager->uBfrBounds.uwY);
  UWORD width = MIN(bmhd.w, s_pScreenshotBufferManager->uBfrBounds.uwX);
  UWORD lineLength = (bmhd.w + 15) / 16;
  width = (width + 7) / 8;
  UWORD paddingBytes = lineLength * 2 - width;
  UWORD offs = 0;
  for (UBYTE row = 0; row < height; row++) {
    for (UBYTE plane = 0; plane < bmhd.nPlanes; plane++) {
      memcpy(s_pScreenshotBufferManager->pBack->Planes[plane] + offs, chunk, width);
      chunk += width + paddingBytes;
    }
    offs += s_pScreenshotBufferManager->pBack->BytesPerRow;
  }
  retval = 1;

  error:
    if (f) {
      fileClose(f);
    }
    if (chunk) {
      memFree(fileData, lSize);
    }
    return retval;
}

static void loadBitmap(void) {
  char *filename = s_ppGameImages[s_ubSelectedGame];
  if (loadIlbm(filename)) {
    // image loaded, all fine
  } else {
    blitUnsafeRect(s_pScreenshotBufferManager->pBack,
      0, 0,
      s_pScreenshotBufferManager->uBfrBounds.uwX, s_pScreenshotBufferManager->uBfrBounds.uwY,
      1);
    blitLine(s_pScreenshotBufferManager->pBack,
      0, 0,
      s_pScreenshotBufferManager->uBfrBounds.uwX, s_pScreenshotBufferManager->uBfrBounds.uwY,
      0, 0xffff, 0);
    blitLine(s_pScreenshotBufferManager->pBack,
      0, s_pScreenshotBufferManager->uBfrBounds.uwY,
      s_pScreenshotBufferManager->uBfrBounds.uwX, 0,
      0, 0xffff, 0);
  }
}

static void invertSelectedGameString(void) {
  UWORD uwBlitWords = s_pListBufferManager->uBfrBounds.uwX >> 4;
	ULONG ulOffs = s_pListBufferManager->pBack->BytesPerRow * s_ubSelectedGame * FONTHEIGHT;
  blitWait();
  g_pCustom->bltcon0 = USEA|USED|0x0f; // invert A;
  g_pCustom->bltcon1 = g_pCustom->bltamod = g_pCustom->bltdmod = 0;
  g_pCustom->bltapt = g_pCustom->bltdpt = &s_pListBufferManager->pBack->Planes[0][ulOffs];
  g_pCustom->bltsize = (FONTHEIGHT << HSIZEBITS) | uwBlitWords;
}

static void debugColor(USHORT color) {
  s_pListCopBlock->pCmds[2].sMove.bfValue = color;
  s_pListCopBlock->ubUpdated = 2;
  s_pView->pCopList->ubStatus |= STATUS_UPDATE;
}

static void loadPosition(void) {
  tFile *f = fileOpen(SCRIPTNAME, "r");
  char position[4];
  memset(position, 0, sizeof(position));
  if (f) {
    fileRead(f, position, 4);
    fileClose(f);
    if ((f = fileOpen(SCRIPTNAME, "w"))) {
      // clear file
      fileClose(f);
    }
  } else {
    position[0] = ';';
    position[1] = '0';
    position[2] = '0';
    position[3] = '0';
  }
  s_ubSelectedGame = 0;
  if (position[0] == ';') {
    for (UBYTE i = 1; i < 4; i++) {
      if (position[i] < '0' || position[i] > '9') {
        s_ubSelectedGame *= 10;
        s_ubSelectedGame += (position[i] - '0');
      } else {
        break;
      }
    }
    if (s_ubSelectedGame >= s_ubGameCount) {
      s_ubSelectedGame = 0;
    }
  }
}

void genericCreate(void) {
  if (!loadConfig()) {
    return;
  }
  loadPosition();

  keyCreate(); // We'll use keyboard
  joyOpen(); // We'll use joystick

  s_pView = viewCreate(0,
    TAG_VIEW_COPLIST_MODE, COPPER_MODE_BLOCK,
    TAG_VIEW_WINDOW_HEIGHT, 200,
    TAG_END);

  s_pScreenshotVPort = vPortCreate(0,
    TAG_VPORT_VIEW, s_pView,
    TAG_VPORT_BPP, 5,
    TAG_VPORT_HEIGHT, 140,
    TAG_END
  );
  s_pScreenshotBufferManager = simpleBufferCreate(0,
    TAG_SIMPLEBUFFER_VPORT, s_pScreenshotVPort,
    TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
    TAG_SIMPLEBUFFER_IS_DBLBUF, 0,
    TAG_SIMPLEBUFFER_USE_X_SCROLLING, 0,
    TAG_END
  );
  s_pScreenshotCopBlock = copBlockCreate(s_pView->pCopList, 33, 0, 0);
  copMove(s_pView->pCopList, s_pScreenshotCopBlock, &g_pCustom->bplcon0, (s_pScreenshotVPort->ubBPP << 12) | BV(9));
  s_screenshotCopBlockColor0 = s_pScreenshotCopBlock->uwCurrCount;
  copMove(s_pView->pCopList, s_pScreenshotCopBlock, &g_pCustom->color[0], 0x0000);
  copMove(s_pView->pCopList, s_pScreenshotCopBlock, &g_pCustom->color[1], 0x0888);
  copMove(s_pView->pCopList, s_pScreenshotCopBlock, &g_pCustom->color[2], 0x0800);
  for (UBYTE i = 3; i < 32; ++i) {
    copMove(s_pView->pCopList, s_pScreenshotCopBlock, &g_pCustom->color[i], 0x0000);
  }

  s_pListVPort = vPortCreate(0,
    TAG_VPORT_VIEW, s_pView,
    TAG_VPORT_BPP, 1,
    TAG_END
  );
  s_pFont = fontCreateFromMem(S_PFONTDATA);
  UWORD lineHeight = FONTHEIGHT;
  s_pListBufferManager = simpleBufferCreate(0,
    TAG_SIMPLEBUFFER_VPORT, s_pListVPort,
    TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
    TAG_SIMPLEBUFFER_IS_DBLBUF, 0,
    TAG_SIMPLEBUFFER_USE_X_SCROLLING, 0,
    TAG_SIMPLEBUFFER_BOUND_HEIGHT, MAX(lineHeight * s_ubGameCount, 150),
    TAG_END
  );
  cameraSetCoord(s_pListBufferManager->pCamera, 0, 0);
  s_pOffCopBlock = copBlockCreate(s_pView->pCopList, 1, 0, s_pView->ubPosY + s_pScreenshotVPort->uwOffsY + s_pScreenshotVPort->uwHeight);
  copMove(s_pView->pCopList, s_pOffCopBlock, &g_pCustom->dmacon, BV(8)); // disable bitplanes

  s_pListCopBlock = copBlockCreate(s_pView->pCopList, 4, 0, s_pView->ubPosY + s_pListVPort->uwOffsY);
  copMove(s_pView->pCopList, s_pListCopBlock, &g_pCustom->dmacon, BV(15) | BV(8)); // enable bitplanes
  copMove(s_pView->pCopList, s_pListCopBlock, &g_pCustom->bplcon0, (s_pListVPort->ubBPP << 12) | BV(9));
  copMove(s_pView->pCopList, s_pListCopBlock, &g_pCustom->color[0], 0x0000);
  copMove(s_pView->pCopList, s_pListCopBlock, &g_pCustom->color[1], 0x0800);
  for (UBYTE i = 0; i < s_ubGameCount; i++) {
    fontDrawStr1bpp(s_pFont, s_pListBufferManager->pBack, 0, i * lineHeight + 1, s_ppGameNames[i]);
  }
  invertSelectedGameString();

  timerCreate();
  viewLoad(s_pView);
}

void genericProcess(void) {
  keyProcess();
  joyProcess();
  timerProcess();
  static UBYTE s_ubInitialLoad = 0;
  if (!s_ubInitialLoad) {
    s_ubInitialLoad = 1;
    loadBitmap();
  } else if (timerCheck(&s_ulTimer, 10)) {
    // only check input every 10 frames
    if (keyCheck(KEY_ESCAPE)) {
      gameExit();
    } else if (keyCheck(KEY_RETURN) || keyCheck(KEY_NUMENTER) || joyCheck(JOY1_FIRE) || joyCheck(JOY2_FIRE)) {
      tFile *f = fileOpen(SCRIPTNAME, "w");
      if (f) {
        fileWrite(f, s_ppGameCommandLines[s_ubSelectedGame], strlen(s_ppGameCommandLines[s_ubSelectedGame]));
        fileClose(f);
      } else {
        logWrite("ERROR: Could not open " SCRIPTNAME " for writing.");
      }
      gameExit();
    } else if (joyCheck(JOY1_UP)) {
      if (s_ubSelectedGame > 0) {
        invertSelectedGameString();
        s_ubSelectedGame--;
        invertSelectedGameString();
        if (s_ubSelectedGame * FONTHEIGHT < s_pListBufferManager->pCamera->uPos.uwY) {
          cameraMoveBy(s_pListBufferManager->pCamera, 0, -FONTHEIGHT);
        }
        loadBitmap();
      }
    } else if (joyCheck(JOY1_DOWN)) {
      if (s_ubSelectedGame < s_ubGameCount - 1) {
        invertSelectedGameString();
        s_ubSelectedGame++;
        invertSelectedGameString();
        if (s_ubSelectedGame * FONTHEIGHT + FONTHEIGHT >= s_pListBufferManager->pCamera->uPos.uwY + s_pListVPort->uwHeight) {
          cameraMoveBy(s_pListBufferManager->pCamera, 0, FONTHEIGHT);
        }
        loadBitmap();
      }
    }
  }
  viewProcessManagers(s_pView);
  copProcessBlocks();
  vPortWaitUntilEnd(s_pListVPort);
}

void genericDestroy(void) {
  timerDestroy();
  copBlockDestroy(s_pView->pCopList, s_pScreenshotCopBlock);
  copBlockDestroy(s_pView->pCopList, s_pListCopBlock);
  copBlockDestroy(s_pView->pCopList, s_pOffCopBlock);
  for (UBYTE i = 0; i < s_ubGameCount; i++) {
    memFree(s_ppGameNames[i], strlen(s_ppGameNames[i]) + 1);
    memFree(s_ppGameCommandLines[i], strlen(s_ppGameCommandLines[i]) + 1);
    memFree(s_ppGameImages[i], strlen(s_ppGameImages[i]) + 1);
  }
  memFree(s_ppGameNames, sizeof(char *) * s_ubGameCount);
  memFree(s_ppGameCommandLines, sizeof(char *) * s_ubGameCount);
  memFree(s_ppGameImages, sizeof(char *) * s_ubGameCount);
  viewDestroy(s_pView);
  fontDestroy(s_pFont);
  keyDestroy(); // We don't need it anymore
  joyClose(); // We don't need it anymore
}
