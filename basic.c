#include "basic.h"
#include <stdlib.h>
#include <time.h>

//===================================================CONSTANTS====================================================
#define WAVETABLE_SIZE 128
#define WAVETABLE_MASK 0x7F

#define DEFAULT_TEMPO 100
#define DEFAULT_OCTAVE 4

enum { MUSIC_LEGATO, MUSIC_NORMAL, MUSIC_STACCATO };

//====================================================STATICS=====================================================
static SDL_Window *window;     // SDL stuff
static SDL_Renderer *renderer; //
static SDL_bool window_closed; // Has the window been closed?

static SDL_Texture *fontTex;      // Textures for rendering the screen
static SDL_Texture *screenTex;    //
static SDL_Texture *bigScreenTex; //

static CELL screen[SCREEN_HEIGHT][SCREEN_WIDTH];                // The screen itself. The verts are for
static SDL_Vertex colorVerts[SCREEN_WIDTH * SCREEN_HEIGHT * 6]; // SDL_RenderGeometry, which is much faster than
static SDL_Vertex glyphVerts[SCREEN_WIDTH * SCREEN_HEIGHT * 6]; // trying to use SDL_RenderCopy

static I cursor_x;  // Cursor position. These are 0-based indices into the screen array, be aware that the
static I cursor_y;  // user uses 1-based screen locations in functions such as LOCATE.
static I cursor_fg; // Cursor color, this is the color drawn to cells when a PRINT occurs.
static I cursor_bg;

static I timer;
// static Uint64 last_frame;

static U8 keys[SDL_NUM_SCANCODES];      // The keyboard state this frame
static U8 last_keys[SDL_NUM_SCANCODES]; // and last frame

static KEY key_buffer[1024]; // A circular buffer of all the keys pressed by the user
static Z key_buffer_start;   //
static Z key_buffer_end;     //

static const C font_bmp[];    // The font BMP. See bottom of file
static const Z font_bmp_size; //

static SDL_AudioSpec audio_spec; // Sound device properties
static I audio_device;           // Audio device ID

static const C *song = "";   // The current song
static I song_octave;        // Octave of next note, 0 - 6
static I song_tempo;         // Tempo in samples
static I song_tempo_div;     // Actual note length is tempo / tempo_div, for quater/half/etc notes
static I song_flow;          // Space between notes
static I song_note;          // Frequency of current note in Hz
static I song_note_duration; // Number of samples remaining in current note
static D song_sample;        // Current sample in the wavetable

// See DATA section for values
static const I16 wavetable[WAVETABLE_SIZE]; // The PC speaker wavetable
static const SDL_Color palette[16];         // The VGA color palette
static const I letter_to_note[256];         // Convert letter to a note
static const I note_frequency[8][12];       // Frequency of each note in each octave

//================================================bpm_to_samples==================================================
I bpm_to_samples(I bpm) { return (I)(1 / ((D)bpm / 60) * 4 * audio_spec.freq); }

//================================================audio_callback==================================================
static void audio_callback(void *, U8 *stream_, I len) {
  I16 *stream = (I16 *)stream_;
  len /= 2;

  I tries = 0; // Prevent accidental infinite loop
  for (I sample = 0; tries < 1000 && sample < len; tries++) {
    // Play a note if we have one to play
    if (song_note_duration != 0) {
      // If the note is 0 then this is a rest
      if (song_note == 0) {
        I d = SDL_min(song_note_duration, len - sample);
        SDL_memset(&stream[sample], 0, d * 2);
        song_note_duration -= d;
        sample += d;
      } else {
        D inc = WAVETABLE_SIZE / ((D)audio_spec.freq / song_note);
        for (; sample < len && song_note_duration > 0; sample++, song_note_duration--) {
          D t = song_sample - (I)song_sample;
          D v = (D)wavetable[(I)song_sample & WAVETABLE_MASK] * (1.0 - t) + //
                (D)wavetable[((I)song_sample + 1) & WAVETABLE_MASK] * t;
          stream[sample] = (I16)v;
          song_sample += inc;
          if (song_sample > WAVETABLE_SIZE)
            song_sample -= WAVETABLE_SIZE;
        }
      }
      continue;
    }

    while (isspace(*song))
      song++;

    switch (*song) {
    case 0: // End of song
      SDL_memset(&stream[sample], 0, (len - sample) * 2);
      sample = len;
      break;

    case 'O': // Change octave
    case 'o': {
      char *end;
      long octave = strtol(song + 1, &end, 10);
      if (end == song + 1) {
        SDL_LogError(0, "audio_callback: Invalid O command '%s'", song);
        song = "";
      } else {
        song_octave = SDL_clamp(octave, 0, 6);
        song = end;
      }
      break;
    }

    case '<': // Decrease octave
      song_octave = SDL_clamp(song_octave - 1, 0, 6);
      song++;
      break;
    case '>': // Increase octive
      song_octave = SDL_clamp(song_octave + 1, 0, 6);
      song++;
      break;

    case 'a': // Note
    case 'A':
    case 'b':
    case 'B':
    case 'c':
    case 'C':
    case 'd':
    case 'D':
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
      song_note = note_frequency[song_octave][letter_to_note[(I)*song]];
      song_note_duration = (I)((D)song_tempo / song_tempo_div);
      song++;

      switch (*song) {
      case '+':
        song_note++;
        song++;
        break;
      case '-':
        song_note--;
        song++;
        break;
      case '.':
        song_note_duration = (I)(song_note_duration * (2.0 / 3.0));
        song++;
      }
      break;

    case 'p': // Rest
    case 'P':
      char *end;
      long duration = strtol(song + 1, &end, 10);
      if (end == song + 1) {
        SDL_LogError(0, "audio_callback: Invalid P command '%s'", song);
        song = "";
      } else {
        song_note = 0;
        duration = SDL_clamp(duration, 1, 64);
        song_note_duration = (I)((D)song_tempo / duration);
        song = end;
      }
      break;

    case 'n': // Specific note
    case 'N': {
      char *end;
      long note = strtol(song + 1, &end, 10);
      if (end == song + 1) {
        SDL_LogError(0, "audio_callback: Invalid N command '%s'", song);
        song = "";
      } else {
        note = SDL_clamp(note, 0, 84);
        song_note = note_frequency[note / 12][note % 12];
        song_note_duration = (I)((D)song_tempo / song_tempo_div);
        song = end;
      }
      break;
    }

    case 'l': // Note length divisor
    case 'L': {
      char *end;
      long div = strtol(song + 1, &end, 10);
      if (end == song + 1) {
        SDL_LogError(0, "audio_callback: Invalid L command '%s'", song);
        song = "";
      } else {
        div = SDL_clamp(div, 1, 64);
        song_tempo_div = div;
        song = end;
      }
      break;
    }

    case 'm': // Music parameter
    case 'M':
      switch (song[1]) {
      case 'l': // Music legato
      case 'L':
        song_flow = MUSIC_LEGATO;
        song += 2;
        break;
      case 'n': // Music normal
      case 'N':
        song_flow = MUSIC_NORMAL;
        song += 2;
        break;
      case 's': // Music staccato
      case 'S':
        song_flow = MUSIC_STACCATO;
        song += 2;
        break;
      case 'f': // Music foreground (ignored)
      case 'F':
      case 'b': // Music background (ignored)
      case 'B':
        song += 2;
        break;
      default:
        SDL_LogError(0, "audio_callback: Invalid M command '%s'", song);
        song = "";
      }
      break;

    case 't': // Tempo
    case 'T': {
      char *end;
      long tempo = strtol(song + 1, &end, 10);
      if (end == song + 1) {
        SDL_LogError(0, "audio_callback: Invalid T command '%s'", song);
        song = "";
      }
      tempo = SDL_clamp(tempo, 32, 255);

      // The T command is in quarter notes per minute, convert this to samples per whole note
      song_tempo = (I)(audio_spec.freq / (1.0 / (tempo / 4.0 / 60.0)));
      song = end;
      break;
    }

    default: // Invalid song
      SDL_LogError(0, "audio_callback: Invalid command '%s'", song);
      song = "";
    }
  }

  if (tries == 1000) {
    SDL_LogError(0, "audio_callback: tries exhausted, something is wrong with the song");
    SDL_LogError(0, "%s", song);
    song = "";
  }
}

