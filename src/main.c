#include <lua.h>
#include <lauxlib.h>
#include <string.h>
#include <stdio.h>
#include "config.h"
#include "hoploop.h"

static snHopLoop *createLoop(); //backend-specific; defined in files below

#ifdef HAVE_EPOLL
	#include "epoll_hop.c"
#else
    #ifdef HAVE_KQUEUE
    	#include "kqueue_hop.c"
    #endif
#endif

#define checkLoop(L) (snHopLoop *)luaL_checkudata(L, 1, "eu.sharpnose.hoploop")

static int hop_create(lua_State *L) {
	snHopLoop *src = createLoop(L);
	if (!src) return luaL_error(L, "Could not create snHopLoop.");
	
	snHopLoop *hloop = lua_newuserdata(L, sizeof(snHopLoop));
	memcpy(hloop, src, sizeof(snHopLoop));
	luaL_getmetatable(L, "eu.sharpnose.hoploop");
	lua_setmetatable(L, -2);
	
	int i = 0;
	for (i = 0; i < SN_SETSIZE; i++)
        hloop->events[i].mask = SN_NONE;
	
	free(src);
	
	return 1;
}

static int getMask(lua_State *L, const char *chFilter) {
	if (strncmp(chFilter, "r", 2) == 0) return SN_READABLE;
	else if (strncmp(chFilter, "w", 2) == 0) return SN_WRITABLE;
	else if (strncmp(chFilter, "rw", 2) == 0) return SN_READABLE | SN_WRITABLE;
	else return -1;
}

static const char *getChMask(int mask) {
	if (mask & SN_READABLE & SN_WRITABLE) return "rw";
	else if (mask & SN_READABLE) return "r";
	else if (mask & SN_WRITABLE) return "w";
	else return "";
}

static int hop_addEvent(lua_State *L) {
	snHopLoop *hloop = checkLoop(L);
	int fd = luaL_checknumber(L, 2);
	const char *chFilter = luaL_checkstring(L, 3);
	if (! lua_isfunction(L, 4)) return luaL_error(L, "Function was expexted.");
	int clbref = luaL_ref(L, LUA_ENVIRONINDEX);
	
	int mask = getMask(L, chFilter);
	if (mask == -1) return luaL_error(L, "Invalid event mask.");
	
	hloop->events[fd].L = L;
	hloop->events[fd].mask |= mask;
	if (mask & SN_READABLE) hloop->events[fd].rcallback = clbref;
	if (mask & SN_WRITABLE) hloop->events[fd].wcallback = clbref;
	
	hloop->api->addEvent(hloop, fd, mask);
	
	return 0;
}

static int hop_removeEvent(lua_State *L) {
	snHopLoop *hloop = checkLoop(L);
	int fd = luaL_checknumber(L, 2);
	const char *chFilter = luaL_checkstring(L, 3);
	
	int mask = getMask(L, chFilter);
	if (mask == -1) return luaL_error(L, "Invalid event mask.");
	
	if (fd >= SN_SETSIZE) return 0;
	if (hloop->events[fd].mask == SN_NONE) return 0;
	hloop->events[fd].mask = hloop->events[fd].mask & (~mask);
	
	int rcallback = hloop->events[fd].rcallback;
	int wcallback = hloop->events[fd].wcallback;
	
	hloop->api->removeEvent(hloop, fd, mask);
	lua_pushnil(L);
	lua_rawseti(L, LUA_ENVIRONINDEX, rcallback);
	lua_pushnil(L);
	lua_rawseti(L, LUA_ENVIRONINDEX, wcallback);
	
	return 0;
}

static int run_callback(lua_State *L, lua_State *ctx, int clbref, int fd, int mask) {
	lua_rawgeti(L, LUA_ENVIRONINDEX, clbref);
	if (!lua_isfunction(L, -1)) return luaL_error(L, "Function was expected");
	
	if (ctx != L) {
		//copy callback function to ctx Lua state
		lua_xmove(L, ctx, 1);
	}
	
	//call user function (callback)
	lua_pushnumber(ctx, fd);
	lua_pushstring(ctx, getChMask(mask));
	lua_pcall(ctx, 2, 0, 0);
	
	return 0;
}

static int hop_poll(lua_State *L) {
	snHopLoop *hloop = checkLoop(L);
	int nevents = hloop->api->poll(hloop, NULL);
	
	int i = 0;
	for (i=0; i<nevents; i++) {
		snEventData *evData = &hloop->events[hloop->fired[i].fd];
		lua_State *ctx = evData->L;
		int rcallback = evData->rcallback;
		int wcallback = evData->wcallback;
		int mask = hloop->fired[i].mask;
		int fd = hloop->fired[i].fd;
		int rfired = 0;
		
		if (evData->mask & mask & SN_READABLE) {
			rfired = 1;
			run_callback(L, ctx, rcallback, fd, mask);
		}
		if (evData->mask & mask & SN_WRITABLE) {
			if (!rfired || evData->wcallback != evData->rcallback) {
				run_callback(L, ctx, wcallback, fd, mask);
			}
		}
	}
	
	return 0;
}

static int hop_repr(lua_State *L) {
	snHopLoop *hloop = checkLoop(L);
	lua_pushfstring(L, "<Hop Loop: %s>", hloop->api->name);
	
	return 1;
}

static int hop_gc(lua_State *L) {
	//some additional cleanup, like closing hloop->state or unregistering event listeners?
	snHopLoop *hloop = checkLoop(L);
	free(hloop->api);
	free(hloop->state);
	
	return 0;
}

static const struct luaL_Reg hoplib_m [] = {
	{"addEvent", hop_addEvent},
	{"removeEvent", hop_removeEvent},
	{"poll", hop_poll},
	{"__tostring", hop_repr},
	{"__gc", hop_gc},
	{NULL, NULL}
};

static const struct luaL_Reg hoplib [] = {
	{"new", hop_create},
	{NULL, NULL}
};

LUALIB_API int luaopen_luahop(lua_State *L) {
	lua_newtable(L);
	lua_replace(L, LUA_ENVIRONINDEX);
	
	luaL_newmetatable(L, "eu.sharpnose.hoploop");
	lua_pushvalue(L, -1);
	lua_setfield(L, -2, "__index");
	luaL_register(L, NULL, hoplib_m);
	
	luaL_register(L, "luahop", hoplib);
	
	return 1;
}