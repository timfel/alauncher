#include <ace/generic/main.h>
#include <ace/utils/font.h>
#include <ace/managers/key.h>
#include <ace/managers/state.h>
#include <ace/managers/log.h>
#include <ace/managers/viewport/simplebuffer.h>
#include <ace/managers/viewport/scrollbuffer.h>

static tView *s_pView;
static tVPort *s_pScreenshotVPort;
static tSimpleBufferManager *s_pScreenshotBufferManager;
static tVPort *s_pListVPort;
static tScrollBufferManager *s_pListBufferManager;
static tFont *s_pFont;

static UBYTE s_ubGameCount = 0;
static char **s_ppGameNames;
static char **s_ppGameCommandLines;
static char **s_ppGameImages;

static char *memReallocFast(char *ptr, UWORD oldSize, UWORD size) {
  char *newPtr = memAllocFast(size);
  memcpy(newPtr, ptr, size);
  memFree(ptr, oldSize);
  return newPtr;
}

static tBitMap readBMP(char *filename) {

}

static UBYTE loadConfig(void) {
  tFile *config = fileOpen("acelauncher.config", "r");
  if (!config) {
    logWrite("Failed to open config file\n");
    gameExit();
    return 0;
  }
  char line[256];
  UBYTE state = 0;
  UBYTE i = 0;
  char c;
  while (fileRead(config, &c, sizeof(c))) {
    if (c == '\r') {
      continue;
    } else if (c == '\n') {
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
          s_ppGameCommandLines[s_ubGameCount] = memAllocFast(sizeof(char) * (i + 1));
          strcpy(s_ppGameCommandLines[s_ubGameCount - 1], line);
          i = 0;
          state = 2;
        } else if (state == 2) {
          s_ppGameImages[s_ubGameCount] = memAllocFast(sizeof(char) * (i + 1));
          strcpy(s_ppGameImages[s_ubGameCount - 1], line);
          i = 0;
          state = 0;
        }
      }
    } else if (i < sizeof(line) - 1) {
      line[i] = c;
    } else {
      continue;
    }
  }
  fileClose(config);
  return 1;
}

void genericCreate(void) {
  logWrite("Hello, Amiga!\n");
  if (!loadConfig()) {
    return;
  }

  keyCreate(); // We'll use keyboard

  s_pView = viewCreate(0, TAG_END);

  s_pScreenshotVPort = vPortCreate(0,
    TAG_VPORT_VIEW, s_pView,
    TAG_VPORT_BPP, 5,
    TAG_VPORT_HEIGHT, 100,
    TAG_END
  );
  s_pScreenshotBufferManager = simpleBufferCreate(0,
    TAG_SIMPLEBUFFER_VPORT, s_pScreenshotVPort,
    TAG_SIMPLEBUFFER_BITMAP_FLAGS, BMF_CLEAR | BMF_INTERLEAVED,
    TAG_SIMPLEBUFFER_IS_DBLBUF, 0,
    TAG_SIMPLEBUFFER_USE_X_SCROLLING, 0,
    TAG_END
  );

  s_pListVPort = vPortCreate(0,
    TAG_VPORT_VIEW, s_pView,
    TAG_VPORT_BPP, 1,
    TAG_VPORT_HEIGHT, 100,
    TAG_END
  );
  s_pFont = fontCreate("topaz.font");
  tUwCoordYX fontSize = fontMeasureText(s_pFont, "A");
  UWORD fontHeight = fontSize.uwY;
  UWORD lineHeight = fontHeight + 2 * (fontHeight * 8 / 7);
  s_pListBufferManager = scrollBufferCreate(0,
    TAG_SCROLLBUFFER_VPORT, s_pListVPort,
    TAG_SCROLLBUFFER_BITMAP_FLAGS, BMF_CLEAR,
    TAG_SCROLLBUFFER_IS_DBLBUF, 0,
    TAG_SCROLLBUFFER_BOUND_HEIGHT, lineHeight * s_ubGameCount,
    TAG_SCROLLBUFFER_BOUND_WIDTH, 320,
    TAG_END
  );
  for (UBYTE i = 0; i < s_ubGameCount; i++) {
    fontDrawStr1bpp(s_pFont, s_pListBufferManager->pBack, 0, i * lineHeight, s_ppGameNames[i]);
  }
}

void genericProcess(void) {
  keyProcess();
  if (keyCheck(KEY_RETURN)) {
    gameExit();
  }
}

void genericDestroy(void) {
  viewDestroy(s_pView);
  fontDestroy(s_pFont);
  keyDestroy(); // We don't need it anymore
  logWrite("Goodbye, Amiga!\n");
}
