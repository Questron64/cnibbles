//====================================================SCREEN======================================================
//-The screen is a fake VGA text mode screen in 80x25 text mode with 16 background and foreground colors. The
// CP437 character set is used, including graphical and box drawing characters.
//
// This also manages time in addition to the screen. Calling draw_screen() will wait until the next 1/60th of a
// second.
#include <SDL.h>

//====================================================CONFIG======================================================
#define WINDOW_TITLE "Screen"
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define FONT_WIDTH 9
#define FONT_HEIGHT 16

//===================================================CONSTANTS====================================================
enum ScreenColor {
  BLACK,
  BLUE,
  GREEN,
  CYAN,
  RED,
  MAGENTA,
  BROWN,
  LIGHT_GRAY,
  DARK_GREY,
  LIGHT_BLUE,
  LIGHT_GREEN,
  LIGHT_CYAN,
  LIGHT_RED,
  LIGHT_MAGENTA,
  YELLOW,
  WHITE
};

//=====================================================TYPES======================================================
struct ScreenCell {
  enum ScreenColor fg : 4;
  enum ScreenColor bg : 4;
  unsigned glyph : 8;
};

//===================================================FUNCTIONS====================================================
int init_screen();
void quit_screen();

SDL_Window *get_window();

void clear_screen();
void clear_screen_to(struct ScreenCell clearCell);
struct ScreenCell get_screen(int x, int y);
void set_screen(int x, int y, struct ScreenCell cell);

int update_screen();
