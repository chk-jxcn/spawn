// build@ gcc -shared -I/lang/lua/src spawnx.c -o spawnx.dll /lang/lua/lua51.dll
#include <sys/param.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

//static char buff[2048];

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



#ifdef WIN32
#include <windows.h>
static HANDLE hPipeRead,hWriteSubProcess;

static int spawn_open(lua_State* L)
{
	const char* prog = lua_tostring(L,1);    
	SECURITY_ATTRIBUTES sa = {sizeof(SECURITY_ATTRIBUTES), 0, 0};
	SECURITY_DESCRIPTOR sd;
	STARTUPINFO si = {
		sizeof(STARTUPINFO), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
	};
	HANDLE hRead2,hPipeWrite;
	BOOL running;
	PROCESS_INFORMATION pi;
	HANDLE hProcess = GetCurrentProcess();
	sa.bInheritHandle = TRUE;
	sa.lpSecurityDescriptor = NULL;
	InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION);
	SetSecurityDescriptorDacl(&sd, TRUE, NULL, FALSE);
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);
	sa.lpSecurityDescriptor = &sd;

	// Create pipe for output redirection
	// read handle, write handle, security attributes,  number of bytes reserved for pipe - 0 default
	CreatePipe(&hPipeRead, &hPipeWrite, &sa, 0);

	// Create pipe for input redirection. In this code, you do not
	// redirect the output of the child process, but you need a handle
	// to set the hStdInput field in the STARTUP_INFO struct. For safety,
	// you should not set the handles to an invalid handle.

	hRead2 = NULL;
	// read handle, write handle, security attributes,  number of bytes reserved for pipe - 0 default
	CreatePipe(&hRead2, &hWriteSubProcess, &sa, 0);

	SetHandleInformation(hPipeRead, HANDLE_FLAG_INHERIT, 0);
	SetHandleInformation(hWriteSubProcess, HANDLE_FLAG_INHERIT, 0);

	si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;
	si.hStdInput = hRead2;
	si.hStdOutput = hPipeWrite;
	si.hStdError = hPipeWrite;

	running = CreateProcess(
			NULL,
			(char*)prog,
			NULL, NULL,
			TRUE, CREATE_NEW_PROCESS_GROUP,
			NULL,
			NULL, // start directory
			&si, &pi);

	CloseHandle(pi.hThread);
	CloseHandle(hRead2);
	CloseHandle(hPipeWrite);

	if (running) {
		lua_pushnumber(L,(int)hPipeRead);  
	} else {
		lua_pushnil(L);
	}
	return 1;
}

static int spawn_reads(lua_State* L)
{
	DWORD bytesRead;
	int res = ReadFile(hPipeRead,buff,sizeof(buff), &bytesRead, NULL);
	buff[bytesRead] = '\0';
	if (res == 0) {
		lua_pushnil(L);
	} else {
		lua_pushstring(L,buff);
	}
	return 1;
}

static int spawn_writes(lua_State* L)
{   
	DWORD bytesWrote;
	const char* s = lua_tostring(L,1);
	WriteFile(hWriteSubProcess,s,strlen(s),&bytesWrote, NULL);
	return 0;
}
#else
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

//static int spawn_fd;

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

static int spawn_open(lua_State* L)
{
	const char *args[30];
	int pid, i = 0;
	int buffsize = 2048;
	struct termios tm;    
	char* argline = strdup(lua_tostring(L,1));
	char* arg = quote_strtok(argline,'"');
	if (arg == NULL) return 0;
	while (arg != NULL) {
		args[i++] = arg;
		//fprintf(stderr,"%d %s\n",i,arg);
		arg = quote_strtok(NULL,'"');
	}
	args[i] = NULL;    
	memset(&tm,0,sizeof(tm));
	cfmakeraw(&tm);    
	errno = 0;
	if (lua_gettop(L) >= 2) buffsize = lua_tointeger(L, 2);
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
}


static int spawn_reads(lua_State* L)
{
	procdesc *pp = toprocp(L);
	int len = pp->buffsize;
	int sz = 0;
	int isnonblock = pp->isnonblock;
	int tempsz = 0;
	int us = pp->delay;
	if (lua_gettop(L) >= 2) len = lua_tointeger(L, 2);
	if (len > pp->buffsize || len <= 0 ) {
		lua_pushnil(L);
		lua_pushvfstring(L,"len should less than %d", len);
		return 2;
	}
	while((tempsz = read(pp->fd, pp->buff + sz, len - sz)) >= 0) {
		if(us) usleep(us);
		sz += tempsz;
		if (!isnonblock || sz == len) break;
	}
	if (sz == 0 && errno != 0) {
		lua_pushnil(L);
		lua_pushstring(L,strerror(errno));
		return 2;
	} else {
		lua_pushlstring(L,pp->buff, sz);
		return 1;
	}
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
	return 0;
}

static int spawn_writes(lua_State* L)
{   
	procdesc *pp = toprocp(L);
	const char* s = lua_tostring(L,2);
	write(pp->fd,s,strlen(s));
	return 0;
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
#endif

static int spawn_version(lua_State* L)
{
	lua_pushstring(L, SPAWN_VERSION);
	return 1;
}

static const struct luaL_reg spawn[] = {
	{"open",spawn_open},
	//{"reads",spawn_reads},
	// {"writes",spawn_writes},
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
	createmeta(L);
	lua_replace(L, LUA_ENVIRONINDEX);
	/* open library */
	luaL_register(L, "spawn", spawn);
	return 1;
}
