#include <ace/generic/main.h>
#include <ace/utils/disk_file.h>
#include <ace/utils/font.h>
#include <ace/managers/joy.h>
#include <ace/managers/key.h>
#include <ace/managers/viewport/simplebuffer.h>

#include "stfont.h"

#define FONTHEIGHT 14
#define FONTMAXCHAR 122
#define FONTMINCHAR 31
#define CONFIGFILE "acelauncher.config"
#define SCRIPTNAME "acelauncher.script"

static tView *s_pView;
static tVPort *s_pScreenshotVPort;
static tSimpleBufferManager *s_pScreenshotBufferManager;
static tVPort *s_pListVPort;
static tSimpleBufferManager *s_pListBufferManager;
static tCopBlock *s_pListColorsBlock;
static tCopBlock *s_pSelectedColorsBlock;
static tCopBlock *s_pUnselectedColorsBlock;
static tCopBlock *s_pPicColorsBlock;

static UBYTE s_ubTimer = 0;

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
  tFile *config = diskFileOpen(CONFIGFILE, "r");
  LONG lSize = fileGetSize(config);
  if (!config || lSize < 0) {
    logWrite("Failed to open config file " CONFIGFILE "\n");
    gameExit();
    return 0;
  }
  char *contentsAllocated = memAllocFastClear(lSize + 1);
  char *contents = contentsAllocated;
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
  memFree(contentsAllocated, lSize + 1);
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
  tFile *f = diskFileOpen(filename, "r");
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
    s_pScreenshotVPort->pPalette[i] = ((UWORD)c4.red << 8) | ((UWORD)c4.green << 4) | c4.blue;
  }
  viewUpdateGlobalPalette(s_pScreenshotVPort->pView);
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

static void changeSelection(void) {
  UWORD baseOffset = s_pView->ubPosY + s_pScreenshotVPort->uwOffsY + s_pScreenshotVPort->uwHeight + \
      s_ubSelectedGame * FONTHEIGHT - s_pListBufferManager->pCamera->uPos.uwY;

  copBlockWait(s_pListVPort->pView->pCopList, s_pSelectedColorsBlock, 0, baseOffset);
  copBlockWait(s_pListVPort->pView->pCopList, s_pUnselectedColorsBlock, 0, baseOffset + FONTHEIGHT);
}

