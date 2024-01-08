#include <ace/generic/main.h>
#include <ace/utils/font.h>
#include <ace/managers/joy.h>
#include <ace/managers/key.h>
#include <ace/managers/state.h>
#include <ace/managers/log.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/utils/palette.h>
#include <ace/utils/custom.h>
#include <ace/managers/blit.h>
#include <ace/macros.h>

#include "stfont.h"

#define FONTHEIGHT 14
#define FONTMAXCHAR 122
#define FONTMINCHAR 31
#define CONFIGFILE "acelauncher.config"

static tView *s_pView;
static tVPort *s_pScreenshotVPort;
static tSimpleBufferManager *s_pScreenshotBufferManager;
static tCopBlock *s_pScreenshotCopBlock;
static tVPort *s_pListVPort;
static tSimpleBufferManager *s_pListBufferManager;
static tCopBlock *s_pListCopBlock;
static tFont *s_pFont;

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
  tFile *config = fileOpen(CONFIGFILE, "r");
  if (!config) {
    logWrite("Failed to open config file\n");
    gameExit();
    return 0;
  }
  char line[256];
  UBYTE state = 0;
  UBYTE i = 0;
  char c = 1;
  while (c) {
    if (!fileRead(config, &c, sizeof(c))) {
      c = 0; // Force end
    }
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
      line[i++] = c;
    } else {
      continue;
    }
  }
  fileClose(config);
  return 1;
}

static void loadBitmap(void) {
  char *filename = s_ppGameImages[s_ubSelectedGame];
  if (filename && fileExists(filename)) {
    blitUnsafeRect(s_pScreenshotBufferManager->pBack,
      0, 0,
      s_pScreenshotBufferManager->uBfrBounds.uwX - 1, s_pScreenshotBufferManager->uBfrBounds.uwY - 1,
      0);
    bitmapLoadFromFile(s_pScreenshotBufferManager->pBack, filename, 0, 0);
  } else {
    blitUnsafeRect(s_pScreenshotBufferManager->pBack,
      0, 0,
      s_pScreenshotBufferManager->uBfrBounds.uwX - 1, s_pScreenshotBufferManager->uBfrBounds.uwY - 1,
      1);
    blitLine(s_pScreenshotBufferManager->pBack,
      0, 0,
      s_pScreenshotBufferManager->uBfrBounds.uwX - 1, s_pScreenshotBufferManager->uBfrBounds.uwY - 1,
      2, 0xffff, 0);
    blitLine(s_pScreenshotBufferManager->pBack,
      0, s_pScreenshotBufferManager->uBfrBounds.uwY - 1,
      s_pScreenshotBufferManager->uBfrBounds.uwX - 1, 0,
      2, 0xffff, 0);
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

void genericCreate(void) {
  logWrite("Hello, Amiga!\n");
  if (!loadConfig()) {
    return;
  }

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
  s_pScreenshotCopBlock = copBlockCreate(s_pView->pCopList, 5, 0, 0);
  copMove(s_pView->pCopList, s_pScreenshotCopBlock, &g_pCustom->bplcon0, (s_pScreenshotVPort->ubBPP << 12) | BV(9));
  copMove(s_pView->pCopList, s_pScreenshotCopBlock, &g_pCustom->color[0], 0x0000);
  copMove(s_pView->pCopList, s_pScreenshotCopBlock, &g_pCustom->color[1], 0x0888);
  copMove(s_pView->pCopList, s_pScreenshotCopBlock, &g_pCustom->color[2], 0x0800);
  copMove(s_pView->pCopList, s_pScreenshotCopBlock, &g_pCustom->color[3], 0x0080);
  copMove(s_pView->pCopList, s_pScreenshotCopBlock, &g_pCustom->color[4], 0x0008);
  loadBitmap();

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
  s_pListCopBlock = copBlockCreate(s_pView->pCopList, 5, 0, s_pView->ubPosY + s_pListVPort->uwOffsY);
  copMove(s_pView->pCopList, s_pListCopBlock, &g_pCustom->bplcon0, (s_pListVPort->ubBPP << 12) | BV(9));
  copMove(s_pView->pCopList, s_pListCopBlock, &g_pCustom->color[0], 0x0000);
  copMove(s_pView->pCopList, s_pListCopBlock, &g_pCustom->color[1], 0x0800);
  for (UBYTE i = 0; i < s_ubGameCount; i++) {
    fontDrawStr1bpp(s_pFont, s_pListBufferManager->pBack, 0, i * lineHeight, s_ppGameNames[i]);
  }
  invertSelectedGameString();

  viewLoad(s_pView);
  systemUnuse();
}

void genericProcess(void) {
  keyProcess();
  joyProcess();
  if (keyCheck(KEY_ESCAPE)) {
    gameExit();
  }
  if (keyCheck(KEY_NUMENTER) || keyCheck(KEY_RETURN) || joyCheck(JOY1_FIRE) || joyCheck(JOY2_FIRE)) {
    gameExit();
  }
  if (keyCheck(KEY_UP) || joyCheck(JOY1_UP)|| joyCheck(JOY2_UP)) {
    if (s_ubSelectedGame > 0) {
      invertSelectedGameString();
      s_ubSelectedGame--;
      invertSelectedGameString();
      if (s_ubSelectedGame * FONTHEIGHT < s_pListBufferManager->pCamera->uPos.uwY) {
        cameraMoveBy(s_pListBufferManager->pCamera, 0, -FONTHEIGHT);
      }
      loadBitmap();
    }
  }
  if (keyCheck(KEY_DOWN) || joyCheck(JOY1_DOWN)|| joyCheck(JOY2_DOWN)) {
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
  viewProcessManagers(s_pView);
  copProcessBlocks();
  vPortWaitUntilEnd(s_pListVPort);
}

void genericDestroy(void) {
  systemUse();
  copBlockDestroy(s_pView->pCopList, s_pScreenshotCopBlock);
  copBlockDestroy(s_pView->pCopList, s_pListCopBlock);
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
  logWrite("Goodbye, Amiga!\n");
}
