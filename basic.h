//====================================================BASIC======================================================
//-A QBasic-like library with VGA display and PC speaker.
//
//============================================DIFFERENCES FROM QBASIC=============================================
//*Anything that was 1-based in QBasic is now 0-based. Namely, the screen coordinates are 0-based, which makes
// LOCATE(0,0) go to the top left of the screen, instead of QBasic's LOCATE(1,1).
//*Anything that requires a time parameter doesn't use ticks or milliseconds, but frames at a constant 60Hz.
#include <SDL.h>

//====================================================CONFIG======================================================
#define WINDOW_WIDTH 800
#define WINDOW_HEIGHT 600

#define SCREEN_WIDTH 80
#define SCREEN_HEIGHT 25
#define FONT_WIDTH 9
#define FONT_HEIGHT 16

#define TYPOMATIC_DELAY 20
#define TYPOMATIC_INTERVAL 5

//===================================================CONSTANTS====================================================
enum {
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
typedef int I;         // Short names for common types
typedef Sint8 I8;      //
typedef Sint16 I16;    //
typedef Sint32 I32;    //
typedef Sint64 I64;    //
typedef Uint8 U8;      //
typedef Uint16 U16;    //
typedef Uint32 U32;    //
typedef Uint64 U64;    //
typedef char C;        //
typedef const char CC; //
typedef double D;      //
typedef unsigned U;    //
typedef void V;        //
typedef size_t Z;      //

typedef struct { // A CELL is a character on the screen, including it background and foreground colors
  U fg : 4;      //
  U bg : 4;      //
  C glyph;       //
} CELL;          //

typedef SDL_Keycode KEY;

//===================================================FUNCTIONS====================================================
V START(const C *window_title); // START must be called at the beginnig of all programs
I UPDATE();                     // Update must be called at the beginning of every frame
V END();                        // END must be called at the end of the program

V BEEP();                            // Produce a beep on the speaker
V CLS(I c);                          // Clear the screen using the cursor color and provided character
V COLOR(I fg, I bg);                 // Set the color that PRINT and CLS will use
CELL GET(I x, I y);                  // Get screen cell at x,y
KEY INKEY();                         // Reads a keypress from the keyboard, or returns if none pressed
V INPUT(I size, C buf[size]);        // Reads input from the keyboard
I ISKEY(KEY k);                      // Is KEY pressed?
I ISKEYJUST(KEY k);                  // Was KEY just pressed this frame?
I ISNOKEY(KEY k);                    // Is KEY released?
I ISNOKEYJUST(KEY k);                // Was KEY just released this frame?
V LOCATE(I x, I y);                  // Positions the cursor on the screen
V LOCATEREL(I x, I y);               // Move the cursor relative to current position
V PLAY(const C *song);               // Play a song, returns immediately
V PLAY_OFF();                        // Disables music
V PLAY_ON();                         // Enabled music
V PLAY_START();                      // Starts music
V PLAY_STOP();                       // Stops music
V POS(I *x, I *y);                   // Get the position of the cursor
V PRINT(const C *format, ...);       // Prints string to the screen at POS
V PRINTRAW(const C *format, ...);    // Prints string to the screen, ignoring any control characters
I RANDOM(I min, I max);              // Return a random number between min and max, inclusive
V RANDOMIZE(I n);                    // Initialize the random number generator
V SET(I x, I y, CELL c);             // Set cell at x,y
V SET_CHAR(I x, I y, C c);           // Set cell at x,y with char c, but preserve color
V SOUND(I freq, D dur);              // Play a sound at frequency for duration frames
I STICK(I param);                    // Returns the coordinates of a joystick
V TIMER();                           // Get the current value of the timer, which increments every frame
V TIMER_OFF();                       // Turn timers off
V TIMER_ON();                        // Turns timers on
V TIMER_RESET();                     // Deletes all active timers
V TIMER_SET(I period, V (*func)(V)); // Set a timer callback
V WAIT(I dur);                       // Wait for dur frames
