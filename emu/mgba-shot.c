// Headless mGBA runner: boot a ROM, press keys on a schedule, save screenshots.
//
// usage: mgba-shot ROM [-f FRAMES] [-k FRAME:KEYS]... [-s FRAME:OUT.png]...
//   -f FRAMES        total frames to run (default: last scheduled event)
//   -k FRAME:KEYS    from FRAME on, hold KEYS (A+B+START, UP, ... or - for none)
//   -s FRAME:PATH    write a PNG of the screen after FRAME
//   -S PATH          write the emulated save chip's contents when done
//   -i FILE          read more options from FILE, whitespace-separated
//                    (# starts a comment, "double quotes" keep blanks in a
//                    word), for long input scripts
//   -g PORT          wait for gdb on PORT (all addresses) before running
//   -y SYMFILE       symbols for crash reports (default: the ROM's .sym)
//   -m FRAME:ADDR:LEN  after FRAME, print LEN bytes of memory from ADDR (hex)
//   -r FRAME         after FRAME, print the CPU registers
//   -h FRAME         after FRAME, print a hash of the screen (same picture,
//                    same hash), to compare runs without saving PNGs
//   -p FRAME:ADDR:VALUE  before FRAME, write a 32-bit VALUE to ADDR
//   -u TEXT          stop after the frame in which the kernel (or a -L
//                    script) logs TEXT
//   -x FRAME:PATH    after FRAME, write a save state (with the cart's state)
//   -l PATH          load a save state before the first frame
//   -L SCRIPT        run a Lua script (mGBA's scripting API: emu, callbacks,
//                    socket, console); with -L and no -f, run until killed
//   -t               run at the GBA's real speed instead of as fast as possible
//   -w TEXT          wait until the kernel (or a -L script) logs TEXT, from
//                    the frame of the last key press (or -p) between the
//                    previous -w and this one; a FRAME of @N in
//                    a later option means N frames after the frame that line
//                    came in (keys at @N go down before frame N runs, outputs
//                    come after it). The run ends once the last -w is met and
//                    its @ events are done. A -w not met within a minute of
//                    game time (or by -f) fails the run with status 4
//   -H FROM:TO       print the screen's hash after every frame from FROM to
//                    TO (both @N or both plain frames), as -h does
//   -c EPOCH         pin the cart's clock: it reads EPOCH (Unix seconds; the
//                    GBA sees local time, so set TZ=UTC) at power-on and runs
//                    with emulated time, not the host's
//   -O DIR           write -s PNGs given relative paths into DIR
//
// If the CPU hits an illegal opcode or jumps to an invalid address, it prints
// a crash report (registers, and code addresses found on the stack with their
// symbols) and exits with status 3.
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba/core/serialize.h>
#ifdef ENABLE_SCRIPTING
#include <mgba/core/scripting.h>
#include <mgba/script/context.h>
#endif
#include <mgba/internal/arm/arm.h>
#include <mgba-util/png-io.h>
#include <mgba-util/vfs.h>
#ifdef USE_GDB_STUB
#include <mgba/debugger/debugger.h>
#include <mgba/internal/debugger/gdb-stub.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct event {
	unsigned frame;  // or, after a -w, the offset from the frame it was met
	char *arg;
	int wait;  // index of the -w it follows if FRAME was @N, else -1
	unsigned end;  // -H: the last frame, of the same kind as frame
};

// -w steps, met in order.
#define MAX_WAITS 64
struct wait {
	const char *text;
	unsigned hit;  // the frame its line came in; 0 until then
	unsigned last;  // the largest @ offset among the events that follow it
	unsigned press;  // the largest @ offset of a key press or poke among them
};
static struct wait waits[MAX_WAITS];
static int nwaits;
static int next_wait;  // the first wait not met yet
static unsigned first_arm;  // the last plain key press or poke frame before the first -w
static unsigned current_frame;

static const char *KEY_NAMES[] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};

// --- Crash reports ---------------------------------------------------------

struct symbol {
	uint32_t address;
	char *name;
};

static struct symbol *symbols;
static size_t nsymbols;