//=====================================================START======================================================
V START(const C *window_title) {
  srand(time(0));

  // Initialize SDL and create the window
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER | SDL_INIT_AUDIO)) {
    SDL_LogCritical(0, "START Failed to initialize SDL: %s", SDL_GetError());
    exit(EXIT_FAILURE);
  }

  window = SDL_CreateWindow(                          //
      window_title,                                   //
      SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, //
      WINDOW_WIDTH, WINDOW_HEIGHT,                    //
      SDL_WINDOW_RESIZABLE);                          //
  if (!window) {
    SDL_LogCritical(0, "START Failed to create window: %s", SDL_GetError());
    exit(EXIT_FAILURE);
  }

  renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_TARGETTEXTURE);
  if (!renderer) {
    SDL_LogCritical(0, "START Failed to create renderer: %s", SDL_GetError());
    exit(EXIT_FAILURE);
  }

  SDL_RenderSetLogicalSize(renderer, WINDOW_WIDTH, WINDOW_HEIGHT);
  SDL_SetWindowMinimumSize(window, WINDOW_WIDTH, WINDOW_HEIGHT);

  // Initialize the position of the color and glyph verts. Their colors and texture coordinates will be set
  // when the screen is cleared at the end of this function, but their positions never change and are set here.
  for (I y = 0, i = 0; y < SCREEN_HEIGHT; y++) {
    for (I x = 0; x < SCREEN_WIDTH; x++, i++) {
      const I W = FONT_WIDTH;
      const I H = FONT_HEIGHT;
      colorVerts[i * 6 + 0] = (SDL_Vertex){.position = {x * W, y * H}};
      colorVerts[i * 6 + 1] = (SDL_Vertex){.position = {x * W, y * H + H}};
      colorVerts[i * 6 + 2] = (SDL_Vertex){.position = {x * W + W, y * H + H}};

      colorVerts[i * 6 + 3] = (SDL_Vertex){.position = {x * W, y * H}};
      colorVerts[i * 6 + 4] = (SDL_Vertex){.position = {x * W + W, y * H + H}};
      colorVerts[i * 6 + 5] = (SDL_Vertex){.position = {x * W + W, y * H}};

      glyphVerts[i * 6 + 0] = (SDL_Vertex){.position = {x * W, y * H}};
      glyphVerts[i * 6 + 1] = (SDL_Vertex){.position = {x * W, y * H + H}};
      glyphVerts[i * 6 + 2] = (SDL_Vertex){.position = {x * W + W, y * H + H}};

      glyphVerts[i * 6 + 3] = (SDL_Vertex){.position = {x * W, y * H}};
      glyphVerts[i * 6 + 4] = (SDL_Vertex){.position = {x * W + W, y * H + H}};
      glyphVerts[i * 6 + 5] = (SDL_Vertex){.position = {x * W + W, y * H}};
    }
  }

  // Load the font
  {
    SDL_Surface *s = SDL_LoadBMP_RW(SDL_RWFromConstMem(font_bmp, (I)font_bmp_size), 1);
    if (!s) {
      SDL_LogCritical(0, "START Failed to load font: %s", SDL_GetError());
      exit(EXIT_FAILURE);
    }

    // Indexed BMP doesn't support alpha channel, so we need to set color 0 to transparent. But that still
    // doesn't fix it, we need to convert it to a 32-bit RGBA image or else the transparency is lost when
    // loading it into a texture
    SDL_SetPaletteColors(s->format->palette, &(SDL_Color){255, 255, 0, 0}, 0, 1);
    SDL_Surface *s2 = SDL_ConvertSurfaceFormat(s, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(s);
    if (!s2) {
      SDL_LogCritical(0, "START Failed to load font: %s", SDL_GetError());
      exit(EXIT_FAILURE);
    }

    fontTex = SDL_CreateTextureFromSurface(renderer, s2);
    SDL_FreeSurface(s2);
    if (!fontTex) {
      SDL_LogCritical(0, "START Failed to load font: %s", SDL_GetError());
      exit(EXIT_FAILURE);
    }
  }

  // Create the screen textures
  screenTex = SDL_CreateTexture(    //
      renderer,                     //
      SDL_PIXELFORMAT_RGBA32,       //
      SDL_TEXTUREACCESS_TARGET,     //
      FONT_WIDTH * SCREEN_WIDTH,    //
      FONT_HEIGHT * SCREEN_HEIGHT); //
  if (!screenTex) {
    SDL_LogCritical(0, "START Failed to create screen texture: %s", SDL_GetError());
    exit(EXIT_FAILURE);
  }

  {
    // Enable filtering for the big texture
    CC *oldQuality = SDL_GetHint(SDL_HINT_RENDER_SCALE_QUALITY);
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");

    bigScreenTex = SDL_CreateTexture(     //
        renderer,                         //
        SDL_PIXELFORMAT_RGBA32,           //
        SDL_TEXTUREACCESS_TARGET,         //
        FONT_WIDTH * SCREEN_WIDTH * 3,    //
        FONT_HEIGHT * SCREEN_HEIGHT * 3); //

    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, oldQuality);
    if (!bigScreenTex) {
      SDL_LogCritical(0, "START Failed to create big screen texture: %s", SDL_GetError());
      exit(EXIT_FAILURE);
    }
  }

  // Reset color and clear the screen
  COLOR(WHITE, BLACK);
  CLS(' ');

  // Create the audio device and start it playing
  audio_device = SDL_OpenAudioDevice(               //
      NULL, 0,                                      //
      &(SDL_AudioSpec){.freq = 44100,               //
                       .format = AUDIO_S16SYS,      //
                       .channels = 1,               //
                       .samples = 512,              //
                       .callback = audio_callback}, //
      &audio_spec,                                  //
      SDL_AUDIO_ALLOW_ANY_CHANGE);                  //
  if (audio_device == 0) {
    SDL_LogCritical(0, "Failed to open audio device: %s", SDL_GetError());
    exit(EXIT_FAILURE);
  }
  SDL_PauseAudioDevice(audio_device, 0);

  song_octave = DEFAULT_OCTAVE;
  // song_tempo = (I)((1.0 / (DEFAULT_TEMPO / 4.0 / 60.0)) * audio_spec.freq);
  // D tmp = 100;            // 100 BMP
  // tmp /= 60;              // 1.66667 BPS
  // tmp = 1 / tmp;          // 0.6 seconds per beat
  // tmp *= 4;               // 2.4 seconds per whole note
  // tmp *= audio_spec.freq; // 26,460 samples per whole note
  // song_tempo = (I)tmp;
  // song_tempo = (I)(1 / ((D)DEFAULT_TEMPO / 60) * 4 * audio_spec.freq);
  // printf("%d\n", song_tempo);
  song_tempo = bpm_to_samples(100);
  song_tempo_div = 4;
  song_flow = MUSIC_NORMAL;
}

