// Headless mGBA runner: boot a ROM, press keys on a schedule, save screenshots.
//
// usage: mgba-shot ROM [-f FRAMES] [-k FRAME:KEYS]... [-s FRAME:OUT.png]...
//   -f FRAMES        total frames to run (default: last scheduled event)
//   -k FRAME:KEYS    from FRAME on, hold KEYS (A+B+START, UP, ... or - for none)
//   -s FRAME:PATH    write a PNG of the screen after FRAME
//   -S PATH          write the emulated save chip's contents when done
//   -i FILE          read more options from FILE, whitespace-separated
//                    (# starts a comment), for long input scripts
//   -g PORT          wait for gdb on PORT (all addresses) before running
//   -y SYMFILE       symbols for crash reports (default: the ROM's .sym)
//
// If the CPU hits an illegal opcode or jumps to an invalid address, it prints
// a crash report (registers, and code addresses found on the stack with their
// symbols) and exits with status 3.
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <mgba/core/log.h>
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

struct event {
	unsigned frame;
	char* arg;
};

static const char* KEY_NAMES[] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};

// --- Crash reports ---------------------------------------------------------

struct symbol {
	uint32_t address;
	char* name;
};

static struct symbol* symbols;
static size_t nsymbols;

// Reads an armips symbol file (ADDRESS NAME per line, sorted by address).
static void load_symbols(const char* path) {
	FILE* f = fopen(path, "r");
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
static const char* symbolize(uint32_t address) {
	static char out[512];
	const struct symbol* best = NULL;
	for (size_t i = 0; i < nsymbols && symbols[i].address <= address; ++i) {
		best = &symbols[i];
	}
	if (!best || address - best->address > 0x10000) {
		return "";
	}
	snprintf(out, sizeof(out), " (%s+0x%X)", best->name, address - best->address);
	return out;
}

static struct mCore* crash_core;
static int crashed;
static char crash_reason[256];
static uint32_t crash_regs[16], crash_cpsr;

static void note_crash(const char* format, va_list args) {
	if (crashed || !crash_core) {
		return;
	}
	crashed = 1;
	vsnprintf(crash_reason, sizeof(crash_reason), format, args);
	struct ARMCore* cpu = crash_core->cpu;
	memcpy(crash_regs, cpu->gprs, sizeof(crash_regs));
	crash_cpsr = cpu->cpsr.packed;
}

static int is_code(uint32_t address) {
	address &= ~1u;
	return (address >= 0x08000000 && address < 0x0A000000) || (address >= 0x03000000 && address < 0x03008000) ||
	       (address >= 0x02000000 && address < 0x02040000);
}

static void print_crash(unsigned frame) {
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
	for (uint32_t sp = crash_regs[ARM_SP] & ~3u; sp < 0x03008000 && sp < (crash_regs[ARM_SP] & ~3u) + 0x400 && shown < 16;
	     sp += 4) {
		uint32_t word = crash_core->busRead32(crash_core, sp);
		if (is_code(word) && (word & 1)) {  // Thumb return addresses
			fprintf(stderr, "    [%08X] %08X%s\n", sp, word, symbolize(word & ~1u));
			++shown;
		}
	}
}

// Prints errors, and every line the kernel logs with tlog (category
// gba.debug) as "kernel: ..." or "kernel error: ...".
static void quiet_log(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args) {
	(void) logger;
	const char* id = mLogCategoryId(category);
	if ((!strcmp(id, "gba.memory") && (level & (mLOG_GAME_ERROR | mLOG_FATAL)) && strstr(format, "invalid address")) ||
	    (!strcmp(id, "gba") && !strncmp(format, "Illegal opcode", 14))) {
		note_crash(format, args);
		return;  // The crash report has it, and it repeats until the frame ends.
	}
	if (!strcmp(id, "gba.debug")) {
		fputs(level & (mLOG_FATAL | mLOG_ERROR) ? "kernel error: " : "kernel: ", stderr);
		vfprintf(stderr, format, args);
		fputc('\n', stderr);
		return;
	}
	if (level & (mLOG_FATAL | mLOG_ERROR)) {
		vfprintf(stderr, format, args);
		fputc('\n', stderr);
	}
}

static uint32_t parse_keys(const char* spec) {
	uint32_t keys = 0;
	if (!strcmp(spec, "-")) {
		return 0;
	}
	char* copy = strdup(spec);
	for (char* tok = strtok(copy, "+"); tok; tok = strtok(NULL, "+")) {
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

static struct event parse_event(const char* spec) {
	struct event ev;
	char* colon = strchr(spec, ':');
	if (!colon) {
		fprintf(stderr, "expected FRAME:VALUE, got %s\n", spec);
		exit(2);
	}
	ev.frame = (unsigned) strtoul(spec, NULL, 10);
	ev.arg = colon + 1;
	return ev;
}

struct events {
	struct event* list;
	size_t count;
	size_t cap;
};

static void add_event(struct events* events, const char* spec) {
	if (events->count == events->cap) {
		events->cap = events->cap ? events->cap * 2 : 64;
		events->list = realloc(events->list, events->cap * sizeof(*events->list));
	}
	events->list[events->count++] = parse_event(spec);
}

struct options {
	struct events keys;
	struct events shots;
	unsigned frames;
	const char* saveOut;
	unsigned gdbPort;
	const char* symFile;
};

static void parse_options(struct options* opts, int argc, char** argv);

static void parse_file(struct options* opts, const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "cannot read %s\n", path);
		exit(2);
	}
	size_t cap = 64, argc = 0;
	char** argv = malloc(cap * sizeof(*argv));
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		char* hash = strchr(line, '#');
		if (hash) {
			*hash = '\0';
		}
		for (char* tok = strtok(line, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
			if (argc == cap) {
				cap *= 2;
				argv = realloc(argv, cap * sizeof(*argv));
			}
			argv[argc++] = strdup(tok);
		}
	}
	fclose(f);
	parse_options(opts, (int) argc, argv);
}

static void parse_options(struct options* opts, int argc, char** argv) {
	for (int i = 0; i < argc; ++i) {
		if (i + 1 >= argc) {
			fprintf(stderr, "missing value for %s\n", argv[i]);
			exit(2);
		}
		if (!strcmp(argv[i], "-f")) {
			opts->frames = (unsigned) strtoul(argv[++i], NULL, 10);
		} else if (!strcmp(argv[i], "-k")) {
			add_event(&opts->keys, argv[++i]);
		} else if (!strcmp(argv[i], "-S")) {
			opts->saveOut = argv[++i];
		} else if (!strcmp(argv[i], "-s")) {
			add_event(&opts->shots, argv[++i]);
		} else if (!strcmp(argv[i], "-y")) {
			opts->symFile = argv[++i];
		} else if (!strcmp(argv[i], "-g")) {
			opts->gdbPort = (unsigned) strtoul(argv[++i], NULL, 10);
		} else if (!strcmp(argv[i], "-i")) {
			parse_file(opts, argv[++i]);
		} else {
			fprintf(stderr, "unknown option: %s\n", argv[i]);
			exit(2);
		}
	}
}

static int save_png(const char* path, const color_t* pixels, unsigned width, unsigned height) {
	struct VFile* vf = VFileOpen(path, O_CREAT | O_TRUNC | O_WRONLY);
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

int main(int argc, char** argv) {
	if (argc < 2) {
		fprintf(stderr, "usage: %s ROM [-f FRAMES] [-k FRAME:KEYS]... [-s FRAME:OUT.png]... [-S SAVE] [-i FILE] [-g PORT]\n", argv[0]);
		return 2;
	}
	struct options opts = {0};
	parse_options(&opts, argc - 2, argv + 2);
	struct event* keys = opts.keys.list;
	struct event* shots = opts.shots.list;
	size_t nkeys = opts.keys.count, nshots = opts.shots.count;
	unsigned frames = opts.frames;
	const char* saveOut = opts.saveOut;
	for (size_t i = 0; i < nkeys; ++i) {
		frames = keys[i].frame > frames ? keys[i].frame : frames;
	}
	for (size_t i = 0; i < nshots; ++i) {
		frames = shots[i].frame > frames ? shots[i].frame : frames;
	}

	static struct mLogger logger = {.log = quiet_log};
	mLogSetDefaultLogger(&logger);

	struct mCore* core = mCoreFind(argv[1]);
	if (!core || !core->init(core)) {
		fprintf(stderr, "cannot create a core for %s\n", argv[1]);
		return 1;
	}
	mCoreInitConfig(core, NULL);
	unsigned width, height;
	core->desiredVideoDimensions(core, &width, &height);
	color_t* buffer = calloc((size_t) width * height, BYTES_PER_PIXEL);
	core->setVideoBuffer(core, buffer, width);
	if (!mCoreLoadFile(core, argv[1])) {
		fprintf(stderr, "cannot load %s\n", argv[1]);
		return 1;
	}
	core->reset(core);
	crash_core = core;
	if (opts.symFile) {
		load_symbols(opts.symFile);
	} else {
		char path[1024];
		snprintf(path, sizeof(path), "%s", argv[1]);
		char* dot = strrchr(path, '.');
		if (dot && dot > strrchr(path, '/')) {
			strcpy(dot, ".sym");
			load_symbols(path);
		}
	}

	struct mDebugger* debugger = NULL;
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
	for (unsigned frame = 1; frame <= frames; ++frame) {
		for (size_t i = 0; i < nkeys; ++i) {
			if (keys[i].frame == frame) {
				core->setKeys(core, parse_keys(keys[i].arg));
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
		if (crashed) {
			print_crash(frame);
			status = 3;
			break;
		}
		for (size_t i = 0; i < nshots; ++i) {
			if (shots[i].frame == frame) {
				if (save_png(shots[i].arg, buffer, width, height)) {
					printf("frame %u -> %s\n", frame, shots[i].arg);
				} else {
					fprintf(stderr, "cannot write %s\n", shots[i].arg);
					status = 1;
				}
			}
		}
	}

	if (saveOut) {
		void* save = NULL;
		size_t size = core->savedataClone(core, &save);
		FILE* f = fopen(saveOut, "wb");
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
		GDBStubShutdown((struct GDBStub*) debugger);
	}
#endif
	mCoreConfigDeinit(&core->config);
	core->deinit(core);
	free(buffer);
	return status;
}
