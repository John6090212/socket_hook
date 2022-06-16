#ifndef _SOCKET_HOOK_H_
#define _SOCKET_HOOK_H_

#define _GNU_SOURCE
#include <sys/mman.h>
#include <fcntl.h> // O_* constant
#include <sys/stat.h> // mode constants
#include <stdio.h>
// socket
#include <sys/socket.h>
// dlsym
#include <dlfcn.h>
// memcpy
#include <string.h>
// exit
#include <stdlib.h>
// sleep
#include <unistd.h>
// sockaddr_in
#include <netinet/in.h>
// inet_*
#include <arpa/inet.h>
// set errno
#include <errno.h>
// get random port number
#include <time.h>
// ceil
#include <math.h>
// hook function with variable-length argument
#include <stdarg.h>
// ioctl
#include <linux/sockios.h>
// struct ifreq
#include <net/if.h>
// struct arpreq
#include <net/if_arp.h>
// SIGPIPE
#include <signal.h>
// poll
#include <poll.h>
// timer in poll
#include <sys/timerfd.h>
#include <stdint.h>
// control socket
#include <sys/un.h>
#include <sys/select.h>

#include "share_queue.h"
#include "queue.h"
#include "socket.h"
#include "share.h"
#include "stack.h"
#include "list.h"
// for logging
#include "log.h"

#define PROFILING_TIME 1

enum SERVER_TYPE {DNSMASQ, TINYDTLS, DCMQRSCP, OTHER} server;

// origin function pointer
int (*original_socket)(int, int, int);
int (*original_bind)(int, const struct sockaddr *, socklen_t);
int (*original_listen)(int, int);
int (*original_accept)(int, struct sockaddr *, socklen_t *);
int (*original_connect)(int, const struct sockaddr *, socklen_t);
int (*original_close)(int);
int (*original_shutdown)(int, int);
int (*original_getsockname)(int, struct sockaddr *, socklen_t *);
ssize_t (*original_recv)(int, void *, size_t, int);
ssize_t (*original_send)(int, const void *, size_t, int);
int (*original_socketpair)(int, int, int, int [2]);
int (*original_getpeername)(int, struct sockaddr *, socklen_t *);
ssize_t (*original_read)(int fd, void *buf, size_t count);
ssize_t (*original_write)(int fd, const void *buf, size_t count);
int (*original_setsockopt)(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int (*original_getsockopt)(int sockfd, int level, int optname, void *restrict optval, socklen_t *restrict optlen);
int (*original_fcntl)(int fd, int cmd, ...);
int (*original_fcntl64)(int fd, int cmd, ...);
int (*original_ioctl)(int fd, unsigned long request, ...);
ssize_t (*original_recvfrom)(int socket, void *restrict buffer, size_t length, int flags, struct sockaddr *restrict address, socklen_t *restrict address_len);
ssize_t (*original_sendto)(int socket, const void *message, size_t length, int flags, const struct sockaddr *dest_addr, socklen_t dest_len);
ssize_t (*original_recvmsg)(int socket, struct msghdr *message, int flags);
ssize_t (*original_sendmsg)(int socket, const struct msghdr *message, int flags);
int (*original_poll)(struct pollfd *fds, nfds_t nfds, int timeout);
pid_t (*original_fork)(void);
int (*original_select)(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
FILE *(*original_fopen)(const char * pathname, const char * mode);

// queue to acquire available fd
Stack *available_fd;
// queue to acquire available share_unit
Stack *available_share_unit;
// save information of self-management socket
mysocket socket_arr[1024];
// fd for logging
int log_fd;
// for signal handler
bool poll_timeout;
timer_t poll_timer;
struct timespec hook_start_time;
// for control socket
int read_count;
int write_count;
// for smart affinity
int aflnet_cpu_id;
int next_cpu_id;
int cpu_count;

// for performance profiling
enum { NS_PER_SECOND = 1000000000 };
void sub_timespec(struct timespec t1, struct timespec t2, struct timespec *td);

// for debug
void my_log_hex(char *m, int length);

// just compare family and port
int cmp_addr(const struct sockaddr *a, const struct sockaddr *b);

// for send and recv timeout
int my_settimer(int fd, int is_send);
int my_stoptimer(int fd, int is_send);

// poll timer
void my_signal_handler(int signum);
int my_createtimer(timer_t *timer);
int my_poll_settimer(int timeout);
int my_poll_stoptimer(timer_t timer);

// initialize function
void init_share_queue(int i, bool is_stream);
void init_connect_accept_queue(void);
int init_socket(int fd, int domain, int type, int protocol);
int init_close_unit(int share_unit_index);
void init_function_pointer(void);
void init_logging(void);
void init_share_memory(void);

// hook checking function
bool is_valid_fd(int sockfd);
bool not_hook_socket_type(int domain, int type, int protocol);

// for select
unsigned long select_check_num[64];

// for tinydtls
int tinydtls_fd;
struct sockaddr_in6 tinydtls_peer_addr;

// for parallel fuzzing
bool USE_SMART_AFFINITY;

// for dcmqrscp
pid_t fork_pid;
int dcmqrscp_fd;
char *parallel_id;

#endif