// Reads an armips symbol file (ADDRESS NAME per line, sorted by address).
static void load_symbols(const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		return;
	}
	size_t cap = 0;
	char line[512], name[480];
	unsigned address;
	while (fgets(line, sizeof(line), f)) {
		if (sscanf(line, "%x %479s", &address, name) != 2) {
			continue;
		}
		if (nsymbols == cap) {
			cap = cap ? cap * 2 : 1024;
			symbols = realloc(symbols, cap * sizeof(*symbols));
		}
		symbols[nsymbols].address = address;
		symbols[nsymbols].name = strdup(name);
		++nsymbols;
	}
	fclose(f);
}

// "name+0xOFFSET" for the symbol at or below address, or "" if none is close.
static const char *symbolize(uint32_t address)
{
	static char out[512];
	const struct symbol *best = NULL;
	for (size_t i = 0; i < nsymbols && symbols[i].address <= address; ++i) {
		best = &symbols[i];
	}
	if (!best || address - best->address > 0x10000) {
		return "";
	}
	snprintf(out, sizeof(out), " (%s+0x%X)", best->name, address - best->address);
	return out;
}

static const char *until_text;
static int until_hit;

#define WAIT_FRAMES 3600  // how long a -w waits: a minute

// The frame lines start to count for the next -w from.
static unsigned wait_start(void)
{
	return next_wait ? waits[next_wait - 1].hit + waits[next_wait - 1].press : first_arm;
}

// A line the kernel or a script logged: checks -u and the next -w.
static void saw_line(const char *line)
{
	if (until_text && strstr(line, until_text)) {
		until_hit = 1;
	}
	if (next_wait < nwaits) {
		if (current_frame >= wait_start() && strstr(line, waits[next_wait].text)) {
			waits[next_wait++].hit = current_frame;
		}
	}
}

static struct mCore *crash_core;
static int crashed;
static char crash_reason[256];
static uint32_t crash_regs[16], crash_cpsr;

static void note_crash(const char *format, va_list args)
{
	if (crashed || !crash_core) {
		return;
	}
	crashed = 1;
	vsnprintf(crash_reason, sizeof(crash_reason), format, args);
	struct ARMCore *cpu = crash_core->cpu;
	memcpy(crash_regs, cpu->gprs, sizeof(crash_regs));
	crash_cpsr = cpu->cpsr.packed;
}

static int is_code(uint32_t address)
{
	address &= ~1u;
	return (address >= 0x08000000 && address < 0x0A000000) || (address >= 0x03000000 && address < 0x03008000) ||
	       (address >= 0x02000000 && address < 0x02040000);
}

static void print_crash(unsigned frame)
{
	fprintf(stderr, "crash at frame %u: %s\n", frame, crash_reason);
	for (int i = 0; i < 13; i += 4) {
		for (int j = i; j < i + 4 && j < 13; ++j) {
			fprintf(stderr, "  r%-2d %08X", j, crash_regs[j]);
		}
		fputc('\n', stderr);
	}
	fprintf(stderr, "  sp  %08X  cpsr %08X (%s)\n", crash_regs[ARM_SP], crash_cpsr,
		crash_cpsr & 0x20 ? "thumb" : "arm");
	fprintf(stderr, "  lr  %08X%s\n", crash_regs[ARM_LR], symbolize(crash_regs[ARM_LR] & ~1u));
	fprintf(stderr, "  pc  %08X%s\n", crash_regs[ARM_PC], symbolize(crash_regs[ARM_PC] & ~1u));
	fprintf(stderr, "  code addresses on the stack:\n");
	int shown = 0;
	for (uint32_t sp = crash_regs[ARM_SP] & ~3u;
	     sp < 0x03008000 && sp < (crash_regs[ARM_SP] & ~3u) + 0x400 && shown < 16; sp += 4) {
		uint32_t word = crash_core->busRead32(crash_core, sp);
		if (is_code(word) && (word & 1)) {  // Thumb return addresses
			fprintf(stderr, "    [%08X] %08X%s\n", sp, word, symbolize(word & ~1u));
			++shown;
		}
	}
}