static void loadPosition(void) {
  tFile *f = diskFileOpen(SCRIPTNAME, "r");
  char position[4];
  memset(position, 0, sizeof(position));
  if (f) {
    fileRead(f, position, 4);
    fileClose(f);
    if ((f = diskFileOpen(SCRIPTNAME, "w"))) {
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
      if (position[i] >= '0' && position[i] <= '9') {
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

static tFont *fontCreateFromMem(const void *memory) {
  logBlockBegin("fontCreateFromMem(memory: %p)", memory);
	tFont *pFont = (tFont *) memAllocFast(sizeof(tFont));
	if (!pFont) {
		return 0;
	}

  memcpy(&pFont->uwWidth, memory, sizeof(UWORD));
  memory += sizeof(UWORD);
  memcpy(&pFont->uwHeight, memory, sizeof(UWORD));
  memory += sizeof(UWORD);
  memcpy(&pFont->ubChars, memory, sizeof(UBYTE));
  memory += sizeof(UBYTE);
	logWrite(
		"Addr: %p, data width: %upx, chars: %u, font height: %upx\n",
		pFont, pFont->uwWidth, pFont->ubChars, pFont->uwHeight
	);

	pFont->pCharOffsets = memAllocFast(sizeof(UWORD) * pFont->ubChars);
  memcpy(pFont->pCharOffsets, memory, sizeof(UWORD) * pFont->ubChars);
  memory += sizeof(UWORD) * pFont->ubChars;

	pFont->pRawData = bitmapCreate(pFont->uwWidth, pFont->uwHeight, 1, 0);
	UWORD uwPlaneByteSize = ((pFont->uwWidth+15)/16) * 2 * pFont->uwHeight;
  memcpy(pFont->pRawData->Planes[0], memory, uwPlaneByteSize);

	logBlockEnd("fontCreateFromMem()");
	return pFont;
}

void genericCreate(void) {
  if (!loadConfig()) {
    return;
  }
  loadPosition();

  keyCreate(); // We'll use keyboard
  joyOpen(); // We'll use joystick

  s_pView = viewCreate(0,
    TAG_VIEW_WINDOW_HEIGHT, 200,
    TAG_VIEW_GLOBAL_BPP, 1,
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

  s_pScreenshotVPort->pPalette[0] = 0x0000;
  s_pScreenshotVPort->pPalette[1] = 0x0888;
  s_pScreenshotVPort->pPalette[2] = 0x0800;
  for (UBYTE i = 3; i < 32; ++i) {
    s_pScreenshotVPort->pPalette[i] = ((UWORD)i << 8) | (i << 4) | i;
  }
  viewUpdateGlobalPalette(s_pScreenshotVPort->pView);

  s_pListVPort = vPortCreate(0,
    TAG_VPORT_VIEW, s_pView,
    TAG_VPORT_BPP, 5,
    TAG_END
  );
  s_pListBufferManager = simpleBufferCreate(0,
    TAG_SIMPLEBUFFER_VPORT, s_pListVPort,
    TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
    TAG_SIMPLEBUFFER_IS_DBLBUF, 0,
    TAG_SIMPLEBUFFER_USE_X_SCROLLING, 0,
    TAG_SIMPLEBUFFER_BOUND_HEIGHT, MAX(FONTHEIGHT * s_ubGameCount, 150),
    TAG_END
  );
  
  // at the end of the picture, set the first two colors to just black and FG
  s_pListColorsBlock = copBlockCreate(s_pListVPort->pView->pCopList, 2, 254, s_pView->ubPosY + s_pScreenshotVPort->uwOffsY + s_pScreenshotVPort->uwHeight - 1);
  copMove(s_pListVPort->pView->pCopList, s_pListColorsBlock, &g_pCustom->color[0], 0x0000);
  copMove(s_pListVPort->pView->pCopList, s_pListColorsBlock, &g_pCustom->color[1], 0x0888);
  
  // Setup block to change background color for selected element
  s_pSelectedColorsBlock = copBlockCreate(s_pListVPort->pView->pCopList, 1, 0, 0);
  copMove(s_pListVPort->pView->pCopList, s_pSelectedColorsBlock, &g_pCustom->color[1], 0x0d00);
  s_pUnselectedColorsBlock = copBlockCreate(s_pListVPort->pView->pCopList, 1, 0, 0);
  copMove(s_pListVPort->pView->pCopList, s_pUnselectedColorsBlock, &g_pCustom->color[1], 0x0888);
  
  // at the top of the picture, set the first two colors to what they need to be
  s_pPicColorsBlock = copBlockCreate(s_pListVPort->pView->pCopList, 2, 0, 0);
  copMove(s_pListVPort->pView->pCopList, s_pPicColorsBlock, &g_pCustom->color[0], s_pScreenshotVPort->pPalette[0]);
  copMove(s_pListVPort->pView->pCopList, s_pPicColorsBlock, &g_pCustom->color[1], s_pScreenshotVPort->pPalette[1]);
  
  cameraSetCoord(s_pListBufferManager->pCamera, 0, 0);
  tFont *pFont = fontCreateFromMem(S_PFONTDATA);
  tTextBitMap *pTextBitMap = fontCreateTextBitMap(320, FONTHEIGHT);
  for (UBYTE i = 0; i < s_ubGameCount; i++) {
    fontDrawStr(pFont, s_pListBufferManager->pBack, 0, i * FONTHEIGHT + 1, s_ppGameNames[i], 1, FONT_LEFT, pTextBitMap);
  }
  fontDestroyTextBitMap(pTextBitMap);
  fontDestroy(pFont);
  changeSelection();

  viewLoad(s_pView);
}

void genericProcess(void) {
  keyProcess();
  joyProcess();
  static UBYTE s_ubInitialLoad = 0;
  if (!s_ubInitialLoad) {
    s_ubInitialLoad = 1;
    loadBitmap();
  } else if (s_ubTimer++ % 16 == 0) {
    // only check input every 16 frames
    if (keyCheck(KEY_ESCAPE)) {
      gameExit();
    } else if (keyCheck(KEY_RETURN) || keyCheck(KEY_NUMENTER) || joyCheck(JOY1_FIRE) || joyCheck(JOY2_FIRE)) {
      tFile *f = diskFileOpen(SCRIPTNAME, "w");
      if (f) {
        fileWrite(f, ";", 1);
        char buf[4];
        sprintf(buf, "%d", s_ubSelectedGame);
        fileWrite(f, buf, strlen(buf));
        fileWrite(f, "\n", 1);
        fileWrite(f, s_ppGameCommandLines[s_ubSelectedGame], strlen(s_ppGameCommandLines[s_ubSelectedGame]));
        fileClose(f);
      } else {
        logWrite("ERROR: Could not open " SCRIPTNAME " for writing.");
      }
      gameExit();
    } else if (joyCheck(JOY1_UP)) {
      if (s_ubSelectedGame > 0) {
        s_ubSelectedGame--;
        if (s_ubSelectedGame * FONTHEIGHT < s_pListBufferManager->pCamera->uPos.uwY) {
          cameraMoveBy(s_pListBufferManager->pCamera, 0, -FONTHEIGHT);
        }
        changeSelection();
        loadBitmap();
      }
    } else if (joyCheck(JOY1_DOWN)) {
      if (s_ubSelectedGame < s_ubGameCount - 1) {
        s_ubSelectedGame++;
        if (s_ubSelectedGame * FONTHEIGHT + FONTHEIGHT >= s_pListBufferManager->pCamera->uPos.uwY + s_pListVPort->uwHeight) {
          cameraMoveBy(s_pListBufferManager->pCamera, 0, FONTHEIGHT);
        }
        changeSelection();
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
  copBlockDestroy(s_pView->pCopList, s_pListColorsBlock);
  copBlockDestroy(s_pView->pCopList, s_pSelectedColorsBlock);
  copBlockDestroy(s_pView->pCopList, s_pUnselectedColorsBlock);
  copBlockDestroy(s_pView->pCopList, s_pPicColorsBlock);

  for (UBYTE i = 0; i < s_ubGameCount; i++) {
    memFree(s_ppGameNames[i], strlen(s_ppGameNames[i]) + 1);
    memFree(s_ppGameCommandLines[i], strlen(s_ppGameCommandLines[i]) + 1);
    memFree(s_ppGameImages[i], strlen(s_ppGameImages[i]) + 1);
  }
  memFree(s_ppGameNames, sizeof(char *) * s_ubGameCount);
  memFree(s_ppGameCommandLines, sizeof(char *) * s_ubGameCount);
  memFree(s_ppGameImages, sizeof(char *) * s_ubGameCount);
  viewDestroy(s_pView);
  keyDestroy(); // We don't need it anymore
  joyClose(); // We don't need it anymore
}
