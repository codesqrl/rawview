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

#define XRES 512
#define YRES 512
#define BPP  32

static inline long imin(long x, long y)
{
    return x < y ? x : y;
}

static void draw_mem(uint32_t* mem, SDL_Surface* surf, int64_t* loff, size_t len)
{
    if (loff == NULL)
        return;

    if (*loff <= 0)
        *loff = 0;

    /* we're about to copy past the end...can we not? */

    if (*loff > ((len / (BPP / 8) - (surf->w * surf->h))))
        *loff = ((len / (BPP / 8) - (surf->w * surf->h)));

    /* possibly the world's ugliest memcpy.    *
     *                                         *
     * the gotcha here is pointer arithmetic;  *
     * 'loff' is the offset in *pixels*, but   *
     * memcpy works in *bytes*. the 2nd arg    *
     * specifies the amount of offset into the *
     * mapped memory region in uint32_t's, but *
     * the last arg needs to be a length in    *
     * bytes, so we need to multiply by the    *
     * pixel width.                            */

    memcpy(surf->pixels,
           (mem + *loff),
           imin(((surf->w * surf->h) * (BPP / 8)), (len - (*loff * (BPP / 8)))));
}

int main(int argc, char** argv, char** envp)
{
    int cont = 1;

    int w = XRES,
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

    pixlen = len / (BPP / 8);

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
                        case SDLK_END: loff = pixlen - (wsurf->w * wsurf->h);
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
                        loff -= (ev.motion.yrel * wsurf->w) + ev.motion.xrel;

                        draw_mem(mem, wsurf, &loff, len);

                        SDL_UpdateWindowSurface(wind);
                    }
                } break;
                case SDL_MOUSEWHEEL:
                {
                    loff -= (ev.wheel.y * wsurf->w) * 50;

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
