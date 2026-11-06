/* Application init, screen registration and the main frame loop. */
#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <pspiofilemgr.h>

#include <stdio.h>
#include <string.h>

#include "net/net.h"
#include "httpd.h"
#include "www_bundle.h"
#include "log.h"
#include "config.h"
#include "printer.h"
#include "session.h"
#include "screen.h"
#include "key_control.h"
#include "screens.h"
#include "ui.h"
#include "worker.h"
#include "model_storage.h"
#include "splash.h"

PSP_MODULE_INFO("FLSUN Remote", 0, 1, 5);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER);
PSP_HEAP_SIZE_KB(-16384);
PSP_MAIN_THREAD_STACK_SIZE_KB(512);

static int exit_callback(int arg1, int arg2, void *common)
{
	(void)arg1;
	(void)arg2;
	(void)common;
	sceKernelExitGame();
	return 0;
}

static int callback_thread(SceSize args, void *argp)
{
	int cbid;
	(void)args;
	(void)argp;
	cbid = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
	sceKernelRegisterExitCallback(cbid);
	sceKernelSleepThreadCB();
	return 0;
}

static int setup_callbacks(void)
{
	int thid = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, NULL);
	if (thid >= 0)
		sceKernelStartThread(thid, 0, 0);
	return thid;
}

static void dash_refresh_local_ip(void)
{
	net_refresh_local_ip(cfg_printer_ip());
}

static void dash_http_start(void)
{
	httpd_start(cfg_http_port());
}

static void dash_apply_log_config(void)
{
	log_apply_config(cfg_log_mode(), cfg_log_port());
}

