/* The code below is largely based on ae_epoll.c file from redis project.
 * 
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010 Mateusz Armatys
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *   * Redistributions of source code must retain the above copyright notice,
 *     this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *   * Neither the name of Redis nor the names of its contributors may be used
 *     to endorse or promote products derived from this software without
 *     specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdlib.h>
#include <sys/types.h>
#include <time.h>
#include <sys/timerfd.h>
#include <sys/epoll.h>
#include "hoploop.h"

typedef struct snApiState {
    int epfd;
    struct epoll_event events[SN_SETSIZE];
} snApiState;

static snHopLoop *createLoop() {
    snHopLoop *loop = malloc(sizeof(snHopLoop));
    snLoopApi *api = malloc(sizeof(snLoopApi));
    snApiState *state = malloc(sizeof(snApiState));
    
    if (! (loop && api)) {
        return NULL;
    }
    
    loop->api = api;
    loop->state = state;
    
    api->name = "epoll";
    
    if (init(loop) < 0) {
        return NULL;
    }
    
    return loop;
}

static int init(struct snHopLoop * hloop) {
    int epfd = epoll_create(1024); /* 1024 is just an hint for the kernel */
    snApiState *state = hloop->state;
    if (epfd == -1) return -1;
    state->epfd = epfd;
    
    return 0;
}

static int addEvent(struct snHopLoop *hloop, int fd, int mask) {
    snApiState *state = hloop->state;
    struct epoll_event ee;
    /* If the fd was already monitored for some event, we need a MOD
     * operation. Otherwise we need an ADD operation. */
    int op = hloop->events[fd].mask == SN_NONE ?
            EPOLL_CTL_ADD : EPOLL_CTL_MOD;

    ee.events = 0;
    mask |= hloop->events[fd].mask; /* Merge old events */
    if (mask & SN_READABLE) ee.events |= EPOLLIN;
    if (mask & SN_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0; /* avoid valgrind warning */
    ee.data.fd = fd;
    if (epoll_ctl(state->epfd,op,fd,&ee) == -1) return -1;
    return 0;
}

static int removeEvent(struct snHopLoop *hloop, int fd, int delmask) {
    snApiState *state = hloop->state;
    struct epoll_event ee;
    int mask = hloop->events[fd].mask & (~delmask);

    ee.events = 0;
    if (mask & SN_READABLE) ee.events |= EPOLLIN;
    if (mask & SN_WRITABLE) ee.events |= EPOLLOUT;
    ee.data.u64 = 0; /* avoid valgrind warning */
    ee.data.fd = fd;
    if (mask != SN_NONE) {
        epoll_ctl(state->epfd,EPOLL_CTL_MOD,fd,&ee);
    } else {
        /* Note, Kernel < 2.6.9 requires a non null event pointer even for
         * EPOLL_CTL_DEL. */
        epoll_ctl(state->epfd,EPOLL_CTL_DEL,fd,&ee);
    }
    
    return 0;
}

static int setTimeout(struct snHopLoop *hloop, struct timeval *tvp) {
    int fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (fd == -1) return -1;
    struct itimerspec new_val;
    
    new_val.it_interval.tv_sec = 0;
    new_val.it_interval.tv_nsec = 0;
    new_val.it_value.tv_sec = tvp->tv_sec;
    new_val.it_value.tv_sec = tvp->usec * 1000;
    
    if (timerfd_settime(fd, 0, &new_val, NULL) == -1) return -1;
    
    struct epoll_event ee;
    int op = EPOLL_CTL_ADD;
    
    ee.events = 0;
    ee.events |= EPOLLIN;
    ee.data.u64 = 0; /* avoid valgrind warning */
    ee.data.fd = fd;
    if (epoll_ctl(state->epfd,op,fd,&ee) == -1) return -1;
    
    return fd;
}

static int setInterval(struct snHopLoop *hloop, struct timeval *tvp) {
    int fd = timerfd_create(CLOCK_MONOTONIC, 0);
    if (fd == -1) return -1;
    struct itimerspec new_val;
    
    new_val.it_interval.tv_sec = tvp->tv_sec;
    new_val.it_interval.tv_nsec = tvp->usec * 1000;
    new_val.it_value.tv_sec = tvp->tv_sec;
    new_val.it_value.tv_sec = tvp->usec * 1000;
    
    if (timerfd_settime(fd, 0, &new_val, NULL) == -1) return -1;
    
    return fd;
}

static int clearTimer(struct snHopLoop *hloop, int fd) {
    close(fd);
    
    return 0;
}

static int poll(struct snHopLoop *hloop, struct timeval *tvp) {
    snApiState *state = hloop->state;
    int retval, numevents = 0;

    retval = epoll_wait(state->epfd,state->events,SN_SETSIZE,
            tvp ? (tvp->tv_sec*1000 + tvp->tv_usec/1000) : -1);
    if (retval > 0) {
        int j;

        numevents = retval;
        for (j = 0; j < numevents; j++) {
            int mask = 0;
            struct epoll_event *e = state->events+j;

            if (e->events & EPOLLIN) mask |= SN_READABLE;
            if (e->events & EPOLLOUT) mask |= SN_WRITABLE;
            printf("evType: %d\n", e-events);
            hloop->fired[j].fd = e->data.fd;
            hloop->fired[j].mask = mask;
        }
    }
    
    return numevents;
}
