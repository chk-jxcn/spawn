# spawn

A expect like lua module fork from lua-gdb package.

*You can download original lua-gdb package here [http://mysite.mweb.co.za/residents/sdonovan/lua/lua-gdb.zip]*


## Build and install
Please compile and install it by copy and paste:
```
$gcc -Wall -shared -fPIC -I/usr/local/include/lua51 spawn.c -o spawn.so -llua-5.1 -lutil
$cp spawn.so /usr/local/lib/lua/5.1/
```

**NOTE:**

* **lexecpt.lua is not working fine with whis version of spawn, just see see:)**
* **This version has been tested only on FreeBSD 10.0 and centos 6.3 up to now.**

## Functions

***

### *spawn.setbuffsize([buffsize])*
Sets default buffer size of IO, always returns the default buffer size.

***

### *spawn.setterm([mode])*
Sets Term mode of process, returns current Term mode or nil if mode is invaild.

Mode is alternative of below:
* "raw"

       Raw INPUT/OUTPUT, this mode useful in lua script daemon.
* "sane"

       See stty(1), may need setting windows size, **NOT TEST** in lua script daemon yet, this mode useful in fork process like bash.
* "keep"

       Keep Term the same with INPUT Term, this mode make auto input and print in current window easy.

***

### *spawn.open(process)*
This function forks a *process*. It returns a new process handle.
Process is default opened with **block mode** and **2048** of *buffersize*.

***

### *spawn.sleep(time)*
Sleep, time can be 1(s) "1"(s) "1s"(s) "1ms"(ms) "1us"(us).

***

### *proc:setnonblock(mode)*
Sets pty FD to nonblock if mode is true. Returns current flags or nil if failed.

***

### *proc:setdelay(us)*
Sets nonblock mode read interval, in unit of microsecond. 

***

### *proc:reads([size])*
Try reads *size* bytes from pty. If in nonblock mode, loop reads in interval of *us*. Returns string of at most min(*buffsize*, *size*) length or nil if any error occurs.

**NOTE: If a not EAGAIN error happened, preform a __gc calling, this may only occur when pty was closed.**

Return vaules:
```
.----------------------------------.
| Condition     | Block | NonBlock |
|---------------|-------|----------|
| No data ready | -     | nil      |
| EOF           | ""    | ""       |
| IO error      | nil   | nil      |
`----------------------------------'
```

***

### *proc:writes(string)*
Writes *string* to pty. Returns write syscall return value.

***

### *proc:kill([sig])*
Sends a *sig* to process or sends SIGINT if *sig* is not presented.

***

### *proc:wait([mode])*
Waits for a process to exit, nonblock if mode is true. Returns pid or 0 if failed. 

**NOTE: This function also clear userdata. Marks process closed**

***

### *proc:closepty()*
Closes FD of pty, process may exits after FD was closed. 

**NOTE: You may call proc:wait to collect process exit status, but __gc can also do it.**

***

### *proc:isdead()*
Checks process is dead or not. Returns true if dead and vice versa.

***

### *proc:__gc()*
Perform follow actions:
* Sends SIGKILL to process.
* Closes pty.
* Waits exit status of any child process in nonblock mode, repeat 5 times.

***

### *proc:version()*
Prints version.

***

## Example
```lua
>> package.cpath="./?.so"
>> require"spawn"
{
    open = function, setbuffsize = function, setterm = function,
    version = function
}
>> spawn.setterm"keep"
"keep"
>> spawn.setbuffsize(50)
50
>> x=spawn.open"bash"
>> x:setnonblock(1)
6
>> x:reads()
nil "Resource temporarily unavailable"
>> x:writes"yes\n"
4
>> x:reads()
"yes\r\n"
>> x:reads()
"[chk@NUC ~/spawn]$ yes\r\n"
>> x:reads()
"y\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r"
>> x:reads()
"\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny"
>> x:reads()
"\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\ny\r\n"
>> x:writes"^C"
1
>> x:reads()
"^C\r\n"
>> x:reads()
"\r\n"
>> x:reads()
"[chk@NUC ~/spawn]$ "
>> x:reads()
nil "Resource temporarily unavailable"
>> x:closepty()
>> x:wait(1)
29674
>> x
proc (closed)
```

### License
None

### Contact
chk.jxcn#gmail.com
