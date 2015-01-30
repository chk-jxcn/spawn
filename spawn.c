// build@ gcc -Wall -shared -fPIC -I/usr/local/include/lua51 spawnx.c -o spawnx.so -llua-5.1 -lutil
#include <sys/param.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include <unistd.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>

#if defined(__FreeBSD_version)
#include <sys/ioctl.h>
#include <termios.h>
#include <libutil.h>
#else
#include <pty.h>
#endif


#define LUA_PROCHANDLE "PROCDESC*"
#define SPAWN_VERSION "0.1"

typedef struct procdesc {
	int fd;
	int isnonblock;
	int delay;
	int pid;
	int buffsize;
	char buff[];
} procdesc;

static int defbuffsize = 2048;
static struct termios tm = {0}; /* inital as raw in openlib*/
static char termmode[10] = "raw";

int isspace(int c);

static char *quote_strtok(char *str, char str_delim)
{
	// a specialized version of strtok() which treats quoted strings specially 
	// (used for handling command-line parms)
	static char *tok;
	if(str != NULL) tok = str;

	while (*tok && isspace(*tok)) tok++;
	if (*tok == '\0') return NULL;

	if (*tok == str_delim) {       
		tok++;            // skip "
		str = tok;
		while (*tok && *tok != str_delim) tok++;        
	} else {
		str = tok;
		while (*tok && ! isspace(*tok)) tok++;
	}
	if (*tok) *tok++ = '\0';  
	return str;
}

static procdesc *toprocp(lua_State *L) {
	procdesc *pp = ((procdesc *)luaL_checkudata(L, 1, LUA_PROCHANDLE));
	if (pp->pid == 0)
		luaL_error(L, "attempt to operate a closed process");
	return pp;
}

static int spawn_setbuffsize(lua_State* L)
{
	int buffsize = defbuffsize;
	if(lua_gettop(L) >= 1) buffsize = lua_tointeger(L, 1);
	lua_pushinteger(L, buffsize>0? defbuffsize = buffsize: defbuffsize);
	return 1;
}

static int spawn_setterm(lua_State* L)
{
	const char *t = NULL;
	if(lua_gettop(L) == 0) {
		lua_pushstring(L, termmode);
		return 1;
	}
	t = luaL_checkstring(L, 1);
	if(strcmp(t, "raw") == 0)
		cfmakeraw(&tm);    
	else if(strcmp(t, "sane") == 0)
		cfmakesane(&tm);
	else if(strcmp(t, "keep") == 0) {
		if (tcgetattr(STDIN_FILENO, &tm) < 0) {
			t = termmode;
		} 
	}
	else {
		lua_pushnil(L);
		lua_pushfstring(L, "%s is not a vaild Term mode", t);
		return 2;
	}
	strncpy(termmode, t, sizeof(termmode));
	lua_pushstring(L, termmode);
	return 1;
}

static int spawn_open(lua_State* L)
{
	const char *args[30];
	int buffsize = defbuffsize;
	char* argline = strdup(luaL_checkstring(L,1));
	char* arg = quote_strtok(argline,'"');

	/* args */	
	if (arg == NULL) return 0;
	int i = 0;
	while (arg != NULL) {
		args[i++] = arg;
		//fprintf(stderr,"%d %s\n",i,arg);
		arg = quote_strtok(NULL,'"');
	}
	args[i] = NULL;    

	errno = 0;
	procdesc *pp = (procdesc *)lua_newuserdata(L, sizeof(procdesc) + buffsize);
	memset(pp, 0, sizeof(procdesc));  /* file handle is currently `closed' */
	pp->buffsize = buffsize;
	pp->pid = forkpty(&pp->fd,NULL,&tm,NULL);
	pp->delay = 0;

	if (pp->pid == 0) { // child
		execvp(args[0],(char * const*)args);
		// if we get here, it's an error!
		perror("'unable to spawn process");        
	} else {
		free(argline);
		luaL_getmetatable(L, LUA_PROCHANDLE);
		lua_setmetatable(L, -2);
		lua_pushstring(L,strerror(errno));
		return 2;
	}
	/* should not run to here*/
	perror("'unable to spawn process");        
	return 0;
}

