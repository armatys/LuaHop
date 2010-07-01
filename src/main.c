/* Parts of the code below are based on ae.c file from redis project.
 *
 * Copyright (c) 2010 Mateusz Armatys
 * Copyright (c) 2006-2010, Salvatore Sanfilippo <antirez at gmail dot com>
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

#include <lua.h>
#include <lauxlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
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

#define SIM 1000000.0 /* 1 second as a number of microseconds */
/* IMPORTANT: time_units and time_scales have to be in sync */
/* The following time_units values are: 
 microseconds, milliseconds, seconds, minutes, hours */
static const char* time_units[] =   {"us",  "ms",   "s",  "m",    "h",       NULL};
static const double time_scales[] = { 1.0,  1000.0, SIM,  SIM*60, SIM*60*60};

static double convert_to_usec(const char *tunit, double val) {
    double usec = 0;
    const char *tu;
    int i = 0;
    
    while ((tu = time_units[i])) {
        if (strncmp(tunit, tu, strlen(tu)) == 0 ) {
            usec = time_scales[i] * val;
            break;
        }
        
        i++;
    }
    
    return usec;
}

/**
 * Converts table containing time data, to microseconds.
 * Table can look like {h=1, m=5, s=60}
 * Available keys are defined in time_units constant.
 **/
static double table_to_usec(lua_State *L, int tidx) {
    int i = 0;
    const char *tunit;
    int usec_total = 0;
    
    while ((tunit = time_units[i])) {
        lua_getfield(L, tidx, time_units[i]);
        if (lua_isnumber(L, -1) == 0) {
            lua_pop(L, 1);
            i++;
            continue;
        }
        double n = lua_tonumber(L, -1);
        lua_pop(L, 1);
        usec_total += convert_to_usec(time_units[i], n);
        
        i++;
    }
    
    return usec_total;
}

static int hop_create(lua_State *L) {
    snHopLoop *src = createLoop(L);
    if (!src) return luaL_error(L, "Could not create snHopLoop.");
    
    snHopLoop *hloop = lua_newuserdata(L, sizeof(snHopLoop));
    if (!hloop) return luaL_error(L, "Could not create snHopLoop.");
    
    memcpy(hloop, src, sizeof(snHopLoop));
    luaL_getmetatable(L, "eu.sharpnose.hoploop");
    lua_setmetatable(L, -2);
    
    int i = 0;
    for (i = 0; i < SN_SETSIZE; i++)
        hloop->events[i].mask = SN_NONE;
    hloop->shouldStop = 0;
    
    free(src);
    
    return 1;
}

static int getMask(lua_State *L, const char *chFilter) {
    if (strncmp(chFilter, "r", 2) == 0) return SN_READABLE;
    else if (strncmp(chFilter, "w", 2) == 0) return SN_WRITABLE;
    else if (strncmp(chFilter, "rw", 3) == 0) return SN_READABLE | SN_WRITABLE;
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
    
    int mask = getMask(L, chFilter);
    if (mask == -1) return luaL_error(L, "Invalid event mask.");
    
    if (hloop->api->addEvent(hloop, fd, mask) == -1) {
        return luaL_error(L, "Could not add event listener.");
    }
    
    int clbref = luaL_ref(L, LUA_ENVIRONINDEX);
    hloop->events[fd].L = L;
    hloop->events[fd].mask |= mask;
    if (mask & SN_READABLE) hloop->events[fd].rcallback = clbref;
    if (mask & SN_WRITABLE) hloop->events[fd].wcallback = clbref;
    
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
    
    hloop->api->removeEvent(hloop, fd, mask);
    if (mask & SN_READABLE) {
        lua_pushnil(L);
        lua_rawseti(L, LUA_ENVIRONINDEX, hloop->events[fd].rcallback);
    }
    if (mask & SN_WRITABLE) {
        lua_pushnil(L);
        lua_rawseti(L, LUA_ENVIRONINDEX, hloop->events[fd].wcallback);
    }
    
    return 0;
}

static int hop_setTimeout(lua_State *L) {
    snHopLoop *hloop = checkLoop(L);
    int fd = luaL_checknumber(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    if (! lua_isfunction(L, 4)) return luaL_error(L, "Function was expexted.");
    
    struct timeval *tv = malloc(sizeof(struct timeval));
    int usec_total = table_to_usec(L, 3);
    
    tv->tv_sec = (long int) (usec_total / SIM);
    tv->tv_usec = (long int) fmod(usec_total, SIM);
    
    int clbref = luaL_ref(L, LUA_ENVIRONINDEX);
    hloop->timers[fd].L = L;
    hloop->timers[fd].callback = clbref;
    
    hloop->api->setTimeout(hloop, fd, tv);
    
    return 0;
}

static int hop_setInterval(lua_State *L) {
    snHopLoop *hloop = checkLoop(L);
    int fd = luaL_checknumber(L, 2);
    luaL_checktype(L, 3, LUA_TTABLE);
    struct timeval *tv = malloc(sizeof(struct timeval));
    int usec_total = table_to_usec(L, 3);
    
    tv->tv_sec = (long int) (usec_total / SIM);
    tv->tv_usec = (long int) fmod(usec_total, SIM);
    
    hloop->api->setInterval(hloop, fd, tv);
    
    return 0;
}

static int hop_clearTimer(lua_State *L) {
    snHopLoop *hloop = checkLoop(L);
    int fd = luaL_checknumber(L, 2);
    
    hloop->api->clearTimer(hloop, fd);
    
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
    lua_pcall(ctx, 2, 0, 0); //TODO check for errors
    
    return 0;
}

static int hop_poll(lua_State *L) {
    snHopLoop *hloop = checkLoop(L);
    struct timeval *tv = NULL;
    double usec_total = 0;
    
    if (lua_istable(L, 2)) {
        usec_total = table_to_usec(L, 2);
        
        if (usec_total > 0) {
            tv = malloc(sizeof(struct timeval));
            tv->tv_sec = (long int) (usec_total / SIM);
            tv->tv_usec = (long int) fmod(usec_total, SIM);
        }
    }
    
    int nevents = hloop->api->poll(hloop, tv);
    if (tv != NULL) free(tv);
    
    int i = 0;
    for (i=0; i<nevents; i++) {
        snFiredEvent fevent = hloop->fired[i];
        int mask = fevent.mask;
        int fd = fevent.fd;
        
        if (mask & SN_TIMER) { /* timer event */
            snTimerEvent timerEvent = hloop->timers[fd];
            lua_State *ctx = timerEvent.L;
            
            if (mask & SN_TIMER) {
                int callback = timerEvent.callback;
                run_callback(L, ctx, callback, fd, mask);
            }
        } else { /* <file event> */
            snEventData *evData = &hloop->events[fd];
            lua_State *ctx = evData->L;
            int rcallback = evData->rcallback;
            int wcallback = evData->wcallback;
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
        } /* </file event> */
    }
    
    return 0;
}

static int hop_stop(lua_State *L) {
    snHopLoop *hloop = checkLoop(L);
    hloop->shouldStop = 1;
    
    return 0;
}

static int hop_loop(lua_State *L) {
    snHopLoop *hloop = checkLoop(L);
    
    while (hloop->shouldStop == 0) {
        hop_poll(L);
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
    {"setTimeout", hop_setTimeout},
    {"setInterval", hop_setInterval},
    {"clearTimer", hop_clearTimer},
    {"poll", hop_poll},
    {"stop", hop_stop},
    {"loop", hop_loop},
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