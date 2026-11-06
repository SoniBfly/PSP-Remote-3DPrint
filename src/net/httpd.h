/*
 * Non-blocking HTTP server on the PSP: Load UI, inbox JSON, upload/preview.
 */
#ifndef FLSUN_HTTPD_H
#define FLSUN_HTTPD_H

#ifdef __cplusplus
extern "C" {
#endif

int  httpd_start(int port);
void httpd_stop(void);
void httpd_shutdown(void);
void httpd_poll(void);
int  httpd_listening(void);
const char *httpd_status(void);   /* short line for UI */
const char *httpd_last_event(void);

/* Update "listen IP:port" status after net_refresh_local_ip. */
void httpd_note_local_ip(const char *ip);

#ifdef __cplusplus
}
#endif

#endif /* FLSUN_HTTPD_H */
