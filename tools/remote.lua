-- Torii remote control: drive a running emulator over TCP, one command per
-- line, one reply per line ("ok ..." or "err ..."). Load it with
-- `just play-remote` (window) or `just remote` (headless), or
-- `MGBA_SCRIPT=tools/remote.lua`; it listens on 127.0.0.1, port 8888 (or
-- REMOTE_PORT). Try `printf 'frame\n' | nc 127.0.0.1 8888`.
--
--   ping                    ok pong
--   frame                   current frame number
--   press KEYS [FRAMES]     hold KEYS (A+B, START, UP, ...) for FRAMES (default 6)
--   hold KEYS | release     hold keys until released
--   wait FRAMES             reply once FRAMES frames have run; later
--                           commands from the same connection wait too
--   r8|r16|r32 ADDR         read memory (hex)
--   read ADDR LEN           read LEN bytes (hex)
--   w8|w16|w32 ADDR VALUE   write memory
--   reg NAME                read a register (r0..r15, sp, lr, pc, cpsr)
--   screenshot PATH         save the screen as PNG
--   save PATH | load PATH   save or load a state (PNG with the cart's state)
--   reset                   reset the console

local KEYS = { A = 0, B = 1, SELECT = 2, START = 3, RIGHT = 4, LEFT = 5, UP = 6, DOWN = 7, R = 8, L = 9 }

local clients = {}
local releaseAt = nil

local function parseKeys(spec)
	local mask = 0
	for name in string.gmatch(spec or "", "[^+]+") do
		local bit = KEYS[string.upper(name)]
		if not bit then
			return nil, "unknown key " .. name
		end
		mask = mask | (1 << bit)
	end
	return mask
end

local function num(text)
	return text and math.tointeger(tonumber(text))
end

local function hex(value, width)
	return string.format("%0" .. width .. "X", value)
end

local commands = {}

function commands.ping() return "ok pong" end
function commands.frame() return "ok " .. emu:currentFrame() end

function commands.press(keys, frames)
	local mask, err = parseKeys(keys)
	if not mask then return "err " .. err end
	emu:setKeys(mask)
	releaseAt = emu:currentFrame() + (num(frames) or 6)
	return "ok"
end

function commands.hold(keys)
	local mask, err = parseKeys(keys)
	if not mask then return "err " .. err end
	emu:setKeys(mask)
	releaseAt = nil
	return "ok"
end

function commands.release()
	emu:setKeys(0)
	releaseAt = nil
	return "ok"
end

function commands.r8(addr) return "ok " .. hex(emu:read8(num(addr)), 2) end
function commands.r16(addr) return "ok " .. hex(emu:read16(num(addr)), 4) end
function commands.r32(addr) return "ok " .. hex(emu:read32(num(addr)), 8) end

function commands.read(addr, len)
	local data = emu:readRange(num(addr), num(len) or 16)
	local out = {}
	for i = 1, #data do
		out[i] = string.format("%02X", string.byte(data, i))
	end
	return "ok " .. table.concat(out, " ")
end

function commands.w8(addr, value) emu:write8(num(addr), num(value)) return "ok" end
function commands.w16(addr, value) emu:write16(num(addr), num(value)) return "ok" end
function commands.w32(addr, value) emu:write32(num(addr), num(value)) return "ok" end

function commands.reg(name)
	return "ok " .. hex(emu:readRegister(name), 8)
end

function commands.screenshot(path)
	emu:screenshot(path)
	return "ok"
end

function commands.save(path)
	return emu:saveStateFile(path, 1 | 2 | 8) and "ok" or "err could not save " .. tostring(path)
end

function commands.load(path)
	return emu:loadStateFile(path, 2 | 8) and "ok" or "err could not load " .. tostring(path)
end

function commands.reset()
	emu:reset()
	return "ok"
end

local function handle(client, line)
	local words = {}
	for word in string.gmatch(line, "%S+") do
		words[#words + 1] = word
	end
	if #words == 0 then return end
	local name = string.lower(table.remove(words, 1))
	if name == "wait" then
		client.waitUntil = emu:currentFrame() + (num(words[1]) or 1)
		return
	end
	local command = commands[name]
	local ok, reply = pcall(function()
		if not command then return "err unknown command " .. name end
		return command(table.unpack(words))
	end)
	client.sock:send((ok and reply or ("err " .. tostring(reply))) .. "\n")
end

-- Runs the client's buffered lines until one of them is a wait. A client
-- that has finished sending is closed once its last reply is out.
local function process(id, client)
	while not client.waitUntil do
		local line, rest = string.match(client.buffer, "^([^\n]*)\n(.*)$")
		if not line then
			if client.ended then
				clients[id] = nil
				client.sock:close()
			end
			return
		end
		client.buffer = rest
		handle(client, line)
	end
end

local function received(id)
	local client = clients[id]
	while client do
		local data, err = client.sock:receive(1024)
		if not data or data == "" then
			if data == "" or err ~= socket.ERRORS.AGAIN then
				client.ended = true
				process(id, client)
			end
			return
		end
		client.buffer = client.buffer .. data
		process(id, client)
	end
end

callbacks:add("frame", function()
	local now = emu:currentFrame()
	if releaseAt and now >= releaseAt then
		emu:setKeys(0)
		releaseAt = nil
	end
	for id, client in pairs(clients) do
		if client.waitUntil and now >= client.waitUntil then
			client.waitUntil = nil
			client.sock:send("ok " .. now .. "\n")
			process(id, client)
		end
	end
end)

local nextId = 1
local getenv = os and os.getenv or function() return nil end
local port = tonumber(getenv("REMOTE_PORT")) or 8888
-- Inside a container the port is published from 0.0.0.0 (REMOTE_HOST).
local host = getenv("REMOTE_HOST") or "127.0.0.1"
local server, err = socket.bind(host, port)
if not server then
	console:error("remote: cannot bind port " .. port .. ": " .. tostring(err))
	return
end
server:listen()
server:add("received", function()
	local sock = server:accept()
	if not sock then return end
	local id = nextId
	nextId = nextId + 1
	clients[id] = { sock = sock, buffer = "" }
	sock:add("received", function() received(id) end)
	sock:add("error", function() clients[id] = nil end)
end)
console:log("remote: listening on " .. host .. ":" .. port)
