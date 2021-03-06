/* A simple SDL-based raw data visualizer.   *
 *                                           *
 * Has tons of bugs.                         *
 *                                           *
 * Doesn't offer a lot of features.          *
 *                                           *
 * Possibly the ugliest C code you'll ever   *
 * read.                                     *
 *                                           *
 * mmaps a file and draws a portion of it    *
 * in the window in platform-native format.  *
 *                                           *
 * Usage: ./rawview <filename> [w] [h]       *
 *                                           *
 * where <filename> is any file. Seriously.  *
 * This thing doesn't even check whether     *
 * the input file is a regular file...if it  *
 * exists and is seekable, it will be drawn. *
 * Maybe.                                    */


#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <SDL2/SDL.h>

#define DEFAULT_WIDTH 512
#define SCROLL_LINES 50
#define SCROLL_WIDTH 16
#define PIXEL_BITS  32
#define PIXEL_BYTES  (PIXEL_BITS / 8)

static inline long imin(long x, long y)
{
    return x < y ? x : y;
}

void set_pixel(SDL_Surface *surf, int x, int y, uint32_t pixel)
{
  uint32_t* target_pixel = (uint32_t*) ((uint8_t*) surf->pixels + y * surf->pitch +
                                                                  x * sizeof(*target_pixel));
  *target_pixel = pixel;
}

uint32_t get_pixel(SDL_Surface *surf, int x, int y)
{
  uint32_t* target_pixel = (uint32_t*) ((uint8_t*) surf->pixels + y * surf->pitch +
                                                                  x * sizeof(*target_pixel));
  return *target_pixel;
}

static void draw_scrollbar(SDL_Surface* surf, int64_t loff, size_t len)
{
    int progress = (int) ((double) surf->h * (double) (loff / surf->w) / (double) (len / PIXEL_BYTES / surf->w - surf->h));
    int y, x;
    int width = SCROLL_WIDTH;

    for (y = 0; y < surf->h; y++)
    {
        if (y > progress - width && y < progress + width)
        {
            for (x = surf->w - width; x < surf->w; x++)
            {
                set_pixel(surf, x, y, 0xFFFFFFFF);
            }
        }
        else
        {
            for (x = surf->w - width; x < surf->w; x++)
            {
                uint32_t pixel = get_pixel(surf, x, y);
                uint32_t a = pixel & 0xFF000000;
                uint32_t b = ((pixel & 0x00FF0000) >> 1) & 0x00FF0000;
                uint32_t g = ((pixel & 0x0000FF00) >> 1) & 0x0000FF00;
                uint32_t r = ((pixel & 0x000000FF) >> 1) & 0x000000FF;
                set_pixel(surf, x, y, a | b | g | r);
            }
        }
    }
}

static void draw_mem(uint32_t* mem, SDL_Surface* surf, int64_t* loff, size_t len)
{
    size_t pixlen = len / PIXEL_BYTES;

    if (loff == NULL)
        return;

    if (*loff <= 0)
        *loff = 0;

    /* we're about to copy past the end...can we not? */

    if (*loff > (pixlen - (surf->w * surf->h)))
        *loff = (pixlen - (surf->w * surf->h));

    memcpy(surf->pixels,
           (mem + *loff),
           imin(((surf->w * surf->h) * PIXEL_BYTES), (len - (*loff * PIXEL_BYTES))));

    if (len > (surf->w * surf->h))
        draw_scrollbar(surf, *loff, len);
}

