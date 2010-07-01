/* The code below is largely based on ae.h file from redis project.
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

#ifndef __SN_HOPLOOP__
#define __SN_HOPLOOP__

#include <sys/time.h>
#include <lua.h>

#define SN_SETSIZE (1024*10)    /* Max number of fd supported */

#define SN_NONE 0
#define SN_READABLE 1
#define SN_WRITABLE 2
#define SN_TIMER 4

struct snHopLoop;

typedef struct snFiredEvent {
    int fd;
    int mask;
} snFiredEvent;

typedef struct snFileEvent {
    lua_State *L;
    int rcallback; //read callback - fn reference
    int wcallback; //write callback
    int mask;
} snFileEvent;

typedef struct snTimerEvent {
    lua_State *L;
    int callback;
    int mask;
} snTimerEvent;

typedef struct snLoopApi {
    /* methods */
    int (*addEvent)(struct snHopLoop *, int fd, int mask);
    int (*removeEvent)(struct snHopLoop*, int fd, int mask);
    int (*poll)(struct snHopLoop*, struct timeval *tvp);
    
    int (*setTimeout)(struct snHopLoop *hloop, int fd, struct timeval *tvp);
    int (*setInterval)(struct snHopLoop *hloop, int fd, struct timeval *tvp);
    int (*clearTimer)(struct snHopLoop *hloop, int fd);
    
    /* fields */
    const char *name;
} snLoopApi;

typedef struct snHopLoop {
    snLoopApi *api;
    void *state;
    snFileEvent events[SN_SETSIZE]; /* Registered file events */
    snTimerEvent timers[SN_SETSIZE];
    snFiredEvent fired[SN_SETSIZE]; /* Fired events */
    int shouldStop;
} snHopLoop;

static int init(struct snHopLoop *);
static int addEvent(struct snHopLoop *hloop, int fd, int mask);
static int removeEvent(struct snHopLoop *, int fd, int mask);
static int poll(struct snHopLoop *, struct timeval *tvp);
static int setTimeout(struct snHopLoop *hloop, int fd, struct timeval *tvp);
static int setInterval(struct snHopLoop *hloop, int fd, struct timeval *tvp);
static int clearTimer(struct snHopLoop *hloop, int fd);

#endif