//====================================================UPDATE======================================================
I UPDATE() {
  if (window_closed)
    return 0;

  // Update the keyboard
  {
    SDL_PumpEvents();
    SDL_memcpy(last_keys, keys, sizeof(keys));

    I nkeys;
    const U8 *k = SDL_GetKeyboardState(&nkeys);
    for (I i = 0; i < nkeys; i++) {
      if (k[i]) {
        keys[i] = last_keys[i] + 1;
        if (keys[i] >= TYPOMATIC_DELAY + TYPOMATIC_INTERVAL) {
          last_keys[i] = 0;
          keys[i] = TYPOMATIC_DELAY;
        }
      } else
        keys[i] = 0;
    }
  }

  // Process events
  for (SDL_Event ev; SDL_PollEvent(&ev);) {
    switch (ev.type) {
    case SDL_QUIT:
      window_closed = SDL_TRUE;
      return 0;
    case SDL_KEYDOWN:
      I new_end = (key_buffer_end + 1) % SDL_arraysize(key_buffer);
      if (new_end == key_buffer_start) {
        BEEP();
      } else {
        key_buffer[new_end] = ev.key.keysym.sym;
        key_buffer_end = new_end;
      }
      break;
    }
  }

  // Increment the timer and fire any timer callbacks
  timer++;

  SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  SDL_RenderClear(renderer);

  SDL_SetRenderTarget(renderer, screenTex);
  SDL_RenderGeometry(renderer, NULL, colorVerts, SCREEN_WIDTH * SCREEN_HEIGHT * 6, NULL, 0);
  SDL_RenderGeometry(renderer, fontTex, glyphVerts, SCREEN_WIDTH * SCREEN_HEIGHT * 6, NULL, 0);

  SDL_SetRenderTarget(renderer, bigScreenTex);
  SDL_RenderCopy(renderer, screenTex, NULL, NULL);

  SDL_SetRenderTarget(renderer, NULL);
  SDL_RenderCopy(renderer, bigScreenTex, NULL, NULL);

  SDL_RenderPresent(renderer);
  return 1;
}

