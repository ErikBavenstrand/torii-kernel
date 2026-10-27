-- Checks the kernel's in-game IRQ hook from outside the game, for
-- tests/games.py: run the kernel with `mgba-shot -L tests/hook_watch.lua`,
-- launch a game with BOOT WITH ADDON (SAVESTATE on alone), and this
-- script takes over the keys once the game starts.
--
-- How the hook works: while loading the ROM, the kernel turns every literal
-- 0x3007FFC or 0x3FFFFFC (where games store their IRQ handler) into
-- 0x3007FF4, and makes the ROM's first instruction branch to its patch area
-- near the end of the ROM. That code stores the hook at 0x3007FFC and jumps
-- to the game. On a VBlank IRQ the hook opens the in-game menu if the menu
-- key (L+R+START) is held, then calls the game's handler at 0x3007FF4. The
-- hook is missed when the game still writes its handler to 0x3007FFC (no
-- literal the kernel recognised), or never enables the VBlank IRQ.
--
-- Script: GAME_RUN frames after the game starts, press START and A to get
-- past title screens, hold the menu key, press B (HOOK_WATCH_CLOSE) to leave the menu, watch
-- the game run on for AFTER frames (pressing DOWN, A, START, A), then a
-- save state round trip (the menu's Save state into slot 1 as "key state
-- save", Load state as "key state load", more keys after it), then log
-- "hook watch done" (stop the run
-- with `-u 'hook watch done'`). Other lines it logs (stderr, as
-- "script: hook FRAME ..."):
--
--   game ENTRY               the game started (the ROM header changed);
--                            ENTRY is its first ROM word
--   patch START              the entry branches to the kernel's entry code
--                            (it starts MOV R0,#0x4000000; ADR R1,hook)
--   vector XXXXXXXX          0x3007FFC changed (the IRQ handler in use)
--   chained XXXXXXXX         0x3007FF4 changed (the game's own handler)
--   key KEYS                 the script pressed KEYS (- releases)
--   vblank N of M            frames that ended with the VBlank IRQ fully
--                            enabled (IME, IE bit 0, DISPSTAT bit 3), from
--                            the game's start to the menu key
--   kernel back              the kernel's ROM is mapped again: the game
--                            reset to the kernel
--   menu open | closed       the menu is on screen: the overlay's header
--                            (source/ingame_abi.h) in the EWRAM window it
--                            borrows, saying it is shown
--   menu shown after N frames  from the menu key's frame to the first frame
--                            that ended with the menu's panel on screen
--   menu text shown after N frames  and with its text too
--   menu left the game as it was | menu changed REGION ...: the game's
--                            memory and registers at the menu key's frame
--                            against the first frame after the menu closed
--                            (see compare() for what can't be compared)
--
-- HOOK_WATCH_DUMP=DIR also writes those two snapshots to DIR/before-NAME.bin
-- and DIR/after-NAME.bin (tests/menu.sh).

local GAME_RUN = 600  -- frames of game before the menu key
local MENU_WAIT = 60  -- frames the menu gets to open before B
local AFTER = 600     -- frames the game runs on after B
-- Then a save state round trip (v0.8 P3), the menu's Save state into slot
-- 1 (DOWN, A, A) and, STATE_PLAY frames later, Load state (DOWN, DOWN, A,
-- A); the game runs on for STATE_AFTER frames, keys pressed as above.
local STATE_PLAY = 120
local STATE_AFTER = 360

local KEYS = { A = 0x001, B = 0x002, START = 0x008, DOWN = 0x080, R = 0x100, L = 0x200 }
local MENU_KEY = KEYS.L | KEYS.R | KEYS.START
-- The key that leaves the menu: B, or HOOK_WATCH_CLOSE (A where the stub
-- refuses to open it: its white screen takes A back to the game, B to Torii)
local CLOSE = os.getenv("HOOK_WATCH_CLOSE") or "B"

-- The overlay's header at INGAME_EWRAM while it runs (struct ingame_image)
local OVERLAY = 0x02000000
local OVERLAY_MAGIC = 0x31564F54
local OVERLAY_SHOWN = OVERLAY + 24

local kernelHeader = nil
local gameStart = nil
local patchStart = nil
local last = {}
local vblank, frames = 0, 0
local schedule = {}
local before = nil
local keyFrame, opened, texted, compared = nil, false, false, false

local function log(text)
	console:log(string.format("hook %d %s", emu:currentFrame(), text))
end

local function report(name, value)
	if last[name] ~= value then
		last[name] = value
		log(name .. " " .. value)
	end
end

local function press(mask, name)
	emu:setKeys(mask)
	log("key " .. name)
end

-- 0, or 1 once the menu's panel shows, 2 once its text does too
local function menuShown()
	if emu:read32(OVERLAY) ~= OVERLAY_MAGIC then
		return 0
	end
	return emu:read32(OVERLAY_SHOWN)
end

-- The I/O registers that read back what was written, and that the menu
-- gives back before its frame ends (SOUNDCNT_L/H, DMA0-3 CNT_H and IME come
-- back just after, as the game goes on: not compared)
local IO = { 0x00, 0x08, 0x0A, 0x0C, 0x0E, 0x48, 0x4A, 0x50, 0x52, 0x60, 0x62, 0x68, 0x6C, 0x70, 0x72, 0x78, 0x7C,
	0x84, 0x88, 0x102, 0x106, 0x10A, 0x10E, 0x132, 0x200, 0x204 }

local function snapshot()
	local io = {}
	for _, r in ipairs(IO) do
		io[#io + 1] = string.pack("<I2", emu:read16(0x04000000 + r))
	end
	return {
		{ "EWRAM", 0x02000000, emu:readRange(0x02000000, 0x40000) },
		{ "IWRAM", 0x03000000, emu:readRange(0x03000000, 0x8000) },
		{ "palette", 0x05000000, emu:readRange(0x05000000, 0x400) },
		{ "VRAM", 0x06000000, emu:readRange(0x06000000, 0x18000) },
		{ "OAM", 0x07000000, emu:readRange(0x07000000, 0x400) },
		{ "I/O", 0x04000000, table.concat(io) },
	}
end

local function dump(dir, tag, snap)
	for _, region in ipairs(snap) do
		local f = io.open(string.format("%s/%s-%s.bin", dir, tag, region[1]:gsub("/", "")), "wb")
		if f then
			f:write(region[3])
			f:close()
		end
	end
end

-- Byte ranges [first, last) not compared: what the hook and its stub keep in
-- the spend area (the 0x80 bytes the patch reserves at the game's stack
-- top), the IRQ stack under the hook (the BIOS's frame, the hook's and
-- sram_copy's words) and the BIOS's words at 0x3007FE0-0x3007FFF.
local function skipped()
	local spend = patchStart and emu:read32(patchStart + 0x18) or 0
	local skips = { { 0x03007FE0, 0x03008000 } }
	if spend ~= 0 then
		table.insert(skips, { spend, spend + 0x80 })
		local sp = emu:read32(spend + 0x20)  -- the IRQ stack at the hook
		if sp >= 0x03000000 and sp < 0x03008000 then
			table.insert(skips, { sp - 0x40, sp + 0x18 })
		end
	end
	return skips
end

-- What the game should find after the menu. The snapshot at the menu key's
-- frame comes a few instructions early: the game runs on until the CPU takes
-- the VBlank IRQ (mGBA delays IRQs by a few cycles), and some games store
-- something there. So everything the menu leaves alone comes from a
-- snapshot taken while it is up, the game frozen, and only what it borrows
-- from the key frame's: the EWRAM window, OBJ tiles 512 on, OAM, the OBJ
-- palette, DISPCNT, BLDCNT, BLDALPHA and WAITCNT.
local function reference(early, frozen, window)
	local function mix(i, cut, borrowed_first)
		local a, b = early[i][3], frozen[i][3]
		return borrowed_first and a:sub(1, cut) .. b:sub(cut + 1) or b:sub(1, cut) .. a:sub(cut + 1)
	end
	local io = {}
	for n, r in ipairs(IO) do
		local from = (r == 0x00 or r == 0x50 or r == 0x52 or r == 0x204) and early or frozen
		io[n] = from[6][3]:sub(2 * n - 1, 2 * n)
	end
	return {
		{ "EWRAM", 0x02000000, mix(1, window, true) },
		{ "IWRAM", 0x03000000, frozen[2][3] },
		{ "palette", 0x05000000, mix(3, 0x200, false) },
		{ "VRAM", 0x06000000, mix(4, 0x14000, false) },
		{ "OAM", 0x07000000, early[5][3] },
		{ "I/O", 0x04000000, table.concat(io) },
	}
end

local function compare(after)
	local skips = skipped()
	local changes = {}
	for i, region in ipairs(before) do
		local name, base, a, b = region[1], region[2], region[3], after[i][3]
		if a ~= b then
			local first, count = nil, 0
			for off = 1, #a do
				if a:byte(off) ~= b:byte(off) then
					local address = base + off - 1
					local skip = false
					for _, s in ipairs(skips) do
						if name ~= "I/O" and address >= s[1] and address < s[2] then
							skip = true
						end
					end
					if not skip then
						count = count + 1
						if name == "I/O" then
							address = 0x04000000 + IO[(off - 1) // 2 + 1]
						end
						first = first or address
					end
				end
			end
			if count > 0 then
				table.insert(changes, string.format("%s %d bytes from %08X", name, count, first))
			end
		end
	end
	if #changes == 0 then
		log("menu left the game as it was")
	else
		local spend = patchStart and emu:read32(patchStart + 0x18) or 0
		log(string.format("menu changed %s (IRQ stack at the hook %08X, IF %04X)", table.concat(changes, ", "),
			spend ~= 0 and emu:read32(spend + 0x20) or 0, emu:read16(0x04000202)))
	end
end

-- Key presses relative to the game's start: {frame, mask, name}.
local function plan(start)
	local at = start + GAME_RUN
	schedule = {
		{ start + 300, KEYS.START, "START" }, { start + 306, 0, "-" },
		{ start + 420, KEYS.A, "A" }, { start + 426, 0, "-" },
		{ start + 480, KEYS.START, "START" }, { start + 486, 0, "-" },
		{ at, MENU_KEY, "L+R+START" }, { at + 6, 0, "-" },
		{ at + MENU_WAIT, KEYS[CLOSE], CLOSE }, { at + MENU_WAIT + 6, 0, "-" },
	}
	-- Keys after the menu, so a game waiting for input changes its screen.
	local back = at + MENU_WAIT
	for i, key in ipairs({ "DOWN", "A", "START", "A" }) do
		table.insert(schedule, { back + 120 * i, KEYS[key], key })
		table.insert(schedule, { back + 120 * i + 6, 0, "-" })
	end
	-- The save state round trip, menu keys 8 frames apart
	-- (the menu key held 20 frames, for a game whose VBlank IRQ is off now
	-- and then: the menu waits for its release)
	local function menu_keys(from, names, tag)
		table.insert(schedule, { from, MENU_KEY, tag })
		table.insert(schedule, { from + 20, 0, "-" })
		for i, key in ipairs(names) do
			table.insert(schedule, { from + 32 + 8 * i, KEYS[key], key })
			table.insert(schedule, { from + 36 + 8 * i, 0, "-" })
		end
	end
	local save = at + MENU_WAIT + AFTER - 40
	menu_keys(save, { "DOWN", "A", "A" }, "state save")
	menu_keys(save + STATE_PLAY, { "DOWN", "DOWN", "A", "A" }, "state load")
	for i, key in ipairs({ "DOWN", "A", "START", "A" }) do
		table.insert(schedule, { save + STATE_PLAY + 60 * (i + 1), KEYS[key], key })
		table.insert(schedule, { save + STATE_PLAY + 60 * (i + 1) + 6, 0, "-" })
	end
end

-- While the kernel has SD mode on, ROM reads return the cart's 4 KB SD
-- buffer (as on the real cart), and the kernel spends most of a launch's
-- frames in SD transfers: then the ROM repeats every 4 KB, which a real ROM's
-- entry branch and header never do.
local function sdMode()
	return emu:read32(0x08000000) == emu:read32(0x08001000) and emu:read32(0x080000A0) == emu:read32(0x080010A0)
end

callbacks:add("frame", function()
	local frame = emu:currentFrame()
	-- First word, title, code and header checksum: the kernel's until the
	-- game is mapped in. (No one of them will do: many games share the
	-- kernel's entry branch, and homebrew may have a blank title and code.)
	local header = string.format("%08X%08X%08X%08X%08X", emu:read32(0x08000000), emu:read32(0x080000A0),
		emu:read32(0x080000A8), emu:read32(0x080000AC), emu:read32(0x080000BC))
	kernelHeader = kernelHeader or header
	if not gameStart then
		if header == kernelHeader or sdMode() then
			return
		end
		gameStart = frame
		local entry = emu:read32(0x08000000)
		log(string.format("game %08X", entry))
		-- A hooked game's first instruction branches to the kernel's entry
		-- code after the ROM. With nothing to patch, the kernel leaves the
		-- game's own entry branch alone.
		local target = 0x08000000 + 8 + ((entry & 0xFFFFFF) << 2)
		if entry >> 24 == 0xEA and emu:read32(target) == 0xE3A00301
				and emu:read32(target + 4) & 0xFFFFF000 == 0xE28F1000 then
			patchStart = target
			log(string.format("patch %08X", patchStart))
		end
		plan(gameStart)
	elseif header == kernelHeader then
		report("kernel", "back")  -- the kernel is mapped again (a reset)
	end

	report("vector", string.format("%08X", emu:read32(0x03007FFC)))
	report("chained", string.format("%08X", emu:read32(0x03007FF4)))

	local menuKeyFrame = gameStart + GAME_RUN
	if frame < menuKeyFrame then
		frames = frames + 1
		local ime = emu:read16(0x04000208) & 1
		local ie = emu:read16(0x04000200) & 1
		local dispstat = emu:read16(0x04000004) & 8
		if ime ~= 0 and ie ~= 0 and dispstat ~= 0 then
			vblank = vblank + 1
		end
	elseif frame == menuKeyFrame then
		log(string.format("vblank %d of %d", vblank, frames))
	end

	local shown = menuShown()
	if frame >= menuKeyFrame then
		report("menu", shown > 0 and "open" or "closed")
	end
	if shown > 0 and not opened and keyFrame then
		opened = true
		log(string.format("menu shown after %d frames", frame - keyFrame))
		-- the game frozen under the menu
		local window = emu:read32(OVERLAY + 12)
		before = reference(before, snapshot(), window)
		local dir = os.getenv("HOOK_WATCH_DUMP")
		if dir then
			dump(dir, "before", before)
		end
	end
	if shown > 1 and not texted and keyFrame then
		texted = true
		log(string.format("menu text shown after %d frames", frame - keyFrame))
	end
	-- The first frame without the overlay in EWRAM after it showed: the stub
	-- has put everything back and waits for the frame after.
	if opened and not compared and emu:read32(OVERLAY) ~= OVERLAY_MAGIC then
		compared = true
		local after = snapshot()
		local dir = os.getenv("HOOK_WATCH_DUMP")
		if dir then
			dump(dir, "after", after)
		end
		compare(after)
	end

	for _, step in ipairs(schedule) do
		if step[1] == frame then
			if step[2] == MENU_KEY and not keyFrame then
				-- The menu opens in this frame's VBlank IRQ, right after this
				keyFrame = frame
				before = snapshot()
			end
			press(step[2], step[3])
		end
	end
	if frame == menuKeyFrame + MENU_WAIT + AFTER - 40 + STATE_PLAY + STATE_AFTER then
		console:log("hook watch done")
	end
end)
