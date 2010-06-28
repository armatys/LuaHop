#include <stdlib.h>
#include <sys/epoll.h>
#include "hoploop.h"

static int init(struct snHopLoop *);
static int addEvent(struct snHopLoop *hloop, int fd, int mask);
static int removeEvent(struct snHopLoop *, int fd, int mask);
static int poll(struct snHopLoop *, struct timeval *tvp);

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
	api->addEvent = addEvent;
	api->removeEvent = removeEvent;
	api->poll = poll;
	
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

static int removeEvent(struct snHopLoop *hloop, int fd, int mask) {
	aeApiState *state = hloop->state;
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

static int poll(struct snHopLoop *hloop, struct timeval *tvp) {
	aeApiState *state = hloop->state;
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
            hloop->fired[j].fd = e->data.fd;
            hloop->fired[j].mask = mask;
        }
    }
    
    return numevents;
}
