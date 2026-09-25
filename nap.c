/*
 * Larn — Copyright © 1986 Noah Morgan
 *        Copyright © 2014-2026 Gibbon
 *
 * This file is part of Larn and is distributed under
 * The Noah Licence, Version 1.0.
 *
 * You may use, modify, and redistribute this code for
 * non‑commercial purposes, provided that:
 *   - this notice is preserved,
 *   - The Noah Licence accompanies all redistributions, and
 *   - no profit is made from Larn or derivative works
 *     without explicit permission from the copyright holder.
 *
 * Larn is provided “AS IS”, without warranty of any kind.
 *
 * See the 'LICENSE.txt' file in the 'docs' folder.
 */

#include "larnfunc.h"
#include "io.h"
#include "nap.h"
#include <time.h>

#ifdef LARN_X11
/* RVIP port: sleep instead of spinning (the command loop naps between key
 * polls, so the busy loop kept one core at 100% while idle) */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
void
nap(int milliseconds)
{
    emscripten_sleep((unsigned)milliseconds);
}
#else
#include <unistd.h>
void
nap(int milliseconds)
{
    usleep((useconds_t)milliseconds * 1000);
}
#endif
#else
void
nap(int milliseconds)
{
    clock_t start, now, ticks;
    ticks = (clock_t)((long)milliseconds * (long)CLOCKS_PER_SEC / 1000L);

    start = clock();
    do {
        now = clock();
    } while ((clock_t)(now - start) < ticks);
}
#endif