// Prints errors, and every line the kernel logs with tlog (category
// gba.debug) as "kernel: ..." or "kernel error: ...".
static void quiet_log(struct mLogger *logger, int category, enum mLogLevel level, const char *format, va_list args)
{
	(void)logger;
	const char *id = mLogCategoryId(category);
	if ((!strcmp(id, "gba.memory") && (level & (mLOG_GAME_ERROR | mLOG_FATAL)) &&
	     strstr(format, "invalid address")) ||
	    (!strcmp(id, "gba") && !strncmp(format, "Illegal opcode", 14))) {
		note_crash(format, args);
		return;  // The crash report has it, and it repeats until the frame ends.
	}
	if (!strcmp(id, "script")) {  // console:log and friends from -L scripts
		char line[512];
		vsnprintf(line, sizeof(line), format, args);
		fprintf(stderr, "script: %s\n", line);
		saw_line(line);
		return;
	}
	if (!strcmp(id, "gba.debug")) {
		char line[512];
		vsnprintf(line, sizeof(line), format, args);
		fprintf(stderr, "%s%s\n", level & (mLOG_FATAL | mLOG_ERROR) ? "kernel error: " : "kernel: ", line);
		saw_line(line);
		return;
	}
	if (level & (mLOG_FATAL | mLOG_ERROR)) {
		vfprintf(stderr, format, args);
		fputc('\n', stderr);
	}
}

static uint32_t parse_keys(const char *spec)
{
	uint32_t keys = 0;
	if (!strcmp(spec, "-")) {
		return 0;
	}
	char *copy = strdup(spec);
	for (char *tok = strtok(copy, "+"); tok; tok = strtok(NULL, "+")) {
		size_t i;
		for (i = 0; i < sizeof(KEY_NAMES) / sizeof(*KEY_NAMES); ++i) {
			if (!strcmp(tok, KEY_NAMES[i])) {
				keys |= 1u << i;
				break;
			}
		}
		if (i == sizeof(KEY_NAMES) / sizeof(*KEY_NAMES)) {
			fprintf(stderr, "unknown key: %s\n", tok);
			exit(2);
		}
	}
	free(copy);
	return keys;
}

// FRAME, or @N after the last -w so far, into ev.
static void parse_frame(struct event *ev, const char *spec)
{
	ev->wait = -1;
	if (*spec == '@') {
		if (!nwaits) {
			fprintf(stderr, "%s: an @ frame needs a -w before it\n", spec);
			exit(2);
		}
		ev->wait = nwaits - 1;
		++spec;
	}
	ev->frame = (unsigned)strtoul(spec, NULL, 10);
}

static struct event parse_event(const char *spec)
{
	struct event ev = {0};
	char *colon = strchr(spec, ':');
	if (!colon) {
		fprintf(stderr, "expected FRAME:VALUE, got %s\n", spec);
		exit(2);
	}
	parse_frame(&ev, spec);
	ev.arg = colon + 1;
	ev.end = ev.frame;
	return ev;
}

// The frame ev starts at, or 0 while its -w is not met.
static unsigned start_frame(const struct event *ev)
{
	if (ev->wait < 0) {
		return ev->frame;
	}
	const struct wait *w = &waits[ev->wait];
	return w->hit ? w->hit + ev->frame : 0;
}

// Whether ev happens at frame.
static int due(const struct event *ev, unsigned frame)
{
	return start_frame(ev) == frame;
}

// Whether frame lies in ev's range (-H).
static int in_range(const struct event *ev, unsigned frame)
{
	unsigned start = start_frame(ev);
	return start && frame >= start && frame <= start + (ev->end - ev->frame);
}

struct events {
	struct event *list;
	size_t count;
	size_t cap;
};

static struct event *add_event(struct events *events, const char *spec)
{
	if (events->count == events->cap) {
		events->cap = events->cap ? events->cap * 2 : 64;
		events->list = realloc(events->list, events->cap * sizeof(*events->list));
	}
	struct event *ev = &events->list[events->count++];
	*ev = parse_event(spec);
	return ev;
}

// Notes an event's frame for the -w it follows; press: a key press or a poke,
// which the next -w waits for.
static void note_frame(const struct event *ev, int press)
{
	if (ev->wait >= 0) {
		struct wait *w = &waits[ev->wait];
		w->last = ev->end > w->last ? ev->end : w->last;
		if (press && ev->frame > w->press) {
			w->press = ev->frame;
		}
	} else if (press && !nwaits && ev->frame > first_arm) {
		first_arm = ev->frame;
	}
}

struct options {
	struct events keys;
	struct events shots;
	struct events dumps;
	struct events regs;
	struct events hashes;
	struct events pokes;
	struct events states;
	struct events ranges;
	const char *outDir;
	long long epoch;
	int pinClock;
	const char *loadState;
	const char *scripts[16];
	size_t nscripts;
	int realtime;
	const char *until;
	unsigned frames;
	const char *saveOut;
	unsigned gdbPort;
	const char *symFile;
};

