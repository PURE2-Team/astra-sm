/*
 * Astra Core
 * http://cesbo.com/astra
 *
 * Copyright (C) 2012-2013, Andrey Dyldin <and@cesbo.com>
 * Licensed under the MIT license.
 */

#ifndef _ASC_H_
#define _ASC_H_ 1

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#define ARRAY_SIZE(_a) (sizeof(_a)/sizeof(_a[0]))
#define UNUSED_ARG(_x) (void)_x

/* event.c */

typedef struct event_s event_t;

typedef enum
{
    EVENT_NONE      = 0x00,
    EVENT_READ      = 0x01,
    EVENT_WRITE     = 0x02,
    EVENT_ERROR     = 0xF0
} event_type_t;

void event_observer_init(void);
void event_observer_loop(void);
void event_observer_destroy(void);

event_t * event_attach(int fd, event_type_t type, void (*callback)(void *, int), void *arg);
void event_detach(event_t *event);

/* timer.c */

void timer_action(void);
void timer_destroy(void);

void timer_one_shot(unsigned int, void (*)(void *), void *);

void * timer_attach(unsigned int, void (*)(void *), void *);
void timer_detach(void *);

/* list.c */

typedef struct list_s list_t;

list_t * list_init(void);
void list_destroy(list_t *list);

void list_first(list_t *list);
void list_next(list_t *list);
int list_is_data(list_t *list);
void * list_data(list_t *list);

void list_insert_head(list_t *list, void *data);
void list_insert_tail(list_t *list, void *data);

void list_remove_current(list_t *list);
void list_remove_item(list_t *list, void *data);

#define list_for(__list) for(list_first(__list); list_is_data(__list); list_next(__list))

/* log.c */

void log_set_stdout(int);
void log_set_debug(int);
void log_set_file(const char *);
#ifndef _WIN32
void log_set_syslog(const char *);
#endif

void log_hup(void);
void log_destroy(void);

void log_info(const char *, ...);
void log_error(const char *, ...);
void log_warning(const char *, ...);
void log_debug(const char *, ...);

/* socket.c */

enum
{
    SOCKET_FAMILY_IPv4      = 0x00000001,
    SOCKET_FAMILY_IPv6      = 0x00000002,
    SOCKET_PROTO_TCP        = 0x00000004,
    SOCKET_PROTO_UDP        = 0x00000008,
    SOCKET_REUSEADDR        = 0x00000010,
    SOCKET_BLOCK            = 0x00000020,
    SOCKET_BROADCAST        = 0x00000040,
    SOCKET_LOOP_DISABLE     = 0x00000080,
    SOCKET_BIND             = 0x00000100,
    SOCKET_CONNECT          = 0x00000200,
    SOCKET_NO_DELAY         = 0x00000400,
    SOCKET_KEEP_ALIVE       = 0x00000800
};

enum
{
    SOCKET_SHUTDOWN_RECV    = 1,
    SOCKET_SHUTDOWN_SEND    = 2,
    SOCKET_SHUTDOWN_BOTH    = 3
};

void socket_init(void);
void socket_destroy(void);

int socket_open(int, const char *, int);
int socket_shutdown(int, int);
void socket_close(int);
char * socket_error(void);

int socket_options_set(int, int);
int socket_port(int);

int socket_accept(int, char *, int *);

int socket_set_buffer(int, int, int);
int socket_set_timeout(int, int, int);

int socket_multicast_join(int, const char *, const char *);
int socket_multicast_leave(int, const char *, const char *);
int socket_multicast_renew(int, const char *, const char *);
int socket_multicast_set_ttl(int, int);
int socket_multicast_set_if(int, const char *);

ssize_t socket_recv(int, void *, size_t);
ssize_t socket_send(int, void *, size_t);

void * socket_sockaddr_init(const char *, int);
void socket_sockaddr_destroy(void *sockaddr);

ssize_t socket_recvfrom(int, void *, size_t, void *);
ssize_t socket_sendto(int, void *, size_t, void *);

/* stream.c */

typedef struct stream_s stream_t;

stream_t * stream_init(void (*)(void *), void *);
void stream_destroy(stream_t *);

ssize_t stream_send(stream_t *, void *, size_t);
ssize_t stream_recv(stream_t *, void *, size_t);

/* thread.c */

typedef struct thread_s thread_t;

int thread_init(thread_t **, void (*)(void *), void *);
void thread_destroy(thread_t **);

int thread_is_started(thread_t *);

/* */

#define ASC_INIT()                                                          \
    socket_init();                                                          \
    event_init();

#define ASC_LOOP()                                                          \
    while(1)                                                                \
    {                                                                       \
        event_action();                                                     \
        timer_action();                                                     \
    }

#define ASC_DESTROY()                                                       \
    timer_destroy();                                                        \
    event_destroy();                                                        \
    socket_destroy();                                                       \
    log_info("[main] exit");                                                \
    log_destroy();

#endif /* _ASC_H_ */