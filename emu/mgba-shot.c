// Headless mGBA runner: boot a ROM, press keys on a schedule, save screenshots.
//
// usage: mgba-shot ROM [-f FRAMES] [-k FRAME:KEYS]... [-s FRAME:OUT.png]...
//   -f FRAMES        total frames to run (default: last scheduled event)
//   -k FRAME:KEYS    from FRAME on, hold KEYS (A+B+START, UP, ... or - for none)
//   -s FRAME:PATH    write a PNG of the screen after FRAME
//   -S PATH          write the emulated save chip's contents when done
#include <mgba/core/core.h>
#include <mgba/core/log.h>
#include <mgba-util/png-io.h>
#include <mgba-util/vfs.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct event {
	unsigned frame;
	char* arg;
};

static const char* KEY_NAMES[] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};

static void quiet_log(struct mLogger* logger, int category, enum mLogLevel level, const char* format, va_list args) {
	(void) logger;
	(void) category;
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
		fprintf(stderr, "usage: %s ROM [-f FRAMES] [-k FRAME:KEYS]... [-s FRAME:OUT.png]...\n", argv[0]);
		return 2;
	}
	struct event keys[64], shots[64];
	size_t nkeys = 0, nshots = 0;
	unsigned frames = 0;
	const char* saveOut = NULL;
	for (int i = 2; i < argc; ++i) {
		if (i + 1 >= argc) {
			fprintf(stderr, "missing value for %s\n", argv[i]);
			return 2;
		}
		if (!strcmp(argv[i], "-f")) {
			frames = (unsigned) strtoul(argv[++i], NULL, 10);
		} else if (!strcmp(argv[i], "-k") && nkeys < 64) {
			keys[nkeys++] = parse_event(argv[++i]);
		} else if (!strcmp(argv[i], "-S")) {
			saveOut = argv[++i];
		} else if (!strcmp(argv[i], "-s") && nshots < 64) {
			shots[nshots++] = parse_event(argv[++i]);
		} else {
			fprintf(stderr, "unknown option: %s\n", argv[i]);
			return 2;
		}
	}
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

	int status = 0;
	for (unsigned frame = 1; frame <= frames; ++frame) {
		for (size_t i = 0; i < nkeys; ++i) {
			if (keys[i].frame == frame) {
				core->setKeys(core, parse_keys(keys[i].arg));
			}
		}
		core->runFrame(core);
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

	mCoreConfigDeinit(&core->config);
	core->deinit(core);
	free(buffer);
	return status;
}