//======================================================END=======================================================
V END() {
  fontTex = NULL;
  screenTex = NULL;
  bigScreenTex = NULL;

  if (renderer) {
    SDL_DestroyRenderer(renderer);
    renderer = NULL;
  }

  if (window) {
    SDL_DestroyWindow(window);
    window = NULL;
  }

  SDL_QuitSubSystem(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
}

//=====================================================BEEP=======================================================
V BEEP() { SOUND(400, 0.2); }

//======================================================CLS=======================================================
V CLS(I c) {
  CELL cell = {cursor_fg, cursor_bg, c};
  for (I y = 0; y < SCREEN_HEIGHT; y++)
    for (I x = 0; x < SCREEN_WIDTH; x++)
      SET(x, y, cell);
}

//=====================================================COLOR======================================================
V COLOR(I fg, I bg) {
  cursor_fg = fg & 0xF;
  cursor_bg = bg & 0xF;
}

//======================================================GET=======================================================
CELL GET(I x, I y) { return screen[y][x]; }

//=====================================================INKEY======================================================
KEY INKEY() {
  if (key_buffer_start == key_buffer_end)
    return 0;
  KEY key = key_buffer[key_buffer_start];
  key_buffer_start = (key_buffer_start + 1) % SDL_arraysize(key_buffer);
  return key;
}

//=====================================================INPUT======================================================
V INPUT(I size, C buf[size]) { SDL_LogInfo(0, "INPUT not implemented"); }

//=====================================================ISKEY======================================================
I ISKEY(KEY k) {
  SDL_Scancode scan = SDL_GetScancodeFromKey(k);
  return keys[scan];
}

//===================================================ISKEYJUST====================================================
I ISKEYJUST(KEY k) {
  SDL_Scancode scan = SDL_GetScancodeFromKey(k);
  return keys[scan] && !last_keys[scan];
}

//====================================================ISNOKEY=====================================================
I ISNOKEY(KEY k) {
  SDL_Scancode scan = SDL_GetScancodeFromKey(k);
  return !keys[scan];
}

//==================================================ISNOKEYJUST===================================================
I ISNOKEYJUST(KEY k) {
  SDL_Scancode scan = SDL_GetScancodeFromKey(k);
  return !keys[scan] && last_keys[scan];
}

//====================================================LOCATE======================================================
V LOCATE(I x, I y) {
  if (x < 0) {
    y -= (x - SCREEN_WIDTH) / SCREEN_WIDTH;
    x += (x - SCREEN_WIDTH) / SCREEN_WIDTH * SCREEN_WIDTH;
  }
  if (x >= SCREEN_WIDTH) {
    y += x / SCREEN_WIDTH;
    x -= x / SCREEN_WIDTH * SCREEN_WIDTH;
  }
  if (y < 0)
    y = 0;
  if (y >= SCREEN_HEIGHT)
    y = SCREEN_HEIGHT;

  cursor_x = x;
  cursor_y = y;
}

//===================================================LOCATEREL====================================================
V LOCATEREL(I x, I y) { LOCATE(cursor_x + x, cursor_y + y); }

//=====================================================PLAY=======================================================
V PLAY(const C *song_) {
  song = song_;
  song_note_duration = 0; // End current note early
}

//===================================================PLAY_OFF=====================================================
V PLAY_OFF() { SDL_LogInfo(0, "PLAY_OFF not implemented"); }

//====================================================PLAY_ON=====================================================
V PLAY_ON() { SDL_LogInfo(0, "PLAY_ON not implemented"); }

//==================================================PLAY_START====================================================
V PLAY_START() { SDL_LogInfo(0, "PLAY_START not implemented"); }

//===================================================PLAY_STOP====================================================
V PLAY_STOP() { SDL_LogInfo(0, "PLAY_STOP not implemented"); }

//======================================================POS=======================================================
V POS(I *x, I *y) {
  if (x)
    *x = cursor_x;
  if (y)
    *y = cursor_y;
}

//=====================================================PRINT======================================================
V PRINT(const C *format, ...) {
  static char buffer[SCREEN_WIDTH * SCREEN_HEIGHT + 1];

  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  for (I i = 0; buffer[i]; i++) {
    switch (buffer[i]) {
    case '\a':
      BEEP();
      break;
    case '\b':
      LOCATEREL(-1, 0);
      break;
    case '\n':
      LOCATE(1, cursor_y + 2);
      break;
    case '\r':
      LOCATE(0, cursor_y);
      break;
    case '\t':
      LOCATE(cursor_x / 8 + 8, cursor_y);
      break;
    default:
      SET(cursor_x, cursor_y, (CELL){.fg = cursor_fg, .bg = cursor_bg, .glyph = buffer[i]});
      LOCATEREL(1, 0);
      break;
    }
  }
}

//===================================================PRINTRAW=====================================================
V PRINTRAW(const C *format, ...) {
  static char buffer[SCREEN_WIDTH * SCREEN_HEIGHT + 1];

  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  for (I i = 0; buffer[i]; i++) {
    SET(cursor_x, cursor_y, (CELL){.fg = cursor_fg, .bg = cursor_bg, .glyph = buffer[i]});
    LOCATEREL(1, 0);
  }
}

//====================================================RANDOM======================================================
I RANDOM(I min, I max) { return rand() % (max - min) + min; }

//===================================================RANDOMIZE====================================================
V RANDOMIZE(I n) { return srand(n); }

//======================================================SET=======================================================
V SET(I x, I y, CELL c) {
  screen[y][x] = c;

  I idx = (y * SCREEN_WIDTH + x) * 6;
  for (I i = 0; i < 6; i++) {
    colorVerts[idx + i].color = palette[c.bg];
    glyphVerts[idx + i].color = palette[c.fg];
  }

  const float gx = (float)(c.glyph % 16 * FONT_WIDTH) / (FONT_WIDTH * 16);
  const float gy = (float)(c.glyph / 16 * FONT_HEIGHT) / (FONT_HEIGHT * 16);
  const float gw = 1.0f / 16;
  const float gh = 1.0f / 16;

  glyphVerts[idx + 0].tex_coord = (SDL_FPoint){gx, gy};
  glyphVerts[idx + 1].tex_coord = (SDL_FPoint){gx, gy + gh};
  glyphVerts[idx + 2].tex_coord = (SDL_FPoint){gx + gw, gy + gh};
  glyphVerts[idx + 3].tex_coord = (SDL_FPoint){gx, gy};
  glyphVerts[idx + 4].tex_coord = (SDL_FPoint){gx + gw, gy + gh};
  glyphVerts[idx + 5].tex_coord = (SDL_FPoint){gx + gw, gy};
}

//===================================================SET_CHAR=====================================================
V SET_CHAR(I x, I y, C c) {
  CELL cell = GET(x, y);
  cell.glyph = c;
  SET(x, y, cell);
}

//=====================================================SOUND======================================================
V SOUND(I freq, D dur) {
  song = "";
  song_note = freq;
  song_note_duration = audio_spec.freq * dur;
  song_sample = 0;
}

//=====================================================STICK======================================================
I STICK(I param) {
  SDL_LogInfo(0, "STICK not implemented");
  return 0;
}

//===================================================TIMER_OFF====================================================
V TIMER_OFF() { SDL_LogInfo(0, "TIMER_OFF not implemented"); }

//===================================================TIMER_ON=====================================================
V TIMER_ON() { SDL_LogInfo(0, "TIMER_ON not implemented"); }

//==================================================TIMER_RESET===================================================
V TIMER_RESET() { SDL_LogInfo(0, "TIMER_RESET not implemented"); }

//===================================================TIMER_ST=====================================================
V TIMER_SET(I period, V (*func)(V)) { SDL_LogInfo(0, "TIMER_SET not implemented"); }

//=====================================================WAIT=======================================================
V WAIT(I dur) {
  for (I i = 0; i < dur; i++)
    UPDATE();
}

//=====================================================DATA=======================================================
static const I letter_to_note[256] = {
    ['c'] = 1, ['C'] = 1, ['d'] = 3, ['D'] = 3,  ['e'] = 5,  ['E'] = 5,  ['f'] = 6,
    ['F'] = 6, ['g'] = 8, ['G'] = 8, ['a'] = 10, ['A'] = 10, ['b'] = 12, ['B'] = 12,
};

static const I note_frequency[8][12] = {
    {0, 0, 0, 39, 41, 44, 46, 49, 51, 55, 58, 62},                            //
    {65, 69, 73, 78, 82, 87, 92, 98, 104, 110, 117, 123},                     //
    {131, 139, 147, 156, 165, 175, 185, 196, 208, 220, 233, 247},             //
    {262, 277, 294, 311, 330, 349, 370, 392, 415, 440, 466, 494},             //
    {523, 554, 587, 622, 659, 698, 740, 784, 831, 880, 932, 988},             //
    {1047, 1109, 1175, 1245, 1318, 1397, 1480, 1568, 1661, 1760, 1865, 1976}, //
    {2093, 2217, 2349, 2489, 2637, 2794, 2960, 3136, 3322, 3520, 3729, 3951}, //
    {4186, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},                                  //
};

static const SDL_Color palette[16] = {
    {0x00, 0x00, 0x00, 0xFF}, // BLACK
    {0x00, 0x00, 0xAA, 0xFF}, // BLUE
    {0x00, 0xAA, 0x00, 0xFF}, // GREEN
    {0x00, 0xAA, 0xAA, 0xFF}, // CYAN
    {0xAA, 0x00, 0x00, 0xFF}, // RED
    {0xAA, 0x00, 0xAA, 0xFF}, // MAGENTA
    {0xAA, 0x55, 0x00, 0xFF}, // BROWN
    {0xAA, 0xAA, 0xAA, 0xFF}, // LIGHT_GRAY
    {0x55, 0x55, 0x55, 0xFF}, // DARK_GREY
    {0x55, 0x55, 0xFF, 0xFF}, // LIGHT_BLUE
    {0x55, 0xFF, 0x55, 0xFF}, // LIGHT_GREEN
    {0x55, 0xFF, 0xFF, 0xFF}, // LIGHT_CYAN
    {0xFF, 0x55, 0x55, 0xFF}, // LIGHT_RED
    {0xFF, 0x55, 0xFF, 0xFF}, // LIGHT_MAGENTA
    {0xFF, 0xFF, 0x55, 0xFF}, // YELLOW
    {0xFF, 0xFF, 0xFF, 0xFF}, // WHITE
};

static const I16 wavetable[WAVETABLE_SIZE] = {
    4713,   6360,   8007,   9430,   10402,  11374,  11945,  12317,  12689,  12666,  12644,  12560,  12355,
    12149,  11907,  11646,  11385,  11142,  10899,  10668,  10462,  10256,  10075,  9907,   9739,   9593,
    9447,   9306,   9177,   9048,   8920,   8792,   8665,   8543,   8422,   8300,   8176,   8051,   7929,
    7808,   7688,   7566,   7445,   7324,   7205,   7085,   6968,   6852,   6736,   6620,   6505,   6391,
    6281,   6170,   6060,   5949,   5839,   5556,   5273,   4739,   3704,   2669,   1087,   -768,   -2624,
    -4485,  -6347,  -8004,  -9250,  -10495, -11300, -11884, -12469, -12573, -12678, -12695, -12539, -12382,
    -12162, -11911, -11661, -11405, -11150, -10906, -10684, -10462, -10268, -10087, -9907,  -9752,  -9598,
    -9450,  -9316,  -9181,  -9052,  -8925,  -8798,  -8674,  -8550,  -8427,  -8304,  -8181,  -8059,  -7936,
    -7814,  -7692,  -7571,  -7450,  -7330,  -7210,  -7092,  -6974,  -6857,  -6741,  -6626,  -6512,  -6398,
    -6284,  -6173,  -6063,  -5954,  -5793,  -5633,  -5300,  -4624,  -3947,  -1613,  1549,
};

static CC font_bmp[] = "\x42\x4d\x3e\x12\x00\x00\x00\x00\x00\x00\x3e\x00\x00\x00\x28\x00" //
                       "\x00\x00\x90\x00\x00\x00\x00\x01\x00\x00\x01\x00\x01\x00\x00\x00" //
                       "\x00\x00\x00\x12\x00\x00\x12\x0b\x00\x00\x12\x0b\x00\x00\x02\x00" //
                       "\x00\x00\x02\x00\x00\x00\x00\x00\x00\x00\xff\xff\xff\x00\x00\x00" //
                       "\x00\x00\x01\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x01\x80\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x80\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x80" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x7f" //
                       "\x9f\x8f\xc1\x83\x80\x00\x00\x00\x00\x00\x03\x80\x00\x00\x00\x00" //
                       "\x00\x00\xfe\x00\x00\x00\x01\x86\xc0\x60\x00\x00\x00\x00\x07\x80" //
                       "\x00\x01\xf0\x00\x00\x00\x00\x00\x0c\x01\x81\x86\xc0\x61\xb8\x00" //
                       "\x00\x00\x0d\x80\x00\x01\xf0\x00\x00\x00\x00\x0c\x06\x03\x01\x86" //
                       "\xc0\x00\xec\x00\x0c\x06\x0d\x80\x00\x01\xf0\x00\x00\x00\xfe\x0c" //
                       "\x03\x06\x01\x80\xc1\xf8\x00\x00\x0c\x00\x1d\x80\x00\x01\xf0\x00" //
                       "\x00\x00\x00\x3f\x01\x8c\x01\x80\xc0\x01\xb8\x00\x00\x00\x01\x86" //
                       "\xc7\xc1\xf0\x00\x00\x00\x00\x0c\x03\x06\x01\x80\xc0\x60\xec\x00" //
                       "\x00\x00\x01\x86\xc6\x41\xf0\x00\x00\x00\xfe\x0c\x06\x03\x01\xb0" //
                       "\xc0\x60\x00\x38\x00\x00\x01\x86\xc3\x01\xf0\x00\x00\x00\x00\x00" //
                       "\x0c\x01\x81\xb0\xc0\x00\x00\x6c\x00\x00\x01\x86\xc1\x80\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\xe0\xc0\x00\x00\x6c\x00\x00\x01\x86" //
                       "\xc6\xc0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xc0\x00\x00\x38" //
                       "\x00\x00\x01\xed\x83\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\xc0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x76\x66" //
                       "\x30\x0d\x8f\xe3\x81\x80\x30\x7e\x1c\x3b\x87\x80\x06\x00\x71\x8c" //
                       "\x00\x00\xdc\x63\x30\x0d\x8c\x66\xc1\x80\x30\x18\x36\x1b\x0c\xc0" //
                       "\x03\x00\xc1\x8c\x00\x00\xd8\x63\x30\x0d\x86\x06\xc1\xf0\x30\x3c" //
                       "\x63\x1b\x0c\xc7\xe3\xf1\x81\x8c\x00\x00\xd8\x63\x30\x0d\x83\x06" //
                       "\xc1\x98\x30\x66\x63\x1b\x0c\xcd\xb7\x99\x81\x8c\x00\x00\xd8\x66" //
                       "\x30\x0d\x81\x86\xc1\x98\x30\x66\x7f\x1b\x0c\xcd\xb6\xd9\x81\x8c" //
                       "\x00\x00\xdc\x6c\x30\x0d\x83\x06\xc1\x98\x30\x66\x63\x31\x87\xcd" //
                       "\xb6\xd9\xf1\x8c\x00\x00\x76\x66\x30\x0d\x86\x03\xf1\x99\xb8\x3c" //
                       "\x63\x31\x81\x87\xe3\xf1\x81\x8c\x00\x00\x00\x66\x31\x9f\xcc\x60" //
                       "\x01\x98\xec\x18\x36\x31\x83\x00\x00\x31\x81\x8c\x00\x00\x00\x66" //
                       "\x31\x80\x0f\xe0\x00\x00\x00\x7e\x1c\x1b\x06\x00\x00\x18\xc0\xf8" //
                       "\x00\x00\x00\x3c\x3f\x80\x00\x00\x00\x00\x00\x00\x00\x0e\x03\xc0" //
                       "\x00\x00\x70\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0c" //
                       "\x0d\x80\x00\x00\xc0\xd8\x6c\x18\x00\x06\x1f\xff\xff\x80\x3e\x00" //
                       "\x00\x00\x00\x0c\x0d\x80\x00\x00\xc0\xd8\x6c\x18\x00\x06\x1f\xff" //
                       "\xff\x80\x3e\x00\x00\x00\x00\x0c\x0d\x80\x00\x00\xc0\xd8\x6c\x18" //
                       "\x00\x06\x1f\xff\xff\x80\x3e\x00\x00\x00\x00\x0c\x0d\x80\x00\x00" //
                       "\xc0\xd8\x6c\x18\x00\x06\x1f\xff\xff\x80\x3e\x00\x00\x00\x00\x0c" //
                       "\x0d\x80\x00\x00\xc0\xd8\x6c\x18\x00\x06\x1f\xff\xff\x80\x3e\x00" //
                       "\x00\x00\x00\x0c\x0d\x80\x00\x00\xc0\xd8\x6c\x18\x00\x06\x1f\xff" //
                       "\xff\x80\x3e\x00\x00\x00\x00\x0c\x0d\x80\x00\x00\xc0\xd8\x6c\x18" //
                       "\x00\x06\x1f\xff\xff\x80\x3e\x00\x00\x00\x00\x0c\x0d\x80\x00\x00" //
                       "\xc0\xd8\x6c\x18\x00\x06\x1f\xff\xff\x80\x3e\x00\x00\x00\xff\xff" //
                       "\xff\xe7\xf1\xf8\xfc\xff\xff\xff\xfc\x07\xff\xff\xff\x80\x3e\x00" //
                       "\x00\x00\x36\x00\x00\x06\xc1\x80\xc0\x00\x6c\x18\x0c\x00\x1f\xf0" //
                       "\x07\x80\x3f\xff\x00\x00\x36\x7f\xc0\x06\xc1\xf8\xfc\x00\x6c\xff" //
                       "\x8c\x00\x1f\xf0\x07\x80\x3f\xff\x00\x00\x36\x00\x00\x06\xc1\x80" //
                       "\x00\x00\x6c\x18\x0c\x00\x1f\xf0\x07\x80\x3f\xff\x00\x00\x36\x00" //
                       "\x00\x06\xc1\x80\x00\x00\x6c\x18\x0c\x00\x1f\xf0\x07\x80\x3f\xff" //
                       "\x00\x00\x36\x00\x00\x06\xc1\x80\x00\x00\x6c\x18\x0c\x00\x1f\xf0" //
                       "\x07\x80\x3f\xff\x00\x00\x36\x00\x00\x06\xc1\x80\x00\x00\x6c\x18" //
                       "\x0c\x00\x1f\xf0\x07\x80\x3f\xff\x00\x00\x36\x00\x00\x06\xc1\x80" //
                       "\x00\x00\x6c\x18\x0c\x00\x1f\xf0\x07\x80\x3f\xff\x00\x00\x00\x00" //
                       "\x06\x03\x00\x00\xc0\x60\x6c\x00\x1b\x00\x06\xc3\x60\x00\xd8\x00" //
                       "\x00\x00\x00\x00\x06\x03\x00\x00\xc0\x60\x6c\x00\x1b\x00\x06\xc3" //
                       "\x60\x00\xd8\x00\x00\x00\x00\x00\x06\x03\x00\x00\xc0\x60\x6c\x00" //
                       "\x1b\x00\x06\xc3\x60\x00\xd8\x00\x00\x00\x00\x00\x06\x03\x00\x00" //
                       "\xc0\x60\x6c\x00\x1b\x00\x06\xc3\x60\x00\xd8\x00\x00\x00\x00\x00" //
                       "\x06\x03\x00\x00\xc0\x60\x6c\x00\x1b\x00\x06\xc3\x60\x00\xd8\x00" //
                       "\x00\x00\x00\x00\x06\x03\x00\x00\xc0\x60\x6c\x00\x1b\x00\x06\xc3" //
                       "\x60\x00\xd8\x00\x00\x00\x00\x00\x06\x03\x00\x00\xc0\x60\x6c\x00" //
                       "\x1b\x00\x06\xc3\x60\x00\xd8\x00\x00\x00\x00\x00\x06\x03\x00\x00" //
                       "\xc0\x60\x6c\x00\x1b\x00\x06\xc3\x60\x00\xd8\x00\x00\x00\x1f\xff" //
                       "\xff\xe3\xff\xff\xfc\x7e\x6f\x3f\x9b\xff\xfe\xf3\x7f\xff\xdf\xff" //
                       "\x00\x00\x18\x0c\x00\x03\x00\x00\xc0\x60\x6c\x30\x18\x00\x00\x03" //
                       "\x00\x00\x00\x00\x00\x00\x18\x0c\x00\x03\x00\x00\xc0\x7e\x6c\x37" //
                       "\x9f\xfd\xff\xf3\x7f\xff\xdf\xff\x00\x00\x18\x0c\x00\x03\x00\x00" //
                       "\xc0\x60\x6c\x36\x00\x0d\x80\x03\x60\x00\xd8\x30\x00\x00\x18\x0c" //
                       "\x00\x03\x00\x00\xc0\x60\x6c\x36\x00\x0d\x80\x03\x60\x00\xd8\x30" //
                       "\x00\x00\x18\x0c\x00\x03\x00\x00\xc0\x60\x6c\x36\x00\x0d\x80\x03" //
                       "\x60\x00\xd8\x30\x00\x00\x18\x0c\x00\x03\x00\x00\xc0\x60\x6c\x36" //
                       "\x00\x0d\x80\x03\x60\x00\xd8\x30\x00\x00\x18\x0c\x00\x03\x00\x00" //
                       "\xc0\x60\x6c\x36\x00\x0d\x80\x03\x60\x00\xd8\x30\x00\x00\x44\x55" //
                       "\x1d\xc3\x01\x80\xc0\xd8\x6c\x18\x1b\x0d\x86\xc0\x00\x00\x00\x30" //
                       "\x00\x00\x11\x2a\xb7\x43\x01\x80\xc0\xd8\x6c\x18\x1b\x0d\x86\xc0" //
                       "\x00\x00\x00\x30\x00\x00\x44\x55\x1d\xc3\x01\x80\xc0\xd8\x6c\x18" //
                       "\x1b\x0d\x86\xc0\x00\x00\x00\x30\x00\x00\x11\x2a\xb7\x43\x01\x80" //
                       "\xc0\xd8\x6c\x18\x1b\x0d\x86\xc0\x00\x00\x00\x30\x00\x00\x44\x55" //
                       "\x1d\xc3\x01\x80\xc0\xd8\x6c\x18\x1b\x0d\x86\xc0\x00\x00\x00\x30" //
                       "\x00\x00\x11\x2a\xb7\x43\x01\x80\xc0\xd8\x6c\x18\x1b\x0d\x86\xc0" //
                       "\x00\x00\x00\x30\x00\x00\x44\x55\x1d\xc3\x01\x80\xc0\xd8\x6c\x18" //
                       "\x1b\x0d\x86\xc0\x00\x00\x00\x30\x00\x00\x11\x2a\xb7\x43\x01\x80" //
                       "\xc0\xd8\x6c\x18\x1b\x0d\x86\xc0\x00\x00\x00\x30\x00\x00\x44\x55" //
                       "\x1d\xc3\x0f\x87\xc3\xd9\xfc\xf8\x7b\x0d\x9e\xcf\xe7\xf3\xe1\xf0" //
                       "\x00\x00\x11\x2a\xb7\x43\x01\x80\xc0\xd8\x00\x18\x03\x0d\x80\xc0" //
                       "\x61\xb0\x60\x00\x00\x00\x44\x55\x1d\xc3\x01\x87\xc0\xd8\x00\xf8" //
                       "\x7b\x0d\x9f\xcf\x61\xb3\xe0\x00\x00\x00\x11\x2a\xb7\x43\x01\x80" //
                       "\xc0\xd8\x00\x00\x1b\x0d\x80\x03\x61\xb0\x60\x00\x00\x00\x44\x55" //
                       "\x1d\xc3\x01\x80\xc0\xd8\x00\x00\x1b\x0d\x80\x03\x61\xb0\x60\x00" //
                       "\x00\x00\x11\x2a\xb7\x43\x01\x80\xc0\xd8\x00\x00\x1b\x0d\x80\x03" //
                       "\x61\xb0\x60\x00\x00\x00\x44\x55\x1d\xc3\x01\x80\xc0\xd8\x00\x00" //
                       "\x1b\x0d\x80\x03\x61\xb0\x60\x00\x00\x00\x11\x2a\xb7\x43\x01\x80" //
                       "\xc0\xd8\x00\x00\x1b\x0d\x80\x03\x61\xb0\x60\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x03\xe0\x60\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x01\x80\x60\x00\x00\x00\x00\x00\x76\x1e" //
                       "\x1f\x0e\xc6\x66\x30\x00\x00\x7c\x00\x00\x00\xc3\xe0\xc0\x00\x00" //
                       "\x00\x00\xcc\x0c\x31\x99\x86\x66\x30\x00\x00\xc6\x60\x01\x93\x69" //
                       "\x61\xe0\x00\x00\x00\x00\xcc\x0c\x31\x99\x86\x66\x30\x00\x00\xc6" //
                       "\x60\x01\x99\xcc\xe1\xe0\xd9\xb0\x00\x00\xcc\x0c\x31\x99\x86\x66" //
                       "\x70\x00\x00\xc0\x60\x01\x8c\x06\x61\xe1\xb0\xd8\x00\x00\x7c\x0c" //
                       "\x31\x99\x86\x66\xf0\x00\x00\x60\x60\x01\x86\x03\x00\xc3\x60\x6c" //
                       "\x00\x00\x0c\x0c\x31\x99\x86\x67\xf1\xf8\xf8\x30\x7f\x3f\x83\x01" //
                       "\x80\xc1\xb0\xd8\x00\x00\x78\x1c\x1f\x19\x8d\xc7\xb0\x00\x00\x30" //
                       "\x00\x00\x19\x8c\xc0\xc0\xd9\xb0\x00\x00\x00\x00\x00\x00\x00\x07" //
                       "\x30\xf8\x70\x00\x00\x00\x18\xcc\x60\x00\x00\x00\x00\x00\x60\x18" //
                       "\x18\x0c\x0d\xc6\x31\xb0\xd8\x30\x00\x00\x18\x4c\x20\xc0\x00\x00" //
                       "\x00\x00\x30\x0c\x0c\x06\x07\x60\x01\xb0\xd8\x30\x00\x00\x18\x0c" //
                       "\x00\xc0\x00\x00\x00\x00\x18\x06\x06\x03\x00\x06\xe0\xf0\x70\x00" //
                       "\x00\x00\x18\x0c\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03" //
                       "\xb0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x78\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0c" //
                       "\x00\x00\x00\x00\x00\x00\x00\xe0\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x06\x00\x00\x00\x00\x00\x00\x01\xb0\x00\x00\xfe\x3b" //
                       "\xb3\x8f\x87\xc3\xe1\xd8\xec\x7e\x3e\x1f\x03\x0f\xc0\xc3\xcc\x30" //
                       "\x00\x00\x66\x6e\x33\x18\xcc\x66\x33\x31\x98\xc6\x63\x31\x83\x0e" //
                       "\x60\xc1\x98\x30\x00\x00\x60\x6c\x33\x18\xcc\x66\x33\x31\x98\xc6" //
                       "\x63\x31\x8f\xc6\x00\xc1\x98\x30\x00\x00\x60\x3f\x33\x18\xcc\x66" //
                       "\x33\x31\x98\xc6\x63\x31\x98\x66\x07\xf9\x98\x30\x00\x00\x7c\x0d" //
                       "\xb3\x18\xcc\x66\x33\x31\x98\xc6\x63\x31\x98\x06\x00\xc1\xbc\x30" //
                       "\x00\x00\x60\x1d\xbf\x98\xcc\x66\x33\x31\x98\xc6\x63\x31\x98\x06" //
                       "\x07\xf9\x98\xfc\x00\x00\x66\x37\x33\x0f\x87\xc3\xe3\x31\x98\xc6" //
                       "\x63\x31\x98\x0f\x00\xc1\x88\x30\x00\x00\xfe\x00\x33\x00\x00\x00" //
                       "\x00\x00\x00\x00\x63\x31\x98\x66\x01\xe1\xf0\x30\x00\x00\x00\x00" //
                       "\x1b\x0d\x80\x00\xc3\x30\x30\x00\x3e\x31\x8f\xc6\x43\x31\x98\x30" //
                       "\x00\x00\x60\x00\x0f\x87\x0c\x61\x81\xe0\x60\xc6\x00\x00\x03\x06" //
                       "\xc6\x19\x98\x36\x00\x00\x30\x00\x00\x02\x00\x03\x00\xc0\xc0\x00" //
                       "\x63\x31\x83\x03\x80\x03\xf0\x1c\x00\x00\x18\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x7c\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x06\x00\x00\x00\x00\x00" //
                       "\x00\x00\x78\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x0c\x3b" //
                       "\x1f\x0e\xc7\x63\xb1\xd8\x0c\x7c\x3e\x1f\x07\x83\xc1\xe3\x19\x8c" //
                       "\x00\x00\x3c\x66\x31\x99\x8c\xc6\x63\x30\x18\xc6\x63\x31\x83\x01" //
                       "\x80\xc3\x19\x8c\x00\x00\x66\x66\x30\x19\x8c\xc6\x63\x30\x78\xc0" //
                       "\x60\x30\x03\x01\x80\xc3\x19\x8c\x00\x00\xc2\x66\x30\x19\x8c\xc6" //
                       "\x63\x30\xcc\xc0\x60\x30\x03\x01\x80\xc3\xf9\xfc\x00\x00\xc0\x66" //
                       "\x3f\x8f\x87\xc3\xe1\xf0\xc0\xfe\x7f\x3f\x83\x01\x80\xc3\x19\x8c" //
                       "\x00\x00\xc0\x66\x31\x81\x80\xc0\x60\x30\xc0\xc6\x63\x31\x83\x01" //
                       "\x80\xc3\x19\x8c\x00\x00\xc0\x66\x1f\x0f\x07\x83\xc1\xe0\xcc\x7c" //
                       "\x3e\x1f\x07\x03\x81\xc1\xb0\xd8\x00\x00\xc2\x00\x00\x00\x00\x00" //
                       "\x00\x00\x78\x00\x00\x00\x00\x00\x00\x00\xe0\x70\x00\x00\x66\x00" //
                       "\x0c\x0d\x80\x00\xc0\xe0\x00\x6c\x00\x06\x00\x06\x60\xc0\x40\x00" //
                       "\x00\x00\x3c\x66\x06\x07\x0c\xc1\x81\xb0\x00\x38\x63\x0c\x0c\xc3" //
                       "\xc1\x80\x00\x70\x00\x00\x00\x00\x03\x02\x00\x03\x00\xe0\x00\x10" //
                       "\x00\x18\x00\x01\x83\x03\x18\xd8\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x70\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\xf0\x0f\x00\x00\x00\x00\x00\x00\x00\x00\x7c\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x60\x06\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x06\x00\x00\x00\x00\x00\x00\x00\x00\x00\x60\x06\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x00\x00\x7c\x3e" //
                       "\x3c\x0f\x81\xc3\xb0\x60\xcc\xc3\x3f\x3f\x81\xc1\x83\x80\x00\x00" //
                       "\x00\x00\x66\x66\x18\x18\xc3\x66\x60\xf1\xfe\x66\x63\x31\x83\x01" //
                       "\x80\xc0\x01\xfc\x00\x00\x66\x66\x18\x01\x83\x06\x61\x99\xb6\x3c" //
                       "\x63\x18\x03\x01\x80\xc0\x01\x8c\x00\x00\x66\x66\x18\x07\x03\x06" //
                       "\x63\x0d\xb6\x18\x63\x0c\x03\x01\x80\xc0\x01\x8c\x00\x00\x66\x66" //
                       "\x19\x8c\x03\x06\x63\x0d\x86\x3c\x63\x06\x03\x01\x80\xc0\x01\x8c" //
                       "\x00\x00\x66\x66\x1d\x98\xc3\x06\x63\x0d\x86\x66\x63\x33\x0e\x00" //
                       "\x00\x70\x00\xd8\x00\x00\xdc\x3b\x37\x0f\x8f\xc6\x63\x0d\x86\xc3" //
                       "\x63\x3f\x83\x01\x80\xc0\x00\x70\x00\x00\x00\x00\x00\x00\x03\x00" //
                       "\x00\x00\x00\x00\x00\x00\x03\x01\x80\xc0\x00\x20\x00\x00\x00\x00" //
                       "\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x03\x01\x80\xc3\x70\x00" //
                       "\x00\x00\x00\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x01\xc1" //
                       "\x83\x81\xd8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xf0\x00\x00\x0f\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x98\x00" //
                       "\x00\x19\x80\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x18\x00\x00\x19\x80\x00\x00\x00\x00\x00\x00\x00\x00\x3b" //
                       "\x1f\x0f\x87\x63\xe3\xc0\xf8\xe6\x1e\x01\x9c\xc3\xc6\xd9\x98\xf8" //
                       "\x00\x00\x00\x66\x19\x98\xcc\xc6\x31\x81\x98\x66\x0c\x01\x8c\xc1" //
                       "\x86\xd9\x99\x8c\x00\x00\x00\x66\x19\x98\x0c\xc6\x01\x81\x98\x66" //
                       "\x0c\x01\x8d\x81\x86\xd9\x99\x8c\x00\x00\x00\x66\x19\x98\x0c\xc6" //
                       "\x01\x81\x98\x66\x0c\x01\x8f\x01\x86\xd9\x99\x8c\x00\x00\x00\x3e" //
                       "\x19\x98\x0c\xc7\xf1\x81\x98\x66\x0c\x01\x8f\x01\x86\xd9\x99\x8c" //
                       "\x00\x00\x00\x06\x1b\x18\xc6\xc6\x33\xc1\x98\x76\x0c\x01\x8d\x81" //
                       "\x87\xf9\x99\x8c\x00\x00\x00\x3c\x1e\x0f\x83\xc3\xe1\x80\xec\x6c" //
                       "\x1c\x03\x8c\xc1\x87\x33\x70\xf8\x00\x00\x00\x00\x18\x00\x00\xc0" //
                       "\x01\x90\x00\x60\x00\x00\x0c\x01\x80\x00\x00\x00\x00\x00\x00\x00" //
                       "\x18\x00\x00\xc0\x01\xb0\x00\x60\x0c\x01\x8c\x01\x80\x00\x00\x00" //
                       "\x00\x00\x18\x00\x38\x00\x01\xc0\x00\xe0\x00\xe0\x0c\x01\x9c\x03" //
                       "\x80\x00\x00\x00\x00\x00\x30\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x30\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x07\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x01\xfe\x00\x00\x00\x06\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xf0\x3e" //
                       "\x39\x8f\x83\xc3\xe0\x60\xcc\xc3\x1e\x3f\xc7\x80\x21\xe0\x00\x00" //
                       "\x00\x00\x60\x6f\x19\x98\xc1\x86\x30\xf0\xcc\xc3\x0c\x30\xc6\x00" //
                       "\x60\x60\x00\x00\x00\x00\x60\x6b\x19\x98\xc1\x86\x31\x99\xfe\x66" //
                       "\x0c\x30\x46\x00\xe0\x60\x00\x00\x00\x00\x60\x63\x19\x80\xc1\x86" //
                       "\x33\x0d\xb6\x3c\x0c\x18\x06\x01\xc0\x60\x00\x00\x00\x00\x60\x63" //
                       "\x1b\x01\x81\x86\x33\x0d\xb6\x18\x0c\x0c\x06\x03\x80\x60\x00\x00" //
                       "\x00\x00\x7c\x63\x1f\x07\x01\x86\x33\x0d\x86\x18\x1e\x06\x06\x07" //
                       "\x00\x60\x00\x00\x00\x00\x66\x63\x19\x8c\x01\x86\x33\x0d\x86\x3c" //
                       "\x33\x03\x06\x0e\x00\x60\x00\x00\x00\x00\x66\x63\x19\x98\xc9\x96" //
                       "\x33\x0d\x86\x66\x61\xa1\x86\x0c\x00\x60\x00\x00\x00\x00\x66\x63" //
                       "\x19\x98\xcd\xb6\x33\x0d\x86\xc3\x61\xb0\xc6\x08\x00\x63\x18\x00" //
                       "\x00\x00\xfc\x3e\x3f\x0f\x8f\xf6\x33\x0d\x86\xc3\x61\xbf\xc7\x80" //
                       "\x01\xe1\xb0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\xe0\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x40\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x7c\x63" //
                       "\x3f\x07\x8f\x87\xf3\xc0\x74\xc6\x1e\x1e\x1c\xcf\xe6\x1b\x18\xf8" //
                       "\x00\x00\xc0\x63\x19\x8c\xc6\xc3\x31\x80\xcc\xc6\x0c\x33\x0c\xc6" //
                       "\x66\x1b\x19\x8c\x00\x00\xdc\x63\x19\x98\x46\x63\x11\x81\x8c\xc6" //
                       "\x0c\x33\x0c\xc6\x26\x1b\x19\x8c\x00\x00\xde\x63\x19\x98\x06\x63" //
                       "\x01\x81\x8c\xc6\x0c\x33\x0d\x86\x06\x1b\x19\x8c\x00\x00\xde\x7f" //
                       "\x19\x98\x06\x63\x41\xa1\xbc\xc6\x0c\x03\x0f\x06\x06\x1b\x39\x8c" //
                       "\x00\x00\xde\x63\x1f\x18\x06\x63\xc1\xe1\x80\xfe\x0c\x03\x0f\x06" //
                       "\x06\xdb\x79\x8c\x00\x00\xc6\x63\x19\x98\x06\x63\x41\xa1\x80\xc6" //
                       "\x0c\x03\x0d\x86\x07\xfb\xf9\x8c\x00\x00\xc6\x36\x19\x98\x46\x63" //
                       "\x11\x89\x84\xc6\x0c\x03\x0c\xc6\x07\xfb\xd9\x8c\x00\x00\x7c\x1c" //
                       "\x19\x8c\xc6\xc3\x31\x98\xcc\xc6\x0c\x03\x0c\xc6\x07\x3b\x99\x8c" //
                       "\x00\x00\x00\x08\x3f\x07\x8f\x87\xf3\xf8\x78\xc6\x1e\x07\x9c\xcf" //
                       "\x06\x1b\x18\xf8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x3c\x3f" //
                       "\x3f\x8f\x81\xe3\xe1\xf0\x60\x7c\x3c\x00\x06\x00\x60\x01\x80\x30" //
                       "\x00\x00\x66\x0c\x31\x98\xc0\xc6\x33\x18\x60\xc6\x06\x06\x03\x00" //
                       "\xc0\x00\xc0\x30\x00\x00\xc3\x0c\x30\x00\xc0\xc0\x33\x18\x60\xc6" //
                       "\x03\x06\x03\x01\x80\x00\x60\x00\x00\x00\xc3\x0c\x18\x00\xc0\xc0" //
                       "\x33\x18\x60\xc6\x03\x00\x00\x03\x03\xf0\x30\x30\x00\x00\xdb\x0c" //
                       "\x0c\x00\xcf\xe0\x33\x18\x30\xc6\x03\x00\x00\x06\x00\x00\x18\x30" //
                       "\x00\x00\xdb\x0c\x06\x07\x8c\xc7\xe3\xf0\x18\x7c\x3f\x00\x00\x03" //
                       "\x00\x00\x30\x30\x00\x00\xc3\x0c\x03\x00\xc6\xc6\x03\x00\x0c\xc6" //
                       "\x63\x06\x03\x01\x83\xf0\x60\x18\x00\x00\xc3\x3c\x01\x80\xc3\xc6" //
                       "\x03\x00\x0c\xc6\x63\x06\x03\x00\xc0\x00\xc1\x8c\x00\x00\x66\x1c" //
                       "\x31\x98\xc1\xc6\x01\x81\x8c\xc6\x63\x00\x00\x00\x60\x01\x81\x8c" //
                       "\x00\x00\x3c\x0c\x1f\x0f\x80\xc7\xf0\xe1\xfc\x7c\x3e\x00\x00\x00" //
                       "\x00\x00\x00\xf8\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x80\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x80" //
                       "\x00\x00\x00\x00\x00\x00\x00\x03\x00\x00\x00\x00\x00\x00\x00\x0c" //
                       "\x00\x0d\x87\xc4\x31\xd8\x00\x0c\x18\x00\x00\x01\x80\x00\x61\x00" //
                       "\x00\x00\x00\x0c\x00\x0d\x8c\x66\x33\x30\x00\x18\x0c\x00\x00\x01" //
                       "\x80\x00\x61\x80\x00\x00\x00\x00\x00\x1f\xc8\x63\x03\x30\x00\x30" //
                       "\x06\x19\x83\x01\x80\x00\x00\xc0\x00\x00\x00\x0c\x00\x0d\x80\x61" //
                       "\x83\x30\x00\x30\x06\x0f\x03\x00\x00\x00\x00\x60\x00\x00\x00\x0c" //
                       "\x00\x0d\x80\x60\xc3\x70\x00\x30\x06\x3f\xcf\xc0\x07\xf0\x00\x30" //
                       "\x00\x00\x00\x0c\x00\x0d\x87\xc0\x61\xd8\x00\x30\x06\x0f\x03\x00" //
                       "\x00\x00\x00\x18\x00\x00\x00\x1e\x00\x1f\xcc\x06\x30\xe0\x00\x30" //
                       "\x06\x19\x83\x00\x00\x00\x00\x0c\x00\x00\x00\x1e\x09\x0d\x8c\x26" //
                       "\x11\xb0\xc0\x30\x06\x00\x00\x00\x00\x00\x00\x04\x00\x00\x00\x1e" //
                       "\x19\x8d\x8c\x60\x01\xb0\x60\x18\x0c\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x0c\x19\x80\x07\xc0\x00\xe0\x60\x0c\x18\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x19\x80\x01\x80\x00\x00\x60\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01\x80" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x03" //
                       "\xe0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x80\x01" //
                       "\x00\x0c\xc1\xb6\x33\xf8\xfc\x18\x0c\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\xc0\x03\x06\x0c\xc1\xb0\x63\xf8\x30\x18\x1e\x00\x00\x00" //
                       "\x00\x03\xf8\x20\x00\x00\xe0\x07\x0f\x00\x01\xb1\xc3\xf8\x78\x18" //
                       "\x3f\x06\x06\x0f\xe1\x23\xf8\x70\x00\x00\xf0\x0f\x1f\x8c\xc1\xb3" //
                       "\x63\xf8\xfc\x18\x0c\x03\x0c\x0c\x03\x31\xf0\x70\x00\x00\xf8\x1f" //
                       "\x06\x0c\xc1\xb6\x30\x00\x30\x18\x0c\x3f\x9f\xcc\x07\xf9\xf0\xf8" //
                       "\x00\x00\xfe\x7f\x06\x0c\xc7\xb6\x30\x00\x30\x18\x0c\x03\x0c\x0c" //
                       "\x03\x30\xe0\xf8\x00\x00\xf8\x1f\x06\x0c\xcd\xb3\x60\x00\x30\x18" //
                       "\x0c\x06\x06\x00\x01\x20\xe1\xfc\x00\x00\xf0\x0f\x1f\x8c\xcd\xb1" //
                       "\xc0\x00\xfc\x7e\x0c\x00\x00\x00\x00\x00\x41\xfc\x00\x00\xe0\x07" //
                       "\x0f\x0c\xcd\xb3\x00\x00\x78\x3c\x0c\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\xc0\x03\x06\x0c\xc7\xf6\x30\x00\x30\x18\x0c\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x80\x01\x00\x00\x00\x03\xe0\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\xff\x00\x3f\xc0\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\x00\x3f\xc0\x00" //
                       "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff" //
                       "\x00\x3f\xc0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\xff\x00\x3f\xc0\x00\x00\x03\x00\x00\x00\x00\x00\x3f" //
                       "\x1f\x82\x00\x01\xe0\xf0\x00\xff\x00\x3f\xcf\x01\x87\x03\x98\x30" //
                       "\x00\x00\x00\x40\xbf\xc7\x01\x00\xc0\x60\x00\xff\x1e\x30\xd9\x81" //
                       "\x87\x83\x9c\x30\x00\x00\x00\x40\xbf\xcf\x83\x80\xc0\x60\x30\xe7" //
                       "\x33\x26\x59\x87\xe3\x81\x9d\xb6\x00\x00\x00\x4c\xb9\xdf\xc7\xc7" //
                       "\x39\xf8\x78\xc3\x21\x2f\x59\x81\x81\x81\x8c\x78\x00\x00\x00\x5e" //
                       "\xb0\xdf\xcf\xe7\x3b\xfc\x78\xc3\x21\x2f\x59\x83\xc1\x81\x8d\xce" //
                       "\x00\x00\x00\x40\xbf\xdf\xc7\xc7\x3b\xfc\x30\xe7\x33\x26\x4f\x06" //
                       "\x61\x81\x8c\x78\x00\x00\x00\x40\xbf\xdf\xc3\x81\xe1\xf8\x00\xff" //
                       "\x1e\x30\xc6\x46\x61\x81\x8d\xb6\x00\x00\x00\x52\xb6\xcd\x81\x01" //
                       "\xe0\xf0\x00\xff\x00\x3f\xc3\x46\x61\xf9\xfc\x30\x00\x00\x00\x40" //
                       "\xbf\xc0\x00\x00\xc0\x60\x00\xff\x00\x3f\xc1\xc6\x61\x99\x8c\x30" //
                       "\x00\x00\x00\x3f\x1f\x80\x00\x00\x00\x00\x00\xff\x00\x3f\xc3\xc3" //
                       "\xc1\xf9\xfc\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff" //
                       "\x00\x3f\xc0\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00" //
                       "\x00\x00\x00\xff\x00\x3f\xc0\x00\x00\x00\x00\x00\x00\x00";
static const size_t font_bmp_size = sizeof(font_bmp);
