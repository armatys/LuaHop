#ifndef __SN_HOPLOOP__
#define __SN_HOPLOOP__

#include <sys/time.h>
#include <lua.h>

#define SN_SETSIZE (1024*10)    /* Max number of fd supported */

#define SN_NONE 0
#define SN_READABLE 1
#define SN_WRITABLE 2

struct snHopLoop;

typedef struct snFiredEvent {
    int fd;
    int mask;
} snFiredEvent;

typedef struct snEventData {
	lua_State *L;
	int rcallback; //read callback - fn reference
	int wcallback; //write callback
	int mask;
} snEventData;

typedef struct snLoopApi {
	/* methods */
	int (*addEvent)(struct snHopLoop *, int fd, int mask);
	int (*removeEvent)(struct snHopLoop*, int fd, int mask);
	int (*poll)(struct snHopLoop*, struct timeval *tvp);
	
	/* fields */
	const char *name;
} snLoopApi;

typedef struct snHopLoop {
	snLoopApi *api;
	void *state;
	snEventData events[SN_SETSIZE]; /* Registered events */
	snFiredEvent fired[SN_SETSIZE]; /* Fired events */
} snHopLoop;

#endif