#ifndef FLSUN_NET_WORKER_H
#define FLSUN_NET_WORKER_H

#include <stdint.h>
#include "printer.h"

enum net_task_type {
	CMD_TEMPS = 1,
	CMD_STATUS,
	CMD_FILES,
	CMD_PRINT,
	CMD_UPLOAD,
	CMD_DELETE_SD,
	CMD_DELETE_PSP,
	CMD_PAUSE,
	CMD_RESUME,
	CMD_HOME,
	CMD_SET_HOTEND,
	CMD_SET_BED,
	CMD_SET_SPEED,
	CMD_SET_FLOW
};

struct net_task {
	int type;
	struct file_entry entry;
	char name[PRINTER_NAME_LEN];
	int value;
	int and_print;
	uint32_t delay_ms;
	uint32_t interval_ms;
	uint32_t timeout_ms;
	uint32_t due_ms;
	int used;
	int running;
};

int net_worker_start(void);
void net_worker_stop(void);
int net_worker_submit(int type, const char *name, const struct file_entry *entry,
		      int and_print, int value, uint32_t delay_ms,
		      uint32_t interval_ms, uint32_t timeout_ms);
int net_worker_pending(void);

#endif
