/* The code below is largely based on ae_kqueue.c file from redis project.
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
#include <sys/event.h>
#include <sys/time.h>
#include "hoploop.h"

typedef struct snApiState {
    int kqfd;
    struct kevent events[SN_SETSIZE];
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
    
    api->name = "kqueue";
    
    if (init(loop) < 0) {
        return NULL;
    }
    
    return loop;
}

static int init(struct snHopLoop *hloop) {
    int kqfd = kqueue();
    if (kqfd == -1) return -1;
    
    snApiState *state = hloop->state;
    state->kqfd = kqfd;
    
    return 0;
}

static int addEvent(struct snHopLoop *hloop, int fd, int mask) {
    snApiState *state = hloop->state;
    int kqfd = state->kqfd;
    struct kevent ke;
    
    if (mask & SN_READABLE) {
        EV_SET(&ke, fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
        if (kevent(kqfd, &ke, 1, NULL, 0, NULL) == -1) return -1;
    }
    if (mask & SN_WRITABLE) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_ADD, 0, 0, NULL);
        if (kevent(kqfd, &ke, 1, NULL, 0, NULL) == -1) return -1;
    }

    return 0;
}

static int removeEvent(struct snHopLoop *hloop, int fd, int mask) {
    snApiState *state = hloop->state;
    int kqfd = state->kqfd;
    struct kevent ke;

    if (mask & SN_READABLE) {
        EV_SET(&ke, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
        kevent(kqfd, &ke, 1, NULL, 0, NULL);
    }
    if (mask & SN_WRITABLE) {
        EV_SET(&ke, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
        kevent(kqfd, &ke, 1, NULL, 0, NULL);
    }
    
    return 0;
}

/* Timer has two types: timeouts and intervals (as in JavaScript) */
static int setTimer(struct snHopLoop *hloop, int fd, struct timeval *tvp, int flags) {
    snApiState *state = hloop->state;
    int kqfd = state->kqfd;
    struct kevent ke;
    
    if ((flags & EV_ADD) && tvp) { /* add timer */
        long int millis = tvp->tv_sec * 1000 + tvp->tv_usec / 1000;
        EV_SET(&ke, fd, EVFILT_TIMER, flags, 0, millis, NULL);
        kevent(kqfd, &ke, 1, NULL, 0, NULL);
    } else if (flags & EV_DELETE) { /* clear timer */
        EV_SET(&ke, fd, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
        kevent(kqfd, &ke, 1, NULL, 0, NULL);
    }
    
    return 0;
}

static int setTimeout(struct snHopLoop *hloop, int fd, struct timeval *tvp) {
    return setTimer(hloop, fd, tvp, EV_ADD|EV_ONESHOT);
}

static int setInterval(struct snHopLoop *hloop, int fd, struct timeval *tvp) {
    return setTimer(hloop, fd, tvp, EV_ADD);
}

static int clearTimer(struct snHopLoop *hloop, int fd) {
    return setTimer(hloop, fd, NULL, EV_DELETE);
}

static int poll(struct snHopLoop *hloop, struct timeval *tvp) {
    snApiState *state = hloop->state;
    int kqfd = state->kqfd;
    int retval, numevents = 0;

    if (tvp != NULL) {
        struct timespec timeout;
        timeout.tv_sec = tvp->tv_sec;
        timeout.tv_nsec = tvp->tv_usec * 1000;
        retval = kevent(kqfd, NULL, 0, state->events, SN_SETSIZE, &timeout);
    } else {
        retval = kevent(kqfd, NULL, 0, state->events, SN_SETSIZE, NULL);
    }    

    if (retval > 0) {
        int j;
        
        numevents = retval;
        for(j = 0; j < numevents; j++) {
            int mask = 0;
            struct kevent *e = state->events+j;
            
            if (e->filter == EVFILT_READ) mask |= SN_READABLE;
            if (e->filter == EVFILT_WRITE) mask |= SN_WRITABLE;
            if (e->filter == EVFILT_TIMER) mask |= SN_TIMER;
            if (e->flags & EV_ONESHOT) mask |= SN_ONCE;
            
            hloop->fired[j].fd = e->ident; 
            hloop->fired[j].mask = mask;  
        }
    }
    
    return numevents;
}
