-- a Lua expect-like library
require 'path'
if not path.loadlib('lexpect','spawnx') then
	print 'cannot find spawnx in same directory as lexpect.lua'
end
local append = table.insert
local find = string.find
local remove = table.remove
local concat = table.concat
local type = type
local ipairs = ipairs
local unpack = unpack
local io = io
local print = print
local spawn = spawn
local path = path

module 'lexpect'

read = spawn.reads
write = spawn.writes
open = spawn.open

lines = {} -- shd be local --4

function split(s,re,keep_delim)
	local i1 = 1
	local ls = {}
	if not re then re = '%s+' end
	while true do
		local i2,i3 = s:find(re,i1)
		if not i2 then
			local last = s:sub(i1) 
			if last ~= '' then append(ls,last) end
			if #ls == 1 and ls[1] == '' then
				return {}
			else
				return ls
			end
		end
		local ie
		if keep_delim then ie = i3 else ie = i2-1 end
		append(ls,s:sub(i1,ie))
		i1 = i3+1
	end
end

function readln()
	if #lines == 0 then
		local chunk = read()
		if not chunk then return nil end
--~ 		print('*',chunk)
		lines = split(chunk,'[\r\n]+',true)
	end
	return remove(lines,1)
end

local function finder(pat)
	if type(pat) == 'table' then
		return function(s,pat,start,plain)
			for _,val in ipairs(pat) do
				if find(s,val,start,plain) then return true end
			end
		end
	else
		return find
	end
end

function expect(pat,lpat)
    local plain = not lpat
	local find = finder(pat)
	repeat
		local line = readln()
	until find(line,pat,1,plain)
end

function ends_with_linefeed (line)
    return line:find('\n$')
end

plain_match = true

function read_upto(pat)
	local find = finder(pat)
	local line,success
	local res = {}
	repeat
		line = readln()
		if not line then return nil end
        -- this somewhat complicated condition is to ensure that we get the last instance of 
        -- the pattern, and only matching on the start of new lines. Sometimes you will
        -- get small chunks coming in, and the idea is only to test chunks that follow
        -- a chunk that ended with a linefeed.
		success = find(line,pat,1,plain_match) and #lines == 0
        if success and #res > 0 and not ends_with_linefeed(res[#res]) then
            success = false
        end
		if success then
			if include_prompt then append(res,line) end
			return res
		else
			append(res,line)
		end
	until success
end

function read_response()
	return read_upto(prompt)
end

function read_response_string()
    return join(read_response())
end

function join(t,delim)
	delim = delim or ''
	return concat(t,delim)
end

function writeln(s)
	write (s..'\n')
end

function command (cmd)
	writeln(cmd)
	return read_upto (prompt)
end

function command_string(cmd)
    return join(command(cmd))
end

function strip_eol(s)
    local i = s:find('[\r\n]+')
    if i then return s:sub(1,i-1) else return s end
end

function join_args(a,istart)
    local argstr = ''
	istart = istart or 1
    for i,v in ipairs(a) do
        if v:find('%s') then
            v = '"'..v..'"'
        end
        if i >= istart then
            argstr = argstr..' '..v
        end
    end	
    return argstr
end

local queue = {}
local events = {}
local command_handler

function check_line (line)
    local cpy = {unpack(events)}
    local ignore
    for i,callback in ipairs(cpy) do
        local discard,please_ignore = callback(line)
        if discard then
           remove(events,i)
         end
        ignore = ignore or please_ignore
    end
    return ignore
end

function set_line_handler(action)
    append(events,action)
end

function queue_command(cmd,action)
    append(queue,cmd)
    if action then set_line_handler(action) end
end

function set_command_handler (handler)
    command_handler = handler
end

function split2 (line)
    local i1,i2 = line:find('%s+')
    if i1 then
        return line:sub(1,i1-1), line:sub(i2+1)
    else
        return line
    end
end

function filter (inf,outf)
	local cmd,res
	-- seems to be necessary on Windows, no harm otherwise.
	io.stdout:setvbuf("no")
	while true do
        local handled
		if not include_prompt then outf:write(prompt) end
		if #queue > 0 then
			cmd = remove(queue,1)
		else
			cmd = inf:read()
		end
        if command_handler then
            handled = command_handler(split2(cmd))
        end
        if not handled then
            res = command(cmd)
            if not res then return end	
            for _,line in ipairs(res) do
                if not check_line(line) then
                    outf:write(line)
                end
            end
        else
            handled = false
        end
	end
end

function printq(s)
    if type(s) == 'table' then s = join(s)
    elseif not s then s = '<nada>' end	
    io.write(s)
end

function echo_to_end ()
    local res = read()
    while res do
        io.write(res)
        res = read()
    end
end
