#include "progress.h"
#include "render.h"

void ui_progress_bar(int x, int y, int w, int h, int permille)
{
	int fillw;

	if (permille < 0)
		permille = 0;
	if (permille > 1000)
		permille = 1000;
	ui_begin_direct();
	ui_fill_round(x, y, w, h, UI_COL_PANEL);
	ui_rect(x, y, w, h, UI_COL_BORDER);
	fillw = ((w - 4) * permille) / 1000;
	if (fillw > 0)
		ui_fill(x + 2, y + 2, fillw, h - 4, UI_COL_ACCENT);
	ui_end_direct();
}
