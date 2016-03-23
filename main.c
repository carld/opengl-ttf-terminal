/* experimental terminal window using libSDL, OpenGL, stb_truetype, fontstash, and libtsm
 * Copyright (c) 2016 A. Carl Douglas
 */


//
// Copyright (c) 2009 Mikko Mononen memon@inside.org
//
// This software is provided 'as-is', without any express or implied
// warranty.  In no event will the authors be held liable for any damages
// arising from the use of this software.
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgment in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.
//

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>
#include <fcntl.h>

#include <GL/glew.h>

#define NO_SDL_GLEXT

#include "SDL.h"
#include "SDL_opengl.h"


#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"
#define GLFONTSTASH_IMPLEMENTATION
#include "glfontstash.h"

#include "libtsm.h"
#include "shl_pty.h"
#include "external/xkbcommon-keysyms.h"

#include "shader.h"

extern char **environ;

static int width = 0, height = 0;
static float fh = 21.0f; /* font height */
static float lineh;
static float ascender;
static float descender;
static float bounds[4]; /* glyph width, height */
static float advance;

static unsigned int fullscreen = 0;

static struct tsm_screen *console;
static struct tsm_vte *vte;
static struct shl_pty *pty;
static tsm_age_t screen_age;

void hup_handler(int s) {
  printf("Signal received: %s\n", strsignal(s));
  exit(1);
}

void io_handler(int s) {
  SDL_Event e;
  e.type = SDL_USEREVENT;
  e.user.code = s;
  SDL_PushEvent(&e);
}

void fonsError(void* uptr, int error, int val) {
  printf("FONT ERROR: %d --- %d\n", error, val);
  exit(-1);
}

/* called when data has been read from the fd slave -> master  (vte input )*/
static void term_read_cb(struct shl_pty *pty, char *u8, size_t len, void *data) {
  tsm_vte_input(vte, u8, len);
}

/* called when there is data to be written to the fd master -> slave  (vte output) */
static void term_write_cb(struct tsm_vte *vtelocal, const char *u8, size_t len, void *data) {
  int r;
  r = shl_pty_write(pty, u8, len);
  if (r < 0) {
    printf ("could not write to pty, %d\n", r);
  }
}

static void log_tsm(void *data, const char *file, int line, const char *fn,
                    const char *subs, unsigned int sev, const char *format,
                    va_list args)
{
  fprintf(stderr, "%d: %s: ", sev, subs);
  vfprintf(stderr, format, args);
  fprintf(stderr, "\n");
}

static int draw_cb(struct tsm_screen *screen, uint32_t id,
                   const uint32_t *ch, size_t len,
                   unsigned int cwidth, unsigned int posx,
                   unsigned int posy,
                   const struct tsm_screen_attr *attr,
                   tsm_age_t age, void *data)
{
  int i;
  int lh=lineh;
  int lw=(bounds[2] - bounds[0]);
  float dx=posx*lw, 
        dy=posy*lh;
  char buf[32];
  uint8_t fr, fg, fb, br, bg, bb;
  unsigned int color;
  if (attr->inverse) {
    fr = attr->br; fg = attr->bg; fb = attr->bb; br = attr->fr; bg = attr->fg; bb = attr->fb;
  } else {
    fr = attr->fr; fg = attr->fg; fb = attr->fb; br = attr->br; bg = attr->bg; bb = attr->bb;
  }

  if (!len) {
    glColor4ub(br,bg,bb,255);
    glPolygonMode(GL_FRONT, GL_FILL);
    glRectf(dx+lw,dy,dx,dy+lh);
  } else {
    glColor4ub(br,bg,bb,255);
    glPolygonMode(GL_FRONT, GL_FILL);
    glRectf(dx+lw,dy,dx,dy+lh);

    color = glfonsRGBA(fr,fg,fb,255);
    fonsSetColor(data, color);
    for (i = 0; i < len; i += cwidth)  {
      sprintf(buf,"%c",ch[i]);
      dx = fonsDrawText(data, dx, dy + ascender /*((bounds[2]-bounds[0]))*/, buf, NULL);
    }
  }
  return 0;
}

