LuaHop - Beautiful Lua event loop.

### Compilation

Use [premake](http://industriousone.com/premake) to generate appropriate build files. E.g run `premake4 gmake` to generate a Makefile. Then execute `make config=release32` or `make config=release64` to compile.

### Usage:

To use LuaHop, you will need also a socket library. Take a look at [LuaAnet](http://github.com/mako52/LuaAnet). Probably, you could also use LuaSocket, but I haven't tested it yet.

	require "anet"
	require "luahop"
	
	local loop = luahop.new()
	local fd, err = anet.tcpserver(8080, "127.0.0.1")
	anet.nonblock(fd)
	
	local function readclient(soc)
		while true do
			local n, msg = anet.read(soc, 4096)
			print("Read " .. n .. " bytes")
			
			if n > 0 then
				io.write(msg)
			elseif n < 0 then
				-- print error message
				print(msg)
				break
			elseif n == 0 then
				-- closed connection
				loop:removeEvent(soc, "r")
				anet.close(soc)
			end
		end
	end
	
	local function handleclient(c, ip, port)
		print("New client: " .. tostring(ip) .. ":" .. tostring(port))
		anet.nonblock(c)
		loop:addEvent(c, "r", function()
			readclient(c)
		end)
	end
	
	-- adding "read" (accepting connection) callback
	loop:addEvent(fd, "r", function()
		local cfd, ip, port = anet.accept(fd, true, true)
		handleclient(cfd, ip, port)
	end)
	
	while true do
		loop:poll()
	end

