require "luahop"
require "anet"

local loop = luahop.new()
local response = "HTTP/1.1 200 OK\r\nContent-Length: 12\r\nConnection: close\r\nContent-Type: text/plain\r\n\r\nHello mako\r\n"

local written = {}

local function writeStatus(soc)
	written[soc] = written[soc] or 1
	local n = anet.write(soc, string.sub(response, written[soc]))
	if n > 0 then
		written[soc] = written[soc]+n
	elseif n == 0 then
		--closed connection
		l:removeEvent(soc, "w")
		anet.close(soc)
		written[soc] = 1
	elseif n < 0 then
		--would block
	end
	
	if written[soc]-1 == #response then
		--print("Closing connection")
		written[soc] = 1
		loop:removeEvent(soc, "w")
		anet.close(soc)
	end
end

local function readclient(soc)
	while true do
		local n, msg = anet.read(soc, 4096)
		--print("Read " .. n .. " bytes")
		if n > 0 then
			--io.write(msg)
			if string.match(msg, "\r\n\r\n$") then
				loop:removeEvent(soc, "r")
				loop:addEvent(soc, "w", function() writeStatus(soc) end)
			end
		elseif n < 0 then
			--print(msg)
			break
		elseif n == 0 then
			loop:removeEvent(soc, "r")
			anet.close(soc)
			print("Closed connection: " .. soc)
		end
	end
end

local function handleClient(c, ip, port)
	--print("New client: " .. tostring(ip) .. ":" .. tostring(port))
	anet.nonblock(c)
	loop:addEvent(c, "r", function()
		readclient(c)
	end)
end

local function server()
	local fd, err = anet.tcpserver(8080, "127.0.0.1")
	if err then print(err) end
	
	print("Listening on port 8080..")
	anet.nonblock(fd)
	loop:addEvent(fd, "r", function()
		local cfd, ip, port = anet.accept(fd, true, true)
		handleClient(cfd, ip, port)
	end)
	print("polling..")
	while true do
		loop:poll()
	end
end

local ok, msg = pcall(server)
if not ok then
	print("Server error: " .. tostring(msg))
end