int main(int argc, char *argv[])
{
  int done;
  int flags;
  SDL_Event event;
  SDL_Surface* screen;
  const SDL_VideoInfo* vi;
  FONScontext* stash = NULL;
  int fontNormal = FONS_INVALID;
  pid_t pid;
  unsigned int ticks;
  int opt;
  const char *fontfile = "VeraMono.ttf";
  struct tsm_screen_attr attr;
  GLhandleARB prg, shader;

  while ((opt = getopt(argc, argv, "f:s:g:m")) != -1) {
    switch (opt) {
    case 'f':
       fontfile = optarg;
       break;
    case 's':
       fh = atoi(optarg);
       break; 
    case 'g':
       width = atoi(strtok(optarg, "x"));
       height = atoi(strtok(NULL, "x"));
      break; 
    case 'm':
      fullscreen = 1;
      break;
    default: /* '?' */
       fprintf(stderr, "Usage: %s [-f ttf file] [-s font size] [-g geometry] [-m fullscreen mode]\n", argv[0]);
       exit(EXIT_FAILURE);
    }
  }
  printf("Using: %s    at: %f \n", fontfile, fh);

  if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
    fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
    return -1;
  }
  ticks = SDL_GetTicks();

  vi = SDL_GetVideoInfo();
  if (width == 0)   width = vi->current_w;
  if (height == 0)  height = vi->current_h;

  tsm_screen_new(&console, log_tsm, 0);
  tsm_vte_new(&vte, console, term_write_cb, 0, log_tsm, 0);

  /* this call will fork */
  pid = shl_pty_open(&pty,
                   term_read_cb,
                   NULL,
                   tsm_screen_get_width(console),
                   tsm_screen_get_height(console));

  if (pid < 0) {
    perror("fork problem");
  } else if (pid != 0 ) {
    /* parent, pty master */
    int fd = shl_pty_get_fd(pty);
    unsigned oflags = 0;
    /* enable SIGIO signal for this process when it has a ready file descriptor */
    signal(SIGIO, &io_handler);
    fcntl(fd, F_SETOWN, getpid(  ));
    oflags = fcntl(fd, F_GETFL);
    fcntl(fd, F_SETFL, oflags | FASYNC);
    /* watch for SIGHUP */
    signal(SIGCHLD, &hup_handler);
  } else {
    /* child, shell */
    char *shell = getenv("SHELL") ? : "/bin/bash";
    char **argv = (char*[]) {  shell, NULL };
    execve(argv[0], argv, environ);
    /* never reached except on execve error */
    perror("execve error");
    exit(-2);
  }

  if (SDL_Init(SDL_INIT_EVERYTHING) < 0) {
    fprintf(stderr, "Couldn't initialize SDL: %s\n", SDL_GetError());
    return -1;
  }
  ticks = SDL_GetTicks();

  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

  flags = SDL_OPENGL;
  if (fullscreen) {
    flags |= SDL_FULLSCREEN;
    vi = SDL_GetVideoInfo();
    width = vi->current_w;
    height = vi->current_h;
  }
  screen = SDL_SetVideoMode(width, height, 0, flags);
  if (!screen) {
    printf("Could not initialise SDL opengl\n");
    return -1;
  }

  SDL_EnableUNICODE(1);  /* for .keysym.unicode */
  SDL_WM_SetCaption("libtsm", 0);
  SDL_EnableKeyRepeat(SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL);

  stash = glfonsCreate(512, 512, FONS_ZERO_TOPLEFT);
  if (stash == NULL) {
    printf("Could not create stash.\n");
    return -1;
  }

  fonsSetErrorCallback(stash, fonsError, stash);

  fontNormal = fonsAddFont(stash, "sans", fontfile);
  if (fontNormal == FONS_INVALID) {
    printf("Could not add font.\n");
    return -1;
  }
  /* measure a character to determine the glyph box size */
  fonsClearState(stash);
  fonsSetFont(stash, fontNormal);
  fonsSetSize(stash, fh);
  advance = fonsTextBounds(stash, 0, 0, "W", NULL, bounds);
  fonsVertMetrics(stash, &ascender, &descender, &lineh);

  tsm_screen_resize(console, (width / (bounds[2]-bounds[0]) - 1), (height / lineh)-1);
  shl_pty_resize(pty, tsm_screen_get_width(console), tsm_screen_get_height(console));

  printf("console width: %d\n", tsm_screen_get_width(console));
  printf("console height: %d\n", tsm_screen_get_height(console));

  glewInit();
  if (GLEW_ARB_vertex_shader && GLEW_ARB_fragment_shader)
    printf("GLSL supported\n");
  else {
    printf("No GLSL support\n");
    exit(1);
  }

  shader = load_shaders("crt.glsl", GL_FRAGMENT_SHADER_ARB);
  prg = glCreateProgramObjectARB();
  glAttachObjectARB(prg,shader);
  glLinkProgramARB(prg);
  arb_info(prg);
  glUseProgramObjectARB(prg);

  done = 0;
  while (!done) {
    if (SDL_WaitEvent(&event)) {
      SDL_keysym k;
      unsigned int mods = 0;
      unsigned int scancode = 0;
      switch (event.type) {
        case SDL_MOUSEMOTION:
          break;
        case SDL_MOUSEBUTTONDOWN:
          break;
        case SDL_KEYDOWN:
          k = event.key.keysym;
          scancode = k.scancode;
          if (k.mod & KMOD_CTRL)  mods |= TSM_CONTROL_MASK;
          if (k.mod & KMOD_SHIFT) mods |= TSM_SHIFT_MASK;
          if (k.mod & KMOD_ALT)   mods |= TSM_ALT_MASK;
          if (k.mod & KMOD_META)  mods |= TSM_LOGO_MASK;
          /* map cursor keys to XKB scancodes to be escaped by libtsm vte */
          if (k.sym == SDLK_UP) scancode = XKB_KEY_Up;
          if (k.sym == SDLK_DOWN) scancode = XKB_KEY_Down;
          if (k.sym == SDLK_LEFT) scancode = XKB_KEY_Left;
          if (k.sym == SDLK_RIGHT) scancode = XKB_KEY_Right;
          if(k.unicode != 0 || 
              (scancode==XKB_KEY_Up || 
                scancode==XKB_KEY_Down || 
                scancode==XKB_KEY_Left || 
                scancode==XKB_KEY_Right)) {
            /* only handle when there's non-zero unicode keypress... found using vim */
            /*printf("scancode: %d  sym: %d  unicode: %d\n", scancode, k.sym, k.unicode);*/
            tsm_vte_handle_keyboard(vte, scancode, k.sym, mods, k.unicode);
          }
          break;
        /*case SDL_TEXTINPUT:*/
          break;
        case SDL_QUIT:
          done = 1;
          break;
        case SDL_USEREVENT: /* sigio - something to read */
          break;
        case SDL_ACTIVEEVENT:
          break;
      }
    }

    shl_pty_dispatch(pty);

    if (SDL_GetTicks() <= (ticks+50))
      continue;
/*
    if ( (SDL_GetAppState() & SDL_APPACTIVE) == 0)
      continue;
*/
    ticks = SDL_GetTicks();

    tsm_vte_get_def_attr(vte, &attr);
    glViewport(0, 0, width, height);
    glClearColor(attr.br, attr.bg, attr.bb, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glDisable(GL_TEXTURE_2D);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0,width,height,0,-1,1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glDisable(GL_DEPTH_TEST);
    //glColor4ub(255,255,255,255);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_CULL_FACE);

    fonsClearState(stash);
    fonsSetFont(stash, fontNormal);
    fonsSetSize(stash, fh);
    screen_age = tsm_screen_draw(console, draw_cb, stash);
    SDL_GL_SwapBuffers();
  }
  shl_pty_close(pty);
  
  SDL_Quit();
  return 0;
}
