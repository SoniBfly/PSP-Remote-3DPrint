#include "worker.h"
#include "log.h"
#include "socket.h"

#include <pspkernel.h>
#include <pspthreadman.h>
#include <stdio.h>
#include <string.h>

#define WORKER_QUEUE_LEN 12

static struct net_task s_queue[WORKER_QUEUE_LEN];
static SceLwMutexWorkarea s_lock;
static SceUID s_wake = -1;
static SceUID s_thread = -1;
static int s_pending, s_stop;

static uint32_t now_ms(void)
{
	return (uint32_t)(sceKernelGetSystemTimeWide() / 1000);
}

static int due_before(uint32_t a, uint32_t b)
{
	return (int32_t)(a - b) < 0;
}

static int worker_main(SceSize args, void *argp)
{
	(void)args;
	(void)argp;
	for (;;) {
		struct net_task task;
		int pick = -1;
		int i;
		uint32_t now = now_ms();
		uint32_t wait_ms = 250;

		sceKernelLockLwMutex(&s_lock, 1, NULL);
        if (s_stop) {sceKernelUnlockLwMutex(&s_lock,1);break;}
		for (i = 0; i < WORKER_QUEUE_LEN; i++) {
			if (!s_queue[i].used || s_queue[i].running)
				continue;
			if (pick < 0 || due_before(s_queue[i].due_ms, s_queue[pick].due_ms))
				pick = i;
		}
		if (pick >= 0 && !due_before(now, s_queue[pick].due_ms)) {
			s_queue[pick].running = 1;
			task = s_queue[pick];
		} else {
			if (pick >= 0) {
				uint32_t delta = s_queue[pick].due_ms - now;
				wait_ms = delta < wait_ms ? delta : wait_ms;
			}
			sceKernelUnlockLwMutex(&s_lock, 1);
			{
				SceUInt timeout = wait_ms * 1000;
				sceKernelWaitSema(s_wake, 1, &timeout);
			}
			continue;
		}
		sceKernelUnlockLwMutex(&s_lock, 1);

        if (task.interval_ms) printer_queue_push();
        sock_task_begin(task.timeout_ms);
        printer_worker_execute(&task);
        sock_task_end();
        if (task.interval_ms) printer_queue_done();

		sceKernelLockLwMutex(&s_lock, 1, NULL);
		if (task.interval_ms) {
			s_queue[pick].running = 0;
			s_queue[pick].due_ms = now_ms() + task.interval_ms;
			s_queue[pick].delay_ms = 0;
		} else {
			memset(&s_queue[pick], 0, sizeof(s_queue[pick]));
			s_pending--;
			printer_queue_done();
		}
		sceKernelUnlockLwMutex(&s_lock, 1);
	}
	return 0;
}

int net_worker_start(void)
{
	if (s_thread >= 0)
		return 0;
	memset(s_queue, 0, sizeof(s_queue));
    s_stop=s_pending=0;
	if (sceKernelCreateLwMutex(&s_lock, "net_worker_q", 0, 0, NULL) < 0)
		return -1;
	s_wake = sceKernelCreateSema("net_worker_wake", 0, 0, WORKER_QUEUE_LEN, NULL);
	if (s_wake < 0) {sceKernelDeleteLwMutex(&s_lock); return -1;}
	s_thread = sceKernelCreateThread("net_worker", worker_main, 0x18, 32 * 1024, 0, NULL);
	if (s_thread < 0) {sceKernelDeleteSema(s_wake);sceKernelDeleteLwMutex(&s_lock);return -1;}
	sock_worker_register(s_thread);
    int rc=sceKernelStartThread(s_thread,0,NULL);
    if (rc<0) {sceKernelDeleteThread(s_thread);sceKernelDeleteSema(s_wake);sceKernelDeleteLwMutex(&s_lock);s_thread=-1;}
    return rc;
}

int net_worker_submit(int type, const char *name, const struct file_entry *entry,
		      int and_print, int value, uint32_t delay_ms,
		      uint32_t interval_ms, uint32_t timeout_ms)
{
	int i, slot = -1;
    if (delay_ms>0x7fffffffu || interval_ms>0x7fffffffu || type<CMD_TEMPS || type>CMD_SET_FLOW) return -1;
	struct net_task *task;

	if (s_thread < 0 && net_worker_start() != 0)
		return -1;
	sceKernelLockLwMutex(&s_lock, 1, NULL);
	for (i = 0; i < WORKER_QUEUE_LEN; i++) {
		if (!s_queue[i].used) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		sceKernelUnlockLwMutex(&s_lock, 1);
		return -1;
	}
	task = &s_queue[slot];
	memset(task, 0, sizeof(*task));
	task->type = type;
	task->and_print = and_print;
	task->value = value;
	task->delay_ms = delay_ms;
	task->interval_ms = interval_ms;
	task->timeout_ms = timeout_ms;
	task->due_ms = now_ms() + delay_ms;
	task->used = 1;
	if (name)
		snprintf(task->name, sizeof(task->name), "%s", name);
	if (entry)
		task->entry = *entry;
	s_pending++;
	if (!interval_ms) printer_queue_push();
	sceKernelUnlockLwMutex(&s_lock, 1);
	sceKernelSignalSema(s_wake, 1);
	return 0;
}

int net_worker_pending(void)
{
    if (s_thread<0) return 0;
	int count;
    sceKernelLockLwMutex(&s_lock,1,NULL);
    count=s_pending;
    sceKernelUnlockLwMutex(&s_lock,1);
    return count;
}

void net_worker_stop(void)
{
    if (s_thread<0) return;
    sceKernelLockLwMutex(&s_lock,1,NULL);s_stop=1;sceKernelUnlockLwMutex(&s_lock,1);
    sceKernelSignalSema(s_wake,1);
    sceKernelWaitThreadEnd(s_thread,NULL);
    sceKernelDeleteThread(s_thread);s_thread=-1;
    sceKernelDeleteSema(s_wake);s_wake=-1;
    sceKernelDeleteLwMutex(&s_lock);
}
