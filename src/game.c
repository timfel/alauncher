#include "game.h"
#include <ace/managers/key.h> // We'll use key* fns here
#include <ace/managers/game.h> // For using gameExit
#include <ace/managers/system.h> // For systemUnuse and systemUse

// "Gamestate" is a long word, so let's use shortcut "Gs" when naming fns

void gameGsCreate(void) {
  // Initializations for this gamestate - load bitmaps, draw background, etc.
  // We don't need anything here right now except for unusing OS
  systemUnuse();
}

void gameGsLoop(void) {
  // This will loop forever until you "pop" or change gamestate
  // or close the game
  if(keyCheck(KEY_ESCAPE)) {
    gameExit();
  }
  else {
    // Process loop normally
    // We'll come back here later
  }
}

void gameGsDestroy(void) {
  systemUse();
  // Cleanup when leaving this gamestate
  // Empty at the moment except systemUse
}

