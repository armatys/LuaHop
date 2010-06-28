#include <stdlib.h>
#include <sys/types.h>
#include <sys/event.h>
#include <sys/time.h>
#include "hoploop.h"

static int init(struct snHopLoop *);
static int addEvent(struct snHopLoop *hloop, int fd, int mask);
static int removeEvent(struct snHopLoop *, int fd, int mask);
static int poll(struct snHopLoop *, struct timeval *tvp);

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
	api->addEvent = addEvent;
	api->removeEvent = removeEvent;
	api->poll = poll;
	
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
            
            hloop->fired[j].fd = e->ident; 
            hloop->fired[j].mask = mask;  
        }
    }
    
    return numevents;
}
