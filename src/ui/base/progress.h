#ifndef FLSUN_UI_PROGRESS_H
#define FLSUN_UI_PROGRESS_H

/* Direct framebuffer progress bar for blocking transfers; permille is 0..1000. */
void ui_progress_bar(int x, int y, int w, int h, int permille);

#endif