int main(int argc, char** argv, char** envp)
{
    int cont = 1;

    int w = DEFAULT_WIDTH,
        h = w;

    int fd = 0;
    size_t len = 0; /* length of mapped file in bytes, displaced by mapping offset */
    size_t pixlen = 0; /* length of mapped file in 32-bit pixels */
    int64_t loff = 0; /* linear offest from mapping 0 */
    int64_t savedloff = 0;

    uint32_t* mem = NULL;

    FILE* file = NULL;

    SDL_Event ev;
    SDL_Window* wind   = NULL;
    SDL_Surface* wsurf = NULL;

    if (argc < 2)
        return 1;

    if (argv[2] && argc >= 3)
        w = atoi(argv[2]);

    if (argv[3] && argc == 4)
        h = atoi(argv[3]);
    else
        h = w;

    file = fopen(argv[1], "r");

    if (file == NULL)
    {
        fprintf(stderr, "Unable to open %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    fseek(file, 0, SEEK_END);
    len = (size_t) ftell(file);

    pixlen = len / PIXEL_BYTES;

    /* if there's too little data to show in the window, *
     *  adjust height to fit, preserving width           */

    if (pixlen < (w * h))
        h = pixlen / w;

    fd = fileno(file);
    mem = mmap(0, len, PROT_READ, MAP_PRIVATE, fd, 0);

    if (mem == MAP_FAILED)
    {
        fprintf(stderr, "Unable to map %s: %s\n", argv[1], strerror(errno));
        return 1;
    }

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS);

    wind = SDL_CreateWindow("SDL2 Testbed",
                            SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED,
                            w, h, SDL_WINDOW_RESIZABLE);

    if (wind == NULL)
    {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }

    wsurf = SDL_GetWindowSurface(wind);

    if (wsurf == NULL)
    {
        fprintf(stderr, "%s\n", SDL_GetError());
        return 1;
    }

    int scrolling = 0;

    draw_mem(mem, wsurf, &loff, len);

    SDL_UpdateWindowSurface(wind);

    while (cont)
    {
        if (SDL_WaitEvent(&ev))
        {
            switch (ev.type)
            {
                case SDL_QUIT:
                    cont = 0;
                    break;
                case SDL_KEYDOWN:
                {
                    switch (ev.key.keysym.sym)
                    {
                        case SDLK_q:
                        case SDLK_ESCAPE: cont = 0; break;
                        case SDLK_UP: loff -= wsurf->w; break;
                        case SDLK_DOWN: loff += wsurf->w; break;
                        case SDLK_LEFT: loff--; break;
                        case SDLK_RIGHT: loff++; break;
                        case SDLK_PAGEUP: loff -= (wsurf->w * wsurf->h); break;
                        case SDLK_PAGEDOWN: loff += (wsurf->w * wsurf->h); break;
                        case SDLK_HOME: loff = 0; break;
                        case SDLK_END: loff = pixlen - (wsurf->w * wsurf->h); break;
                        case SDLK_SPACE: savedloff = loff; break;
                        case SDLK_RETURN: loff = savedloff; break;
                        default: break;
                    }

                    draw_mem(mem, wsurf, &loff, len);

                    SDL_UpdateWindowSurface(wind);
                } break;
                case SDL_WINDOWEVENT:
                {
                    if (ev.window.event == SDL_WINDOWEVENT_RESIZED ||
                        ev.window.event == SDL_WINDOWEVENT_EXPOSED)
                    {
                        wsurf = SDL_GetWindowSurface(wind);

                        if (pixlen < (wsurf->w * wsurf->h))
                        {
                            /* artificially resize window to fit again */

                            SDL_SetWindowSize(wind, wsurf->w, (pixlen / wsurf->w));

                            /* reacquiring window surface after artificial resize. *
                             * how this didn't constantly segfault before, I have  *
                             * no idea...                                          */

                            wsurf = SDL_GetWindowSurface(wind);
                        }


                        draw_mem(mem, wsurf, &loff, len);

                        SDL_UpdateWindowSurface(wind);
                    }
                } break;
                case SDL_MOUSEMOTION:
                {
                    if (ev.motion.state & SDL_BUTTON_LMASK)
                    {
                        int x, y;
                        SDL_GetMouseState(&x, &y);
                        if (x < wsurf->w - SCROLL_WIDTH && !scrolling)
                            loff -= (ev.motion.yrel * wsurf->w) + ev.motion.xrel;
                        else
                        {
                            scrolling = 1;
                            loff = (uint64_t)(((double)y / (double)wsurf->h) * (pixlen / wsurf->w - wsurf->h)) * wsurf->w;
                        }
                        draw_mem(mem, wsurf, &loff, len);
                        SDL_UpdateWindowSurface(wind);
                    }
                    else
                        scrolling = 0;
                } break;
                case SDL_MOUSEWHEEL:
                {
                    loff -= (ev.wheel.y * wsurf->w) * SCROLL_LINES;

                    draw_mem(mem, wsurf, &loff, len);

                    SDL_UpdateWindowSurface(wind);
                } break;
                default: break;
            }
        }
    }

    munmap(mem, len);

    SDL_DestroyWindow(wind);

    SDL_Quit();

    return 0;
}
