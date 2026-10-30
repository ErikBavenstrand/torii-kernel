-- Save states across scenes, for tests/state_trips.py: run the kernel with
-- `mgba-shot -L tests/state_trip.lua`, launch a game with BOOT WITH ADDON
-- (SAVESTATE on), and this script plays it from its start through the steps
-- in TRIP_STEPS, words apart:
--
--   play:N            N frames of seeded random keys (A, B, START and the
--                     D-pad, one at a time; never L or R, so never the menu
--                     key, and never a reset combination)
--   save:SLOT:NAME    the menu's Save state into SLOT; then, from the frame
--                     the game goes on, TRIP_K frames of the keys NAME seeds
--                     (its replay), recording each frame's memory digest
--   load:NAME[:fade]  the menu's Load state; then NAME's replay, each frame's
--                     digest compared with the recorded one. fade: first
--                     wait (up to 600 frames) for a fade or a transition:
--                     BLDCNT's brightness effect on, or 64 or more palette
--                     colours changed in a frame
--   reset             the menu's Reset game (the game from its start)
--   torii             the menu's Back to Torii; the run goes on in the kernel
--   autoload:NAME     the game was launched into a state (a slot from the
--                     game popup's Load state): wait for the stub's load at
--                     the game's start, then NAME's replay, compared with the
--                     digests TRIP_IN holds
--
-- A digest covers EWRAM and IWRAM in 1 KB blocks, but for the hook's own
-- words (hook_watch.lua's skips: the spend area, the IRQ stack under the
-- hook, the BIOS's words at 0x3007FE0). The frame the game goes on is the
-- first frame end after the menu at which the CPU is neither in the stub
-- (the game's patch area) nor in the overlay: a SAVE and a LOAD both give
-- the game back at a frame's start. Lines it logs (stderr, as "script: ..."):
--
--   trip game ENTRY                the game started
--   trip saved NAME FRAME INFO     the game went on after Save state at
--                                  FRAME (the first frame end after it);
--                                  INFO: the display mode, effects, sound
--   trip loaded NAME FRAME INFO    the same after Load state
--   trip same NAME K               the replay's K digests all matched
--   trip differs NAME +J WHERE     the first frame of the replay whose
--                                  digest differs, and its first block
--   trip fail WHAT                 a step couldn't run (the menu didn't
--                                  open, the stub never gave the game back)
--   trip done                      every step ran (stop with -u 'trip done')
--
-- TRIP_SEED seeds the keys (default 1), TRIP_K is the replay's frames
-- (default 120), TRIP_OUT a file the records are written to at the end,
-- TRIP_IN one read at the start (records of an earlier run, for autoload),
-- TRIP_BUFSLOT the state slot already in cart SRAM's buffer (0), which
-- the Save state page opens on.

local K = tonumber(os.getenv("TRIP_K") or "120")
local SEED = tonumber(os.getenv("TRIP_SEED") or "1")
local STEPS = os.getenv("TRIP_STEPS") or ""
local OUT = os.getenv("TRIP_OUT")
local IN = os.getenv("TRIP_IN")
local bufslot = tonumber(os.getenv("TRIP_BUFSLOT") or "0")
-- TRIP_DEBUG: keep the memory too, and name the first bytes that differ
local DEBUG = os.getenv("TRIP_DEBUG")

local KEYS = { A = 0x001, B = 0x002, SELECT = 0x004, START = 0x008, RIGHT = 0x010, LEFT = 0x020, UP = 0x040,
	DOWN = 0x080, R = 0x100, L = 0x200 }
local MENU_KEY = KEYS.L | KEYS.R | KEYS.START
local OVERLAY = 0x02000000
local OVERLAY_MAGIC = 0x31564F54
local PATCH_SIZE = 0x2000

local kernelHeader, patchStart, gameStarted = nil, nil, false
local records = {}
local co

-- mGBA's functions refuse calls from a coroutine (another lua_State): the
-- steps' coroutine asks the frame callback to make them (E, C)
local function proxy(object)
	return setmetatable({}, {
		__index = function(_, name)
			return function(_, ...)
				local _, main = coroutine.running()
				if main then
					return object[name](object, ...)
				end
				return coroutine.yield("call", object, name, ...)
			end
		end,
	})
end
local E, C = proxy(emu), proxy(console)

local function log(text)
	C:log("trip " .. text)
end

-- --- Keys --------------------------------------------------------------------

-- xorshift32
local function rng(seed)
	local s = seed & 0xFFFFFFFF
	if s == 0 then
		s = 0x9E3779B9
	end
	return function(n)
		s = s ~ ((s << 13) & 0xFFFFFFFF)
		s = s ~ (s >> 17)
		s = s ~ ((s << 5) & 0xFFFFFFFF)
		return s % n
	end
end

local function seed_of(text)
	local h = 2166136261
	for i = 1, #text do
		h = ((h ~ text:byte(i)) * 16777619) & 0xFFFFFFFF
	end
	return h ~ (SEED * 0x9E3779B1 & 0xFFFFFFFF)
end

-- n frames of keys, one held at a time: A most, the D-pad held longer
local POOL = { "A", "A", "A", "B", "START", "UP", "UP", "DOWN", "DOWN", "LEFT", "LEFT", "RIGHT", "RIGHT" }
local function key_plan(seed, n)
	local r = rng(seed)
	local plan, i = {}, 1
	while i <= n do
		local name = POOL[r(#POOL) + 1]
		local hold = name:match("^[UDLR]") and 4 + r(40) or 3 + r(8)
		for _ = 1, hold do
			if i > n then
				break
			end
			plan[i] = KEYS[name]
			i = i + 1
		end
		for _ = 1, 2 + r(12) do
			if i > n then
				break
			end
			plan[i] = 0
			i = i + 1
		end
	end
	return plan
end

-- --- Frames ------------------------------------------------------------------

local function tick()
	coroutine.yield()
end

local function frames(n)
	for _ = 1, n do
		tick()
	end
end

local function overlay_up()
	return E:read32(OVERLAY) == OVERLAY_MAGIC
end

local function in_stub()
	local pc = E:readRegister("pc")
	return overlay_up() or (patchStart ~= nil and pc >= patchStart and pc < patchStart + PATCH_SIZE)
end

-- --- Digests -----------------------------------------------------------------

local BLOCK = 0x400
local UNPACK = "<" .. string.rep("j", 16)

local function skips()
	local list = { { 0x03007FE0, 0x03008000 } }
	local spend = patchStart and E:read32(patchStart + 0x18) or 0
	if spend ~= 0 then
		table.insert(list, { spend, spend + 0x80 })
		-- The IRQ stack under the hook's frame, and the System stack under
		-- where SAVE found it (spend + 0x30): dead at a frame's end, while
		-- the game waits there for VBlank. A load gives the game back some
		-- cycles off the save's, so an IRQ can come an instruction later and
		-- leave another return address there (Pokémon Emerald)
		local sp = E:read32(spend + 0x20)
		if sp >= 0x03000000 and sp < 0x03008000 then
			table.insert(list, { sp - 0x400, sp + 0x18 })
		end
		local sys = E:read32(spend + 0x30)
		if sys >= 0x03000000 and sys <= 0x03008000 then
			table.insert(list, { sys - 0xC00, sys })
		end
		-- and the Supervisor stack under where SAVE found it (SP_SVC, after
		-- its magic "TSVC"), the BIOS's calls' and the stub's
		if E:read32(spend + 0x60) == 0x43565354 then
			local svc = E:read32(spend + 0x64)
			if svc >= 0x03000000 and svc <= 0x03008000 then
				table.insert(list, { svc - 0x100, svc })
			end
		end
	end
	return list
end

local function masked(base, data, list)
	for _, s in ipairs(list) do
		local a, b = s[1] - base, s[2] - base
		if b > 0 and a < #data then
			a = math.max(a, 0)
			b = math.min(b, #data)
			data = data:sub(1, a) .. string.rep("\0", b - a) .. data:sub(b + 1)
		end
	end
	return data
end

-- {address, hash} per block
local REGIONS = { { 0x02000000, 0x40000 }, { 0x03000000, 0x8000 } }
local function digest()
	local list = skips()
	local out = {}
	local raw = {}
	for _, region in ipairs(REGIONS) do
		local data = masked(region[1], E:readRange(region[1], region[2]), list)
		raw[#raw + 1] = data
		for off = 1, #data, BLOCK do
			local h = 0
			for pos = off, off + BLOCK - 1, 128 do
				local v = { string.unpack(UNPACK, data, pos) }
				for i = 1, 16 do
					h = (h ~ v[i]) * 0x100000001B3
				end
			end
			out[#out + 1] = h
		end
	end
	-- and last, one hash of what the display reads that a CPU can read too:
	-- palette, VRAM, OAM and the readable display registers
	local video = E:readRange(0x05000000, 0x400) .. E:readRange(0x06000000, 0x18000) .. E:readRange(0x07000000, 0x400)
	for _, r in ipairs({ 0x00, 0x08, 0x0A, 0x0C, 0x0E, 0x48, 0x4A, 0x50, 0x52 }) do
		video = video .. string.pack("<I2", E:read16(0x04000000 + r))
	end
	video = video .. string.rep("\0", (-#video) % 128)
	local h = 0
	for pos = 1, #video, 128 do
		local v = { string.unpack(UNPACK, video, pos) }
		for i = 1, 16 do
			h = (h ~ v[i]) * 0x100000001B3
		end
	end
	out[#out + 1] = h
	if DEBUG then
		out.raw = table.concat(raw)
	end
	return out
end

local function block_address(i)
	local n = REGIONS[1][2] // BLOCK
	if i <= n then
		return REGIONS[1][1] + (i - 1) * BLOCK
	end
	return REGIONS[2][1] + (i - n - 1) * BLOCK
end

-- --- The menu ----------------------------------------------------------------

-- The menu key held 20 frames, then each key pressed for 4 frames, 8 apart
-- (hook_watch.lua's timing). False if the menu didn't open.
-- on_open runs at the first frame end with the menu up (the game frozen).
-- A game keeps its VBlank IRQ off now and then (a loading screen), when the
-- menu can't open: the key is tried again, 60 frames apart, 8 times.
local function menu(names, on_open)
	local opened = false
	for _ = 1, 8 do
		E:setKeys(MENU_KEY)
		for i = 1, 40 do
			tick()
			if not opened and overlay_up() then
				opened = true
				if on_open then
					on_open()
				end
			end
			if i == 20 then
				E:setKeys(0)
			end
		end
		if opened then
			break
		end
		-- A game asleep by its own choice (its pause menu's Sleep: SWI Stop,
		-- woken by the keypad IRQ) runs no VBlank for the hook: its wake
		-- keys, KEYCNT's, wake it
		local keycnt = E:read16(0x04000132)
		if E:read16(0x04000200) & 0x1000 ~= 0 and keycnt & 0x4000 ~= 0 and keycnt & 0x3FF ~= 0 then
			log(string.format("woke the game from its own sleep: keys %03X", keycnt & 0x3FF))
			E:setKeys(keycnt & 0x3FF)
			frames(10)
			E:setKeys(0)
			frames(30)
		end
		if DEBUG then
			log(string.format("debug no menu: vector %08X IE %04X IME %04X DISPSTAT %04X pc %08X",
				E:read32(0x03007FFC), E:read16(0x04000200), E:read16(0x04000208), E:read16(0x04000004),
				E:readRegister("pc")))
		end
		frames(60)
	end
	if not opened then
		local vector = E:read32(0x03007FFC)
		if patchStart and (vector < patchStart or vector >= patchStart + PATCH_SIZE) then
			log(string.format("fail unhooked: the game put its own IRQ handler %08X over the hook", vector))
		end
		return false
	end
	for _, name in ipairs(names) do
		E:setKeys(KEYS[name])
		frames(4)
		E:setKeys(0)
		frames(4)
	end
	return true
end

-- What the BG layers show (the game's BG palette and VRAM to 0x06010000;
-- the menu borrows OBJ ones)
local function bg_look()
	return E:readRange(0x05000000, 0x200) .. E:readRange(0x06000000, 0x10000)
end

-- Waits for the game to go on after the stub; the frame, or nil. With
-- look (bg_look() as the menu opened), also how many frame ends had the
-- layers on (DISPCNT) over BG memory other than that: a load showing the
-- state's VRAM under other display registers
local function resumed(limit, look)
	local mixed = 0
	local last_if, last_ie = 0, 0
	for _ = 1, limit do
		if not in_stub() then
			if DEBUG then
				log(string.format("debug resumed: IF %04X IE %04X at the frame before", last_if, last_ie))
			end
			return E:currentFrame(), mixed
		end
		last_if, last_ie = E:read16(0x04000202), E:read16(0x04000200)
		if look and not overlay_up() then
			local dispcnt = E:read16(0x04000000)
			if dispcnt & 0x1F00 ~= 0 and dispcnt & 0x80 == 0 and bg_look() ~= look then
				mixed = mixed + 1
				if DEBUG then
					log(string.format("debug mixed frame %d DISPCNT %04X pc %08X", E:currentFrame(), dispcnt,
						E:readRegister("pc")))
				end
			end
		end
		tick()
	end
	return nil
end

local function info()
	local dispcnt = E:read16(0x04000000)
	local bldcnt = E:read16(0x04000050)
	local sndh = E:read16(0x04000082)
	local dma = {}
	for ch = 0, 3 do
		local cnt = E:read16(0x040000BA + 12 * ch)
		if cnt & 0x8000 ~= 0 then
			dma[#dma + 1] = string.format("%d:%d", ch, (cnt >> 12) & 3)
		end
	end
	return string.format("mode %d bld %d fifo %s dma %s", dispcnt & 7, (bldcnt >> 6) & 3,
		sndh & 0x3300 ~= 0 and "on" or "off", #dma > 0 and table.concat(dma, ",") or "-")
end

-- The replay of NAME's keys from the frame the game went on: its digests
local function replay(name)
	local plan = key_plan(seed_of(name), K)
	local digests = {}
	for j = 1, K do
		digests[j] = digest()
		E:setKeys(plan[j])
		tick()
	end
	E:setKeys(0)
	return digests
end

local function compare(name, got)
	local want = records[name]
	if not want then
		log("fail no record " .. name)
		return
	end
	local video, memory
	for j = 1, K do
		local a, b = want[j], got[j]
		if not a or not b then
			log(string.format("fail %s: %d frames recorded", name, #want))
			return
		end
		if a[#a] ~= b[#b] and not video then
			video = j - 1
		end
		local first, count = nil, 0
		for i = 1, #a - 1 do
			if a[i] ~= b[i] then
				first = first or i
				count = count + 1
			end
		end
		if first and not memory then
			memory = true
			log(string.format("differs %s +%d %08X %d blocks", name, j - 1, block_address(first), count))
			if a.raw and b.raw then
				local shown = {}
				for at = (first - 1) * BLOCK + 1, #a.raw do
					if a.raw:byte(at) ~= b.raw:byte(at) and #shown < 24 then
						local i = (at - 1) // BLOCK + 1
						shown[#shown + 1] = string.format("%08X:%02X/%02X", block_address(i) + (at - 1) % BLOCK,
							a.raw:byte(at), b.raw:byte(at))
					end
				end
				log("debug " .. table.concat(shown, " "))
			end
		end
	end
	if video then
		log(string.format("vdiffers %s +%d", name, video))
	end
	if not memory then
		log(string.format("same %s %d", name, K))
	end
end

-- --- Steps -------------------------------------------------------------------

local playSeed = seed_of("play")

local function play(n)
	local r = rng(playSeed)
	playSeed = (playSeed * 1103515245 + 12345) & 0xFFFFFFFF
	local plan = key_plan(r(0x7FFFFFFF) + 1, n)
	for i = 1, n do
		E:setKeys(plan[i])
		tick()
	end
	E:setKeys(0)
end

-- Waits for a fade or transition, up to limit frames; true if one came
local function seek_fade(limit)
	local pal = E:readRange(0x05000000, 0x400)
	for _ = 1, limit do
		tick()
		local now = E:readRange(0x05000000, 0x400)
		local changed = 0
		for i = 1, #now, 2 do
			if now:byte(i) ~= pal:byte(i) or now:byte(i + 1) ~= pal:byte(i + 1) then
				changed = changed + 1
			end
		end
		pal = now
		if (E:read16(0x04000050) >> 6) & 3 >= 2 or changed >= 64 then
			return true
		end
	end
	return false
end

local function step_save(slot, name)
	local before = info()
	-- The Save state page opens on the buffer's slot (slot 1 when empty)
	local names = { "DOWN", "A" }
	local at = bufslot > 0 and bufslot or 1
	for _ = at + 1, slot do
		names[#names + 1] = "DOWN"
	end
	for _ = slot + 1, at do
		names[#names + 1] = "UP"
	end
	names[#names + 1] = "A"
	if not menu(names) then
		log("fail menu didn't open for save " .. name)
		return false
	end
	local f = resumed(1200)
	if not f then
		log("fail the stub never gave the game back after save " .. name)
		return false
	end
	bufslot = slot
	log(string.format("saved %s %d %s", name, f, before))
	records[name] = replay(name)
	return true
end

local function step_load(name, fade)
	local note = ""
	if fade then
		note = seek_fade(600) and " at a transition" or " (no transition came)"
	end
	local before = info()
	local look
	if not menu({ "DOWN", "DOWN", "A", "A" }, function()
			look = bg_look()
		end) then
		log("fail menu didn't open for load " .. name)
		return false
	end
	local f, mixed = resumed(1200, look)
	if not f then
		log("fail the stub never gave the game back after load " .. name)
		return false
	end
	log(string.format("loaded %s %d %s%s", name, f, before, note))
	if mixed > 0 then
		log(string.format("mixed %s %d", name, mixed))
	end
	compare(name, replay(name))
	return true
end

local function step_autoload(name)
	-- the stub's code at two frame ends in a row: the load under way (not
	-- the overlay's magic, which the kernel's launch may leave in EWRAM
	-- until the game clears it)
	local inside = 0
	for _ = 1, 3000 do
		local pc = E:readRegister("pc")
		inside = patchStart and pc >= patchStart and pc < patchStart + PATCH_SIZE and inside + 1 or 0
		if inside >= 2 then
			break
		end
		tick()
	end
	if inside < 2 then
		log("fail no load at the game's start for " .. name)
		return false
	end
	local f = resumed(1200)
	if not f then
		log("fail the stub never gave the game back after the load at the start")
		return false
	end
	log(string.format("loaded %s %d %s at the game's start", name, f, info()))
	compare(name, replay(name))
	return true
end

local function write_records()
	if not OUT then
		return
	end
	local f = io.open(OUT, "w")
	for name, list in pairs(records) do
		f:write("record ", name, "\n")
		for _, d in ipairs(list) do
			local hex = {}
			for i, h in ipairs(d) do
				hex[i] = string.format("%x", h)
			end
			f:write(table.concat(hex, " "), "\n")
		end
		if list[1] and list[1].raw then
			local r = io.open(OUT .. "-" .. name .. ".raw", "wb")
			r:write(list[1].raw)
			r:close()
		end
	end
	f:close()
end

local function read_records()
	if not IN then
		return
	end
	local f = io.open(IN, "r")
	if not f then
		log("fail can't read " .. IN)
		return
	end
	local list
	for line in f:lines() do
		local name = line:match("^record (%S+)")
		if name then
			list = {}
			records[name] = list
			local r = DEBUG and io.open(IN .. "-" .. name .. ".raw", "rb")
			if r then
				list.raw = r:read("a")
				r:close()
			end
		else
			local d = {}
			for word in line:gmatch("%x+") do
				d[#d + 1] = tonumber(word, 16)
			end
			if #list == 0 then
				d.raw = list.raw
			end
			list[#list + 1] = d
		end
	end
	f:close()
end

local function run()
	read_records()
	for word in STEPS:gmatch("%S+") do
		local parts = {}
		for p in word:gmatch("[^:]+") do
			parts[#parts + 1] = p
		end
		local kind, ok = parts[1], true
		if kind == "play" then
			play(tonumber(parts[2]))
		elseif kind == "save" then
			ok = step_save(tonumber(parts[2]), parts[3])
		elseif kind == "load" then
			ok = step_load(parts[2], parts[3] == "fade")
		elseif kind == "autoload" then
			ok = step_autoload(parts[2])
		elseif kind == "reset" then
			-- Resume, then UP round the end: Back to Torii, Sleep, Reset game
			ok = menu({ "UP", "UP", "UP", "A" })
			if not ok then
				log("fail menu didn't open for reset")
			end
			frames(60)
		elseif kind == "torii" then
			write_records()
			ok = menu({ "UP", "A" })
			if not ok then
				log("fail menu didn't open for Back to Torii")
			end
			log("torii")
			return
		else
			log("fail no step " .. word)
			ok = false
		end
		if not ok then
			break
		end
	end
	write_records()
	log("done")
end

-- While the kernel has SD mode on, ROM reads return the cart's 4 KB SD
-- buffer, which repeats (hook_watch.lua)
local function sdMode()
	return emu:read32(0x08000000) == emu:read32(0x08001000) and emu:read32(0x080000A0) == emu:read32(0x080010A0)
end

callbacks:add("frame", function()
	if not gameStarted then
		local header = string.format("%08X%08X%08X%08X%08X", emu:read32(0x08000000), emu:read32(0x080000A0),
			emu:read32(0x080000A8), emu:read32(0x080000AC), emu:read32(0x080000BC))
		kernelHeader = kernelHeader or header
		if header == kernelHeader or sdMode() then
			return
		end
		gameStarted = true
		local entry = emu:read32(0x08000000)
		local target = 0x08000000 + 8 + ((entry & 0xFFFFFF) << 2)
		if entry >> 24 == 0xEA and emu:read32(target) == 0xE3A00301
				and emu:read32(target + 4) & 0xFFFFF000 == 0xE28F1000 then
			patchStart = target
		end
		log(string.format("game %08X patch %08X", entry, patchStart or 0))
		co = coroutine.create(run)
	end
	-- The steps until their next frame, making the calls they ask for
	local args = {}
	while co and coroutine.status(co) == "suspended" do
		local r = table.pack(coroutine.resume(co, table.unpack(args, 1, args.n)))
		if not r[1] then
			log("fail script error " .. tostring(r[2]))
			log("done")
			co = nil
		elseif r[2] == "call" then
			args = table.pack(r[3][r[4]](r[3], table.unpack(r, 5, r.n)))
		else
			break
		end
	end
end)
