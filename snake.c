//=====================================================SNAKE======================================================
#include "basic.h"

//=================================================intro_screen===================================================
void intro_screen() {
  COLOR(WHITE, BLACK);
  CLS(' ');

  while (UPDATE()) {
  }
}

//=====================================================main=======================================================
int main(int, char *[]) {
  START("Snake");
  COLOR(WHITE, BLUE);
  CLS(' ');

  LOCATE(77, 1);
  PRINT("Hello\nworld!");
  // PLAY("MBT160O1L8CDEDCDL4ECC");
  // PLAY("T160O1>L20CDEDCDL10ECC");
  //PLAY("T100MBO0L4>>CP64CP64CE");
  BEEP();
  while (UPDATE())
    ;
  exit(EXIT_SUCCESS);

  intro_screen();

  int player_x = 10;
  int player_y = 10;
  SET_CHAR(player_x, player_y, 2);

  COLOR(5, 0);
  LOCATE(20, 20);
  PRINT("Hello, world! \x02");

  while (UPDATE()) {
    int nx = player_x + ISKEYJUST(SDLK_RIGHT) - ISKEYJUST(SDLK_LEFT);
    int ny = player_y + ISKEYJUST(SDLK_DOWN) - ISKEYJUST(SDLK_UP);

    if (nx < 0)
      nx = 0;
    if (nx >= SCREEN_WIDTH)
      nx = SCREEN_WIDTH - 1;
    if (ny < 0)
      ny = 0;
    if (ny >= SCREEN_HEIGHT)
      ny = SCREEN_HEIGHT - 1;

    if (nx != player_x || ny != player_y) {
      SET_CHAR(player_x, player_y, ' ');
      SET_CHAR(nx, ny, 2);
      player_x = nx;
      player_y = ny;
    }
  }

  END();
}
