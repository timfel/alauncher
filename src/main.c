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
#define BPP 5

static tView *s_pView;
static tVPort *s_pScreenshotVPort;
static tSimpleBufferManager *s_pScreenshotBufferManager;
static tVPort *s_pListVPort;
static tSimpleBufferManager *s_pListBufferManager;
static tCopBlock *s_pListColorsBlock;
static tCopBlock *s_pSelectedColorsBlock;
static tCopBlock *s_pUnselectedColorsBlock;
static tCopBlock *s_pPicColorsBlock;

static UBYTE s_idxToKey[] = {
  KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0,
  KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
  KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
  KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z, KEY_SPACE,
};
static UBYTE s_idxToLowerChar[] = {
  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
  'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j',
  'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r', 's', 't',
  'u', 'v', 'w', 'x', 'y', 'z', ' ',
};
static UBYTE s_idxToUpperChar[] = {
  '1', '2', '3', '4', '5', '6', '7', '8', '9', '0',
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J',
  'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R', 'S', 'T',
  'U', 'V', 'W', 'X', 'Y', 'Z', ' ',
};

static UBYTE s_ubTimer = 0;

#define KEY_FILTER_COUNT 8
static UBYTE s_ubFilterLen = 0;
static UBYTE s_pFilterKeys[KEY_FILTER_COUNT] = {0};
static UBYTE s_ubFilterResetTimer = 0;
static UBYTE s_ubReloadBitmap = 0;

