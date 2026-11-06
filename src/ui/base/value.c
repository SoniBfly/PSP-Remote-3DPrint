#include "value.h"
#include "widget.h"

#include <stdio.h>

void ui_value_init(UiValue *item, const char *label, int *value,
		   int step, int min, int max)
{
	if (!item)
		return;
	item->label = label;
	item->value = value;
	item->step = step;
	item->min = min;
	item->max = max;
}

void ui_value_draw(const UiValue *item, int y, int selected)
{
	char text[24];

	if (!item || !item->value)
		return;
	snprintf(text, sizeof(text), "%d", *item->value);
	ui_draw_tune_row(y, selected, item->label, text);
}

int ui_value_handle(UiValue *item, int direction)
{
	int next;

	if (!item || !item->value || !direction)
		return 0;
	next = *item->value + (direction < 0 ? -item->step : item->step);
	if (next < item->min)
		next = item->min;
	if (next > item->max)
		next = item->max;
	if (next == *item->value)
		return 0;
	*item->value = next;
	return 1;
}
