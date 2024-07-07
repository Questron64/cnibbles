//=====================================================SNAKE======================================================
#include "screen.h"

//=====================================================main=======================================================
int main(int, char *[]) {
  if (init_screen()) {
    SDL_LogCritical(0, "init_screen failed: %s", SDL_GetError());
    exit(EXIT_FAILURE);
  }

  clear_screen_to((struct ScreenCell){.fg = WHITE, .bg = BLUE, .glyph = 2});
  while (update_screen()) {
    for (int i = 0; i < 100; i++)
      set_screen(                                //
          rand() % SCREEN_WIDTH,                 //
          rand() % SCREEN_HEIGHT,                //
          (struct ScreenCell){                   //
                              .fg = rand(),      //
                              .bg = rand(),      //
                              .glyph = rand()}); //
  }

  // quit_screen();
}
