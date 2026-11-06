/*
 * Low-level Inet helpers: connect with timeout, recv until quiet/deadline,
 * reliable send_all.
 */
#ifndef FLSUN_SOCKET_H
#define FLSUN_SOCKET_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Worker-only task deadline; HTTP keeps independent per-call deadlines. */
int sock_runtime_init(void);
void sock_runtime_shutdown(void);
int sock_stopping(void);
void sock_worker_register(int thread_id);
uint64_t sock_limit_begin(int timeout_ms);
void sock_limit_end(uint64_t previous);
void sock_task_begin(uint32_t timeout_ms);
void sock_task_end(void);
int sock_task_expired(void);
uint64_t sock_now_ms(void);
int sock_remaining_ms(int limit_ms);

void sock_set_nonblock(int fd, int on);

/* Parse "a.b.c.d" into 4 octets. 0 = ok, -1 = bad. */
int sock_parse_ipv4(const char *s, unsigned char out[4]);

/*
 * Non-blocking TCP connect with poll wait.
 * Returns fd >= 0 on success, or negative error codes (-1 bad ip, -2 timeout, -3 so_error).
 */
int sock_connect_timeout(const char *ip, int port, int timeout_ms);

/*
 * Recv until quiet_ms of silence, total timeout_ms, or early_stop substring in buffer.
 * Returns bytes received (0 if none).
 */
int sock_recv_deadline(int fd, char *buf, int cap, int timeout_ms,
		       const char *early_stop, int quiet_ms);

/*
 * Send the full buffer. Returns 0 on success, -1 if short send / error / timeout.
 * Retries with short delays until deadline (~30s) rather than giving up after one retry.
 */
int send_all(int fd, const void *buf, int len);

#ifdef __cplusplus
}
#endif

#endif /* FLSUN_SOCKET_H */