static void parse_options(struct options *opts, int argc, char **argv);

static void parse_file(struct options *opts, const char *path)
{
	FILE *f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "cannot read %s\n", path);
		exit(2);
	}
	size_t cap = 64, argc = 0;
	char **argv = malloc(cap * sizeof(*argv));
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		char *hash = strchr(line, '#');
		if (hash) {
			*hash = '\0';
		}
		// Words split at blanks; "a quoted word" keeps its blanks
		for (char *p = line;;) {
			p += strspn(p, " \t\r\n");
			if (!*p) {
				break;
			}
			char *end;
			if (*p == '"') {
				end = strchr(++p, '"');
				if (!end) {
					fprintf(stderr, "%s: unclosed quote\n", path);
					exit(2);
				}
			} else {
				end = p + strcspn(p, " \t\r\n");
			}
			if (argc == cap) {
				cap *= 2;
				argv = realloc(argv, cap * sizeof(*argv));
			}
			argv[argc++] = strndup(p, (size_t)(end - p));
			p = *end ? end + 1 : end;
		}
	}
	fclose(f);
	parse_options(opts, (int)argc, argv);
}

static void parse_options(struct options *opts, int argc, char **argv)
{
	for (int i = 0; i < argc; ++i) {
		if (i + 1 >= argc && strcmp(argv[i], "-t")) {
			fprintf(stderr, "missing value for %s\n", argv[i]);
			exit(2);
		}
		if (!strcmp(argv[i], "-f")) {
			opts->frames = (unsigned)strtoul(argv[++i], NULL, 10);
		} else if (!strcmp(argv[i], "-k")) {
			struct event *ev = add_event(&opts->keys, argv[++i]);
			note_frame(ev, strcmp(ev->arg, "-") != 0);
		} else if (!strcmp(argv[i], "-S")) {
			opts->saveOut = argv[++i];
		} else if (!strcmp(argv[i], "-s")) {
			note_frame(add_event(&opts->shots, argv[++i]), 0);
		} else if (!strcmp(argv[i], "-m")) {
			note_frame(add_event(&opts->dumps, argv[++i]), 0);
		} else if (!strcmp(argv[i], "-r")) {
			char *spec = malloc(strlen(argv[++i]) + 2);
			sprintf(spec, "%s:", argv[i]);
			note_frame(add_event(&opts->regs, spec), 0);
		} else if (!strcmp(argv[i], "-h")) {
			char *spec = malloc(strlen(argv[++i]) + 2);
			sprintf(spec, "%s:", argv[i]);
			note_frame(add_event(&opts->hashes, spec), 0);
		} else if (!strcmp(argv[i], "-H")) {
			struct event *ev = add_event(&opts->ranges, argv[++i]);
			struct event end = {0};
			parse_frame(&end, ev->arg);
			if (end.wait != ev->wait || end.frame < ev->frame) {
				fprintf(stderr, "-H %s: FROM and TO must both be @N or both frames, FROM first\n",
					argv[i]);
				exit(2);
			}
			ev->end = end.frame;
			note_frame(ev, 0);
		} else if (!strcmp(argv[i], "-p")) {
			note_frame(add_event(&opts->pokes, argv[++i]), 1);
		} else if (!strcmp(argv[i], "-x")) {
			note_frame(add_event(&opts->states, argv[++i]), 0);
		} else if (!strcmp(argv[i], "-w")) {
			if (nwaits == MAX_WAITS) {
				fprintf(stderr, "more than %d -w options\n", MAX_WAITS);
				exit(2);
			}
			waits[nwaits++].text = argv[++i];
		} else if (!strcmp(argv[i], "-c")) {
			opts->epoch = strtoll(argv[++i], NULL, 10);
			opts->pinClock = 1;
		} else if (!strcmp(argv[i], "-O")) {
			opts->outDir = argv[++i];
		} else if (!strcmp(argv[i], "-t")) {
			opts->realtime = 1;
		} else if (!strcmp(argv[i], "-L") && opts->nscripts < 16) {
			opts->scripts[opts->nscripts++] = argv[++i];
		} else if (!strcmp(argv[i], "-l")) {
			opts->loadState = argv[++i];
		} else if (!strcmp(argv[i], "-u")) {
			opts->until = argv[++i];
		} else if (!strcmp(argv[i], "-y")) {
			opts->symFile = argv[++i];
		} else if (!strcmp(argv[i], "-g")) {
			opts->gdbPort = (unsigned)strtoul(argv[++i], NULL, 10);
		} else if (!strcmp(argv[i], "-i")) {
			parse_file(opts, argv[++i]);
		} else {
			fprintf(stderr, "unknown option: %s\n", argv[i]);
			exit(2);
		}
	}
}

