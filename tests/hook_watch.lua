-- Checks the kernel's in-game IRQ hook from outside the game, for
-- tests/games.py: run the kernel with `mgba-shot -L tests/hook_watch.lua`,
-- launch a game with BOOT WITH ADDON (SAVESTATE and RESET on), and this
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
-- past title screens, hold the menu key, press B to leave the menu, watch
-- the game run on for AFTER frames (pressing DOWN, A, START, A), then log "hook watch done" (stop the run
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
--   menu open | closed       the menu is on screen: mode 3 with BG2 only,
--                            the CPU in the menu's code

local GAME_RUN = 600  -- frames of game before the menu key
local MENU_WAIT = 60  -- frames the menu gets to open before B
local AFTER = 600     -- frames the game runs on after B
local PATCH_SIZE = 0x2000  -- the largest patch area (cheats)

local KEYS = { A = 0x001, B = 0x002, START = 0x008, DOWN = 0x080, R = 0x100, L = 0x200 }
local MENU_KEY = KEYS.L | KEYS.R | KEYS.START

local kernelHeader = nil
local gameStart = nil
local patchStart = nil
local last = {}
local vblank, frames = 0, 0
local menuAt, closedAt = nil, nil
local schedule = {}

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

-- The menu's code, past the entry code and the hook's own key checks
-- (ingamemenu_now is 0x3B0 bytes in).
local function inMenuCode(address)
	return patchStart and address >= patchStart + 0x100 and address < patchStart + PATCH_SIZE
end

-- Key presses relative to the game's start: {frame, mask, name}.
local function plan(start)
	local at = start + GAME_RUN
	schedule = {
		{ start + 300, KEYS.START, "START" }, { start + 306, 0, "-" },
		{ start + 420, KEYS.A, "A" }, { start + 426, 0, "-" },
		{ start + 480, KEYS.START, "START" }, { start + 486, 0, "-" },
		{ at, MENU_KEY, "L+R+START" }, { at + 6, 0, "-" },
		{ at + MENU_WAIT, KEYS.B, "B" }, { at + MENU_WAIT + 6, 0, "-" },
	}
	-- Keys after the menu, so a game waiting for input changes its screen.
	local back = at + MENU_WAIT
	for i, key in ipairs({ "DOWN", "A", "START", "A" }) do
		table.insert(schedule, { back + 120 * i, KEYS[key], key })
		table.insert(schedule, { back + 120 * i + 6, 0, "-" })
	end
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
		if header == kernelHeader then
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
		plan(frame)
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

	local menu = emu:read16(0x04000000) == 0x0403 and inMenuCode(emu:readRegister("pc"))
	if frame >= menuKeyFrame then
		report("menu", menu and "open" or "closed")
	end

	for _, step in ipairs(schedule) do
		if step[1] == frame then
			press(step[2], step[3])
		end
	end
	if frame == menuKeyFrame + MENU_WAIT + AFTER then
		console:log("hook watch done")
	end
end)