int main(void)
{
	static const struct dash_ops dash_ops = {
		.refresh_local_ip = dash_refresh_local_ip,
		.local_ip = net_local_ip,
		.http_status = httpd_status,
		.http_listening = httpd_listening,
		.http_start = dash_http_start,
		.http_stop = httpd_stop,
		.apply_log_config = dash_apply_log_config,
	};
	struct session *ss;
	struct printer *pr;
	int net_err, frames = 0;
    unsigned requests_seen=0;
    int batch_error=0;
#ifdef FLSUN_DEBUG
	int autodump = 0;
	int dump_frame = 0;
#endif

	setup_callbacks();
	pspDebugScreenInit();
	pspDebugScreenSetBackColor(0x00182028);
	pspDebugScreenSetTextColor(0x00FFFFFF);
	pspDebugScreenClear();
    splash_show("Starting",0,0,0,0,"",0);
	sceCtrlSetSamplingCycle(0);
	sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
	key_poll();

	session_init();
	printer_init();
	printer_set_upload_progress(models_upload_progress, NULL);
	ss = session_get();
	pr = printer_get();
	cfg_load();
    model_storage_init();
	log_init();
	log_apply_config(cfg_log_mode(), cfg_log_port());
	LOG_I("FLSUN Remote start log=%s port=%d",
	      cfg_log_mode() == LOG_MODE_NET ? "net" : "off", cfg_log_port());
	if (www_bundle_ensure(splash_progress,NULL) != 0) {
        session_set_status("www update FAIL");
        LOG_E("www update failed; waiting for acknowledgment");
        /* Keep the real error frame visible until the user decides to continue. */
        for (;;) {
            SceCtrlData err_pad;

            sceCtrlReadBufferPositive(&err_pad, 1);
            if (err_pad.Buttons & PSP_CTRL_START) {log_term();sceKernelExitGame();return 1;}
            if (err_pad.Buttons & PSP_CTRL_CROSS) break;
            sceDisplayWaitVblankStart();
        }
    }

	splash_show("Starting network",0,0,0,0,"",0);
	net_err = net_init();
	if (net_err == 0)
		net_refresh_local_ip(cfg_printer_ip());
	if (cfg_local_ip()[0])
		net_set_local_ip_override(cfg_local_ip());
	if (net_err != 0)
		session_set_statusf("net err %08X", (unsigned)net_err);
	if (net_ready()) {
        if (net_worker_start()!=0) session_set_status("worker start FAIL");
        httpd_start(cfg_http_port());
    }
	log_apply_config(cfg_log_mode(), cfg_log_port());
	dash_set_ops(&dash_ops);

	screens_register(&models_screen);
	screens_register(&tune_screen);
	screens_register(&dash_screen);
	screens_register(&preview_screen);
	screens_register(&confirm_screen);
	screen_go(&models_screen);

#ifdef FLSUN_DEBUG
	{
		SceUID fd = sceIoOpen("ms0:/PSP/GAME/FLSUNRemote/ui_autodump.flag", PSP_O_RDONLY, 0);
		if (fd >= 0) {
			sceIoClose(fd);
			autodump = 1;
			LOG_I("ui_autodump.flag present");
		}
	}
#endif

	while (1) {
		const struct screen *current;

		key_poll();
        char selected_name[PRINTER_NAME_LEN]="";
        if (ss->sel>=0 && ss->sel<pr->files.count)
            snprintf(selected_name,sizeof(selected_name),"%s",pr->files.items[ss->sel].name);
        if (printer_poll()) {
            for (int i=0;i<pr->files.count;i++) {
                if (!strcmp(pr->files.items[i].name,selected_name)) { ss->sel=i; break; }
            }
            models_clamp_selection();
            models_sync_selection_size();
        }
        if (requests_seen!=pr->requests_version) {
            requests_seen=pr->requests_version; batch_error=0;
        }
        struct printer_result result;
        while (printer_next_result(&result)) {
            if (printer_poll()) {models_clamp_selection();models_sync_selection_size();}
            if (result.rc!=PRINTER_OK) {
                if (!batch_error) session_set_statusf("%s FAIL: %s",result.label,result.error);
                batch_error=1;
            } else {
                if (result.type==CMD_PAUSE) ss->ui_paused=1;
                if (result.type==CMD_RESUME || result.type==CMD_PRINT) ss->ui_paused=0;
                if (!batch_error) {
                    if (result.error[0]) session_set_statusf("%s: %s",result.label,result.error);
                    else session_set_statusf("%s OK",result.label);
                }
            }
        }
        httpd_poll();
		log_poll();
		frames++;

#ifdef FLSUN_DEBUG
		dump_frame = 0;
		if (autodump && frames > 90 && (frames % 120) == 0) {
			static int phase;
			const struct screen *tabs[3] = { &models_screen, &tune_screen, &dash_screen };
			screen_go(tabs[phase % 3]);
			phase++;
			dump_frame = 1;
		}
#endif
		if (!ss->did_list && net_ready() && !session_any_busy() && frames > 35) {
			ss->did_list = 1;
			if (printer_request_files() == PRINTER_OK)
				session_set_status("initial refresh queued");
			else
				session_set_statusf("list FAIL: %s", pr->last_error);
			ss->sel = 0;
			ss->scroll_row = 0;
			ss->sel_mark = -1;
			models_sync_selection_size();
			printer_request_status();
		}
		if (key_just(KEY_START))
			break;
		if (key_just(KEY_L) || key_just(KEY_R)) {
			static const struct screen *tabs[3] =
				{ &models_screen, &tune_screen, &dash_screen };
			static const char *tab_names[3] = { "models", "tune", "dash" };
			const struct screen *cur = screen_current();
			int i, next, dir;

			for (i = 0; i < 3; i++)
				if (tabs[i] == cur)
					break;
			dir = key_just(KEY_R) ? 1 : -1;
			if (i < 3) {
				switch (i) {
				case 0: next = dir > 0 ? 1 : 2; break;
				case 1: next = dir > 0 ? 2 : 0; break;
				default: next = dir > 0 ? 0 : 1; break;
				}
				screen_go(tabs[next]);
				session_set_status(tab_names[next]);
#ifdef FLSUN_DEBUG
				dump_frame = 1;
#endif
			}
		}

		current = screen_current();
		if (current && current->input)
			current->input();
		ui_begin();
		current = screen_current();
		if (current && current->draw)
			current->draw();
		ui_end();

#ifdef FLSUN_DEBUG
		if (dump_frame && autodump) {
			char path[96];
			snprintf(path, sizeof(path), "ms0:/PSP/GAME/FLSUNRemote/ui_%d.bmp", ss->screen);
			ui_dump_bmp(path);
			ui_dump_bmp("ms0:/PSP/GAME/FLSUNRemote/ui_frame.bmp");
		}
#endif
		sceDisplayWaitVblankStart();
	}

	net_term();
	sceKernelExitGame();
	return 0;
}
