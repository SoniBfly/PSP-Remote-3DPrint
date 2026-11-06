#ifndef FLSUN_UI_TEXT_H
#define FLSUN_UI_TEXT_H

/* scale: 1 = 8px, 2 = 16px (readable). */
void ui_text(int x, int y, int scale, unsigned int abgr, const char *s);
int ui_text_width(int scale, const char *s);

#endif