static int spawn_gc(lua_State* L);

static int spawn_reads(lua_State* L)
{
	procdesc *pp = toprocp(L);
	int len = pp->buffsize;
	int sz = 0;
	int isnonblock = pp->isnonblock;
	int tempsz = 0;
	int us = pp->delay;
	int sleept = 0;
	if (lua_gettop(L) >= 2) len = lua_tointeger(L, 2);
	if (len > pp->buffsize || len <= 0 ) {
		lua_pushnil(L);
		lua_pushfstring(L, "len should less than %d", len);
		return 2;
	}
	while((tempsz = read(pp->fd, pp->buff + sz, len - sz)) >= 0) {
		sz += tempsz;
		if ((!isnonblock || sz == len) || (tempsz == 0 && errno != EAGAIN)) break;
		if(us) {
			usleep(us);
			sleept ++;
		}
	}
	if (sz == 0 && (errno != 0)) {
		if(errno != EAGAIN) spawn_gc(L);	
		lua_pushnil(L);
		lua_pushstring(L, strerror(errno));
		return 2;
	} else {
		lua_pushlstring(L, pp->buff, sz);
		lua_pushinteger(L, sleept * us);
		return 2;
	}
}

static int spawn_isdead(lua_State* L)
{
	procdesc *pp = ((procdesc *)luaL_checkudata(L, 1, LUA_PROCHANDLE));
	if(pp->pid == 0) 
		lua_pushboolean(L, 1);
	else
		lua_pushboolean(L, 0);
	return 1;
}

static int spawn_setnonblock(lua_State* L)
{   
	procdesc *pp = toprocp(L);
	int f = lua_toboolean(L,2);
	int flags = fcntl(pp->fd,F_GETFL,NULL);
	int error = 0;
	if(f == 1) {
		flags = flags | O_NONBLOCK;
		pp->isnonblock = 1;
	}
	else
		flags = flags & ~O_NONBLOCK;
	error = fcntl(pp->fd,F_SETFL,flags);
	if (error == -1) {
		lua_pushnil(L);
		lua_pushstring(L,strerror(errno));
		return 2;
	}
	lua_pushinteger(L, flags);
	return 1;
}

static int spawn_setdelay(lua_State* L)
{
	procdesc *pp = toprocp(L);
	pp->delay = lua_tointeger(L, 2);
	return 0;
}

static int spawn_closepty(lua_State* L)
{
	procdesc *pp = toprocp(L);
	close(pp->fd);
	pp->fd = -1;
	return 0;
}

static int spawn_wait(lua_State* L)
{
	int nonblock = 0;
	int error = 0;
	if(lua_gettop(L) >= 2)  nonblock = lua_toboolean(L, 2);
	procdesc *pp = toprocp(L);
	if (nonblock == 1)
		error = waitpid(pp->pid, NULL,  WEXITED|WNOHANG);
	else
		error = waitpid(pp->pid, NULL,  WEXITED);
	if (error == -1) {
		memset(pp, 0, sizeof(procdesc));
		lua_pushnil(L);
		lua_pushstring(L,strerror(errno));
		return 2;
	}
	if (error > 0) memset(pp, 0, sizeof(procdesc));
	lua_pushinteger(L, error);
	return 1;
}

static int spawn_kill(lua_State* L)
{
	int sig = SIGINT;
	int error = 0;
	if(lua_gettop(L) >= 2) sig = lua_tointeger(L, 2);
	procdesc *pp = toprocp(L);
	error = kill(pp->pid, sig);
	if (error != 0) {
		lua_pushnil(L);
		lua_pushstring(L,strerror(errno));
		return 2;
	}
	lua_pushboolean(L, 1);
	return 1;
}