static void print_registers(struct mCore *core, unsigned frame)
{
	struct ARMCore *cpu = core->cpu;
	printf("regs frame %u:", frame);
	for (int i = 0; i < 16; ++i) {
		printf(" r%d=%08X", i, cpu->gprs[i]);
	}
	printf(" cpsr=%08X\n", cpu->cpsr.packed);
}

static void dump_memory(struct mCore *core, unsigned frame, const char *spec)
{
	uint32_t address = strtoul(spec, NULL, 0);
	const char *colon = strchr(spec, ':');
	uint32_t length = colon ? strtoul(colon + 1, NULL, 0) : 16;
	for (uint32_t i = 0; i < length; i += 16) {
		printf("mem frame %u %08X:", frame, address + i);
		for (uint32_t j = i; j < i + 16 && j < length; ++j) {
			printf(" %02X", core->rawRead8(core, address + j, -1));
		}
		putchar('\n');
	}
}

static void poke_memory(struct mCore *core, const char *spec)
{
	uint32_t address = strtoul(spec, NULL, 0);
	const char *colon = strchr(spec, ':');
	uint32_t value = colon ? strtoul(colon + 1, NULL, 0) : 0;
	core->busWrite32(core, address, value);  // Like a CPU store, so the renderer sees it
}

// FNV-1a over the screen's pixels.
static uint64_t hash_screen(const color_t *pixels, unsigned width, unsigned height)
{
	uint64_t hash = 0xCBF29CE484222325ULL;
	const uint8_t *bytes = (const uint8_t *)pixels;
	for (size_t i = 0; i < (size_t)width * height * BYTES_PER_PIXEL; ++i) {
		hash = (hash ^ bytes[i]) * 0x100000001B3ULL;
	}
	return hash;
}

// The last plain frame among events, or frames if that is later.
static unsigned last_frame(const struct events *events, unsigned frames)
{
	for (size_t i = 0; i < events->count; ++i) {
		if (events->list[i].wait < 0) {
			frames = events->list[i].end > frames ? events->list[i].end : frames;
		}
	}
	return frames;
}

// Prints the screen's hash, as -h does.
static void print_hash(unsigned frame, const color_t *pixels, unsigned width, unsigned height)
{
	printf("hash frame %u: %016llX\n", frame, (unsigned long long)hash_screen(pixels, width, height));
}

static int save_png(const char *path, const color_t *pixels, unsigned width, unsigned height)
{
	struct VFile *vf = VFileOpen(path, O_CREAT | O_TRUNC | O_WRONLY);
	if (!vf) {
		return 0;
	}
	png_structp png = PNGWriteOpen(vf);
	png_infop info = PNGWriteHeader(png, width, height);
	int ok = PNGWritePixels(png, width, height, width, pixels);
	PNGWriteClose(png, info);
	vf->close(vf);
	return ok;
}