static UWORD s_uwGameCount = 0;
static UWORD s_uwSelectedGame = 0;
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
  for (LONG rem = lSize; rem > 0;) {
    ULONG bytesRead = fileRead(config, contents + (lSize - rem), rem);
    rem -= bytesRead;
    if (!bytesRead) {
      break;
    }
  }
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
          if (s_uwGameCount == 0) {
            s_ppGameNames = memAllocFast(sizeof(char *));
            s_ppGameCommandLines = memAllocFast(sizeof(char *));
            s_ppGameImages = memAllocFast(sizeof(char *));
          } else {
            s_ppGameNames = (char**)memReallocFast((char*)s_ppGameNames, sizeof(char *) * s_uwGameCount, sizeof(char *) * (s_uwGameCount + 1));
            s_ppGameCommandLines = (char**)memReallocFast((char*)s_ppGameCommandLines, sizeof(char *) * s_uwGameCount, sizeof(char *) * (s_uwGameCount + 1));
            s_ppGameImages = (char**)memReallocFast((char*)s_ppGameImages, sizeof(char *) * s_uwGameCount, sizeof(char *) * (s_uwGameCount + 1));
          }
          s_ppGameNames[s_uwGameCount] = memAllocFast(sizeof(char) * (i + 1));
          strcpy(s_ppGameNames[s_uwGameCount], line);
          s_uwGameCount++;
          i = 0;
          state = 1;
        } else if (state == 1) {
          s_ppGameCommandLines[s_uwGameCount - 1] = memAllocFast(sizeof(char) * (i + 1));
          strcpy(s_ppGameCommandLines[s_uwGameCount - 1], line);
          i = 0;
          state = 2;
        } else if (state == 2) {
          s_ppGameImages[s_uwGameCount - 1] = memAllocFast(sizeof(char) * (i + 1));
          strcpy(s_ppGameImages[s_uwGameCount - 1], line);
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
  tFile *f = NULL;
  char *chunk = NULL;
  if (!diskFileExists(filename)) {
    logWrite("Cannot open file: %s\n", filename);
    goto error;
  }
  f = diskFileOpen(filename, "r");
  if (!f) {
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
  
  // UWORD yOff = (s_pScreenshotBufferManager->uBfrBounds.uwY - height) / 2;
  // UWORD xOff = (s_pScreenshotBufferManager->uBfrBounds.uwX - width) / 2 / 16;
  // UWORD offs = yOff * s_pScreenshotBufferManager->pBack->BytesPerRow + xOff;
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
  char *filename = s_ppGameImages[s_uwSelectedGame];
  if (loadIlbm(filename)) {
    // image loaded, all fine
  } else {
    blitUnsafeRect(s_pScreenshotBufferManager->pBack,
      0, 0,
      s_pScreenshotBufferManager->uBfrBounds.uwX, s_pScreenshotBufferManager->uBfrBounds.uwY,
      1);
    blitLine(s_pScreenshotBufferManager->pBack,
      1, 1,
      s_pScreenshotBufferManager->uBfrBounds.uwX - 1, s_pScreenshotBufferManager->uBfrBounds.uwY - 1,
      0, 0xffff, 0);
    blitLine(s_pScreenshotBufferManager->pBack,
      1, s_pScreenshotBufferManager->uBfrBounds.uwY - 1,
      s_pScreenshotBufferManager->uBfrBounds.uwX - 1, 1,
      0, 0xffff, 0);
  }
}

static void changeSelection(UWORD uwNewSelection) {
  if (s_uwSelectedGame == uwNewSelection) {
    return;
  }
  s_uwSelectedGame = uwNewSelection;

  if (s_uwSelectedGame * FONTHEIGHT + FONTHEIGHT >= s_pListBufferManager->pCamera->uPos.uwY + s_pListVPort->uwHeight) {
    cameraSetCoord(s_pListBufferManager->pCamera, 0, s_uwSelectedGame * FONTHEIGHT - s_pListVPort->uwHeight + FONTHEIGHT);
  }
  if (s_uwSelectedGame * FONTHEIGHT < s_pListBufferManager->pCamera->uPos.uwY) {
    cameraSetCoord(s_pListBufferManager->pCamera, 0, s_uwSelectedGame * FONTHEIGHT);
  }

  UWORD baseOffset = s_pView->ubPosY + s_pScreenshotVPort->uwOffsY + s_pScreenshotVPort->uwHeight + \
      s_uwSelectedGame * FONTHEIGHT - s_pListBufferManager->pCamera->uPos.uwY;
  copBlockWait(s_pListVPort->pView->pCopList, s_pSelectedColorsBlock, 0, baseOffset);
  copBlockWait(s_pListVPort->pView->pCopList, s_pUnselectedColorsBlock, 0, baseOffset + FONTHEIGHT);

  s_ubReloadBitmap = 15;
}

static UWORD loadPosition(void) {
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
  UWORD uwPos = 0;
  if (position[0] == ';') {
    for (UBYTE i = 1; i < 4; i++) {
      if (position[i] >= '0' && position[i] <= '9') {
        uwPos *= 10;
        uwPos += (position[i] - '0');
      } else {
        break;
      }
    }
    if (uwPos >= s_uwGameCount) {
      uwPos = 0;
    }
  }
  return uwPos;
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

  keyCreate(); // We'll use keyboard
  joyOpen(); // We'll use joystick

  s_pView = viewCreate(0,
    TAG_VIEW_WINDOW_HEIGHT, 200,
    TAG_VIEW_GLOBAL_BPP, 1,
    TAG_END);

  s_pScreenshotVPort = vPortCreate(0,
    TAG_VPORT_VIEW, s_pView,
    TAG_VPORT_BPP, BPP,
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
    TAG_VPORT_BPP, BPP,
    TAG_END
  );
  s_pListBufferManager = simpleBufferCreate(0,
    TAG_SIMPLEBUFFER_VPORT, s_pListVPort,
    TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
    TAG_SIMPLEBUFFER_IS_DBLBUF, 0,
    TAG_SIMPLEBUFFER_USE_X_SCROLLING, 0,
    TAG_SIMPLEBUFFER_BOUND_HEIGHT, MAX(FONTHEIGHT * s_uwGameCount, 150),
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
  for (UBYTE i = 0; i < s_uwGameCount; i++) {
    fontDrawStr(pFont, s_pListBufferManager->pBack, 0, i * FONTHEIGHT + 1, s_ppGameNames[i], 1, FONT_LEFT, pTextBitMap);
  }
  fontDestroyTextBitMap(pTextBitMap);
  fontDestroy(pFont);
  s_uwSelectedGame = (UWORD)-1;
  changeSelection(loadPosition());

  viewLoad(s_pView);
}

void genericProcess(void) {
  keyProcess();
  joyProcess();
  ++s_ubTimer;
  static UBYTE s_ubInitialLoad = 0;
  if (!s_ubInitialLoad) {
    s_ubInitialLoad = 1;
    loadBitmap();
  } else {
    if (s_ubReloadBitmap && !(--s_ubReloadBitmap)) {
      loadBitmap();
    }
    if (keyUse(KEY_ESCAPE)) {
      gameExit();
    } else if (keyUse(KEY_RETURN) || keyUse(KEY_NUMENTER) || joyCheck(JOY1_FIRE) || joyCheck(JOY2_FIRE)) {
      tFile *f = diskFileOpen(SCRIPTNAME, "w");
      if (f) {
        fileWrite(f, ";", 1);
        char buf[8] = {0};
        sprintf(buf, "%d", s_uwSelectedGame);
        fileWrite(f, buf, strlen(buf));
        fileWrite(f, "\n", 1);
        fileWrite(f, s_ppGameCommandLines[s_uwSelectedGame], strlen(s_ppGameCommandLines[s_uwSelectedGame]));
        fileClose(f);
      } else {
        logWrite("ERROR: Could not open " SCRIPTNAME " for writing.");
      }
      gameExit();
    } else if (keyUse(KEY_UP) || (s_ubTimer % 16 && joyCheck(JOY1_UP))) {
      if (s_uwSelectedGame > 0) {
        changeSelection(s_uwSelectedGame - 1);
      } else {
        changeSelection(s_uwGameCount - 1);
      }
      s_ubFilterLen = 0;
    } else if (keyUse(KEY_DOWN) || (s_ubTimer % 16 && joyCheck(JOY1_DOWN))) {
      if (s_uwSelectedGame < s_uwGameCount - 1) {
        changeSelection(s_uwSelectedGame + 1);
      } else {
        changeSelection(0);
      }
      s_ubFilterLen = 0;
    } else if (keyUse(KEY_BACKSPACE)) {
      if (s_ubFilterLen) {
        s_pFilterKeys[--s_ubFilterLen] = 0;
      }
    } else {
      if (++s_ubFilterResetTimer == 0) {
        s_ubFilterLen = 0;
      }
      for (UBYTE i = 0; i < sizeof(s_idxToKey); i++) {
        if (s_ubFilterLen >= KEY_FILTER_COUNT) {
          break;
        }
        if (keyUse(s_idxToKey[i])) {
          s_ubFilterResetTimer = 0;
          s_pFilterKeys[s_ubFilterLen++] = i;
          UBYTE done = 0;
          for (UBYTE j = 0; j < s_uwGameCount && !done; j++) {
            for (UBYTE k = 0; k < s_ubFilterLen; k++) {
              if (s_ppGameNames[j][k] == s_idxToLowerChar[s_pFilterKeys[k]] || s_ppGameNames[j][k] == s_idxToUpperChar[s_pFilterKeys[k]]) {
                if (k == s_ubFilterLen - 1) {
                  changeSelection(j);
                  done = 1;
                  break;
                }
              } else {
                break;
              }
            }
          }
        }
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

  for (UBYTE i = 0; i < s_uwGameCount; i++) {
    memFree(s_ppGameNames[i], strlen(s_ppGameNames[i]) + 1);
    memFree(s_ppGameCommandLines[i], strlen(s_ppGameCommandLines[i]) + 1);
    memFree(s_ppGameImages[i], strlen(s_ppGameImages[i]) + 1);
  }
  memFree(s_ppGameNames, sizeof(char *) * s_uwGameCount);
  memFree(s_ppGameCommandLines, sizeof(char *) * s_uwGameCount);
  memFree(s_ppGameImages, sizeof(char *) * s_uwGameCount);
  viewDestroy(s_pView);
  keyDestroy(); // We don't need it anymore
  joyClose(); // We don't need it anymore
}
