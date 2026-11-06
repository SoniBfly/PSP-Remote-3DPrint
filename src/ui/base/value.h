#ifndef FLSUN_UI_VALUE_H
#define FLSUN_UI_VALUE_H

typedef struct {
	const char *label;
	int *value;
	int step;
	int min;
	int max;
} UiValue;

void ui_value_init(UiValue *item, const char *label, int *value,
		   int step, int min, int max);
void ui_value_draw(const UiValue *item, int y, int selected);
int ui_value_handle(UiValue *item, int direction);

#endif