int main(int argc, char **argv)
{
	if (argc < 2) {
		fprintf(stderr,
			"usage: %s ROM [-f FRAMES] [-k FRAME:KEYS]... [-s FRAME:OUT.png]... [-S SAVE] [-i FILE] [-g "
			"PORT]\n",
			argv[0]);
		return 2;
	}
	struct options opts = {0};
	parse_options(&opts, argc - 2, argv + 2);
	struct event *keys = opts.keys.list;
	struct event *shots = opts.shots.list;
	size_t nkeys = opts.keys.count, nshots = opts.shots.count;
	unsigned frames = opts.frames;
	const char *saveOut = opts.saveOut;
	frames = last_frame(&opts.keys, frames);
	frames = last_frame(&opts.shots, frames);
	frames = last_frame(&opts.ranges, frames);
	frames = last_frame(&opts.dumps, frames);
	frames = last_frame(&opts.regs, frames);
	frames = last_frame(&opts.hashes, frames);
	frames = last_frame(&opts.pokes, frames);
	frames = last_frame(&opts.states, frames);
	unsigned last_plain = frames;  // a run with -w goes on at least to here
	until_text = opts.until;
	if ((until_text || nwaits) && !opts.frames) {
		frames = frames > 36000 ? frames : 36000;  // Give up after 10 minutes of game time.
	}

	static struct mLogger logger = {.log = quiet_log};
	mLogSetDefaultLogger(&logger);

	struct mCore *core = mCoreFind(argv[1]);
	if (!core || !core->init(core)) {
		fprintf(stderr, "cannot create a core for %s\n", argv[1]);
		return 1;
	}
	mCoreInitConfig(core, NULL);
	if (opts.pinClock) {
		core->rtc.override = RTC_FAKE_EPOCH;
		core->rtc.value = opts.epoch * 1000;
	}
	unsigned width, height;
	core->desiredVideoDimensions(core, &width, &height);
	color_t *buffer = calloc((size_t)width * height, BYTES_PER_PIXEL);
	core->setVideoBuffer(core, buffer, width);
	if (!mCoreLoadFile(core, argv[1])) {
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 1;
	}
	core->reset(core);
	crash_core = core;
#ifdef ENABLE_SCRIPTING
	static struct mScriptContext scriptContext;
	static struct mLogger scriptLogger = {.log = quiet_log};
	if (opts.nscripts) {
		mScriptContextInit(&scriptContext);
		mScriptContextAttachStdlib(&scriptContext);
		mScriptContextAttachSocket(&scriptContext);
		mScriptContextRegisterEngines(&scriptContext);
		mScriptContextAttachLogger(&scriptContext, &scriptLogger);
		mScriptContextAttachCore(&scriptContext, core);
		struct mScriptEngineContext *lua = HashTableLookup(&scriptContext.engines, "lua");
		for (size_t i = 0; i < opts.nscripts; ++i) {
			if (!lua || !mScriptContextLoadFile(&scriptContext, opts.scripts[i]) || !lua->run(lua)) {
				fprintf(stderr, "cannot run script %s\n", opts.scripts[i]);
				return 1;
			}
		}
		if (!opts.frames) {
			frames = frames > 0xFFFFFFF0u ? frames : 0xFFFFFFF0u;
		}
		mScriptContextTriggerCallback(&scriptContext, "start");
	}
#else
	if (opts.nscripts) {
		fprintf(stderr, "built without scripting\n");
		return 1;
	}
#endif
	if (opts.loadState) {
		struct VFile *vf = VFileOpen(opts.loadState, O_RDONLY);
		if (!vf || !mCoreLoadStateNamed(core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC)) {
			fprintf(stderr, "cannot load state %s\n", opts.loadState);
			return 1;
		}
		vf->close(vf);
	}
	if (opts.symFile) {
		load_symbols(opts.symFile);
	} else {
		char path[1024];
		snprintf(path, sizeof(path), "%s", argv[1]);
		char *dot = strrchr(path, '.');
		if (dot && dot > strrchr(path, '/')) {
			strcpy(dot, ".sym");
			load_symbols(path);
		}
	}

	struct mDebugger *debugger = NULL;
	if (opts.gdbPort) {
#ifdef USE_GDB_STUB
		static struct GDBStub gdb;
		GDBStubCreate(&gdb);
		struct Address any = {.version = IPV4, .ipv4 = 0};
		if (!GDBStubListen(&gdb, opts.gdbPort, &any, GDB_WATCHPOINT_STANDARD_LOGIC)) {
			fprintf(stderr, "cannot listen for gdb on port %u\n", opts.gdbPort);
			return 1;
		}
		debugger = &gdb.d;
		mDebuggerAttach(debugger, core);
		mDebuggerEnter(debugger, DEBUGGER_ENTER_MANUAL, NULL);
		fprintf(stderr, "waiting for gdb on port %u\n", opts.gdbPort);
#else
		fprintf(stderr, "built without the gdb stub\n");
		return 1;
#endif
	}

	int status = 0;
	unsigned frame;
	for (frame = 1; frame <= frames; ++frame) {
		current_frame = frame;
		for (size_t i = 0; i < nkeys; ++i) {
			if (due(&keys[i], frame)) {
				core->setKeys(core, parse_keys(keys[i].arg));
			}
		}
		for (size_t i = 0; i < opts.pokes.count; ++i) {
			if (due(&opts.pokes.list[i], frame)) {
				poke_memory(core, opts.pokes.list[i].arg);
			}
		}
		if (debugger) {
			mDebuggerRunFrame(debugger);
			if (debugger->state == DEBUGGER_SHUTDOWN) {
				break;
			}
		} else {
			core->runFrame(core);
		}
#ifdef ENABLE_SCRIPTING
		if (opts.nscripts) {
			mScriptContextTriggerCallback(&scriptContext, "frame");
		}
#endif
		if (opts.realtime) {
			// 59.7275 frames per second.
			static struct timespec next;
			struct timespec now;
			clock_gettime(CLOCK_MONOTONIC, &now);
			if (!next.tv_sec) {
				next = now;
			}
			next.tv_nsec += 16742706;
			if (next.tv_nsec >= 1000000000) {
				next.tv_nsec -= 1000000000;
				++next.tv_sec;
			}
			clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, NULL);
		}
		if (crashed) {
			print_crash(frame);
			status = 3;
			break;
		}
		for (size_t i = 0; i < opts.states.count; ++i) {
			if (due(&opts.states.list[i], frame)) {
				struct VFile *vf = VFileOpen(opts.states.list[i].arg, O_CREAT | O_TRUNC | O_RDWR);
				if (vf && mCoreSaveStateNamed(core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC)) {
					printf("state frame %u -> %s\n", frame, opts.states.list[i].arg);
				} else {
					fprintf(stderr, "cannot write state %s\n", opts.states.list[i].arg);
					status = 1;
				}
				if (vf) {
					vf->close(vf);
				}
			}
		}
		for (size_t i = 0; i < opts.dumps.count; ++i) {
			if (due(&opts.dumps.list[i], frame)) {
				dump_memory(core, frame, opts.dumps.list[i].arg);
			}
		}
		for (size_t i = 0; i < opts.regs.count; ++i) {
			if (due(&opts.regs.list[i], frame)) {
				print_registers(core, frame);
			}
		}
		for (size_t i = 0; i < opts.hashes.count; ++i) {
			if (due(&opts.hashes.list[i], frame)) {
				print_hash(frame, buffer, width, height);
			}
		}
		for (size_t i = 0; i < opts.ranges.count; ++i) {
			if (in_range(&opts.ranges.list[i], frame)) {
				print_hash(frame, buffer, width, height);
				break;  // once, if ranges overlap
			}
		}
		for (size_t i = 0; i < nshots; ++i) {
			if (due(&shots[i], frame)) {
				char path[1024];
				if (opts.outDir && shots[i].arg[0] != '/') {
					snprintf(path, sizeof(path), "%s/%s", opts.outDir, shots[i].arg);
				} else {
					snprintf(path, sizeof(path), "%s", shots[i].arg);
				}
				if (save_png(path, buffer, width, height)) {
					printf("frame %u -> %s (hash %016llX)\n", frame, path,
					       (unsigned long long)hash_screen(buffer, width, height));
				} else {
					fprintf(stderr, "cannot write %s\n", path);
					status = 1;
				}
			}
		}
		if (until_hit) {
			printf("stopped at frame %u\n", frame);
			break;
		}
		if (nwaits && next_wait == nwaits && frame >= waits[nwaits - 1].hit + waits[nwaits - 1].last &&
		    frame >= last_plain) {
			printf("stopped at frame %u\n", frame);
			break;
		}
		if (next_wait < nwaits && frame >= wait_start() + WAIT_FRAMES) {
			++frame;  // as if the loop ran out
			break;
		}
	}
	if (nwaits && next_wait < nwaits && !crashed && !until_hit) {
		fprintf(stderr, "timed out at frame %u waiting for: %s\n", frame - 1, waits[next_wait].text);
		status = status ? status : 4;
	}

	if (saveOut) {
		void *save = NULL;
		size_t size = core->savedataClone(core, &save);
		FILE *f = fopen(saveOut, "wb");
		if (!f || fwrite(save, 1, size, f) != size) {
			fprintf(stderr, "cannot write %s\n", saveOut);
			status = 1;
		}
		if (f) {
			fclose(f);
		}
		free(save);
	}

#ifdef USE_GDB_STUB
	if (debugger) {
		GDBStubShutdown((struct GDBStub *)debugger);
	}
#endif
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
	free(buffer);
	return status;
}