static int spawn_gc(lua_State* L)
{
	procdesc *pp = ((procdesc *)luaL_checkudata(L, 1, LUA_PROCHANDLE));
	int i = 0;
	if(pp->pid > 0) kill(pp->pid, 9);
	if(pp->fd >=0 ) spawn_closepty(L);
	for(;i<5;i++) waitpid(-1, (int *) 0, WNOHANG);
	memset(pp, 0, sizeof(procdesc));
	return 0;
}

static int spawn_writes(lua_State* L)
{   
	procdesc *pp = toprocp(L);
	size_t size = 0;
	const char* s = luaL_checklstring(L, 2, &size);
	ssize_t ret = write(pp->fd, s, size);
	lua_pushinteger(L, ret);
	return 1;
}

static int spawn_tostring(lua_State* L)
{
	procdesc *pp = ((procdesc *)luaL_checkudata(L, 1, LUA_PROCHANDLE));
	if(pp->pid == 0)
		lua_pushliteral(L, "proc (closed)");
	else
		lua_pushfstring(L, "proc (%d)", pp->pid);
	return 1;
}

static int spawn_sleep(lua_State* L)
{
	/* sleep"1s" or 1ms or 1us */
	int scale = 1;
	size_t size = 0;
	const char *s = NULL;
	if(lua_gettop(L) != 1)
		luaL_error(L, "error: only 1 args accept");
	(void)luaL_checkstring(L, 1);
	lua_getglobal(L, "string");
	lua_getfield(L, -1, "match");
	lua_pushvalue(L, 1);
	/* if capture mm, um or m, rasie a error */
	lua_pushstring(L, "^(%d*%.?%d*)([um]?[s]?)$"); 
	lua_call(L, 2, 2);
	if(lua_isnil(L, -1))
		luaL_error(L, "error: not a vaild arg");
	s = luaL_checklstring(L, -1, &size);
	if(size == 0) 
		scale = 1000000;
	else if(size == 1)
		if(*s != 's')	
			luaL_error(L, "error: not a vaild arg");
		else
			scale = 1000000;
	else
		switch (*s) {
			case 'm':
				scale = 1000;
				break;
			case 'u':
				scale = 1;
				break;
		}
	lua_Number t = lua_tonumber(L, -2);
	usleep((int)(t * scale));
	return 0;
}

static int spawn_version(lua_State* L)
{
	lua_pushstring(L, SPAWN_VERSION);
	return 1;
}

static const struct luaL_reg spawn[] = {
	{"open",spawn_open},
	{"setbuffsize",spawn_setbuffsize},
	{"setterm",spawn_setterm},
	{"sleep",spawn_sleep},
	{"version",spawn_version},
	{NULL,NULL}
};

static const struct luaL_reg proclib[] = {
	{"kill",spawn_kill},
	{"setdelay",spawn_setdelay},
	{"closepty",spawn_closepty},
	{"wait",spawn_wait},
	{"reads",spawn_reads},
	{"writes",spawn_writes},
	{"isdead",spawn_isdead},
	{"setnonblock",spawn_setnonblock},
	{"__gc", spawn_gc},
	{"__tostring", spawn_tostring},
	{NULL,NULL}
};

static void createmeta (lua_State *L) {
	luaL_newmetatable(L, LUA_PROCHANDLE);  /* create metatable for file handles */
	lua_pushvalue(L, -1);  /* push metatable */
	lua_setfield(L, -2, "__index");  /* metatable.__index = metatable */
	luaL_register(L, NULL, proclib);  /* file methods */
}

int luaopen_spawn(lua_State *L)
{
	cfmakeraw(&tm);    
	createmeta(L);
	lua_replace(L, LUA_ENVIRONINDEX);
	/* open library */
	luaL_register(L, "spawn", spawn);
	return 1;
}
