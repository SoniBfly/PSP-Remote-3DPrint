#include "screens.h"
#include "printer.h"
#include "session.h"

#include <stdio.h>
#include <string.h>

void model_label(const char *filename, char *out, int outlen)
{
	int n;
	const char *dot;

	if (!filename || outlen < 2) {
		if (outlen > 0)
			out[0] = 0;
		return;
	}
	n = (int)strlen(filename);
	dot = strrchr(filename, '.');
	if (dot) {
		int elen = (int)strlen(dot);
		if ((elen == 6 && (dot[1] == 'g' || dot[1] == 'G') &&
		     (dot[2] == 'c' || dot[2] == 'C') &&
		     (dot[3] == 'o' || dot[3] == 'O') &&
		     (dot[4] == 'd' || dot[4] == 'D') &&
		     (dot[5] == 'e' || dot[5] == 'E')) ||
		    (elen == 4 && (dot[1] == 'g' || dot[1] == 'G') &&
		     (dot[2] == 'c' || dot[2] == 'C') &&
		     (dot[3] == 'o' || dot[3] == 'O')) ||
		    (elen == 2 && (dot[1] == 'g' || dot[1] == 'G')))
			n = (int)(dot - filename);
	}
	if (n <= 0) {
		snprintf(out, outlen, "%s", filename);
		return;
	}
	if (n >= outlen) {
		snprintf(out, outlen, "%.*s...", outlen - 4, filename);
		return;
	}
	memcpy(out, filename, (size_t)n);
	out[n] = 0;
}

void models_sync_selection_size(void)
{
	struct session *ss = session_get();
	const struct printer *pr = printer_get();

	if (pr->files.count <= 0 || ss->sel < 0 || ss->sel >= pr->files.count) {
		ss->sel_size = -1;
		ss->sel_size_for = -1;
		return;
	}
	ss->sel_size = pr->files.items[ss->sel].size;
	ss->sel_size_for = ss->sel;
}

void models_clamp_selection(void)
{
	struct session *ss = session_get();
	int count = printer_get()->files.count;
	int sel_row;

	if (count <= 0) {
		ss->sel = 0;
		ss->scroll_row = 0;
		return;
	}
	if (ss->sel >= count)
		ss->sel = count - 1;
	if (ss->sel < 0)
		ss->sel = 0;
	sel_row = ss->sel / 2;
	if (sel_row < ss->scroll_row)
		ss->scroll_row = sel_row;
	if (sel_row >= ss->scroll_row + 3)
		ss->scroll_row = sel_row - 3 + 1;
}
