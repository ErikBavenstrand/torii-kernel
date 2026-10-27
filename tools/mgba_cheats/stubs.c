// What mGBA's GBA cheat decoders (src/core/cheats.c, src/gba/cheats*.c) call
// outside themselves, for the host build tools/mgba_cheats.sh makes: logging
// is dropped, and the hook's breakpoints and the memory map are never used
// since nothing here runs a cheat.
#include <mgba/core/core.h>
#include <mgba/core/directories.h>
#include <mgba/core/log.h>
#include <mgba/internal/gba/gba.h>

void mLog(int category, enum mLogLevel level, const char *format, ...)
{
	(void)category;
	(void)level;
	(void)format;
}

int mLogGenerateCategory(const char *name, const char *id)
{
	(void)name;
	(void)id;
	return 0;
}

const struct mCoreMemoryBlock *mCoreGetMemoryBlockInfo(struct mCore *core, uint32_t address)
{
	(void)core;
	(void)address;
	return NULL;
}

void GBASetBreakpoint(struct GBA *gba, struct mCPUComponent *component, uint32_t address, enum ExecutionMode mode,
		      uint32_t *opcode)
{
	(void)gba;
	(void)component;
	(void)address;
	(void)mode;
	(void)opcode;
}

void GBAClearBreakpoint(struct GBA *gba, uint32_t address, enum ExecutionMode mode, uint32_t opcode)
{
	(void)gba;
	(void)address;
	(void)mode;
	(void)opcode;
}

void mLogSetDefaultLogger(struct mLogger *logger)
{
	(void)logger;
}

struct VFile *mDirectorySetOpenSuffix(struct mDirectorySet *dirs, struct VDir *dir, const char *suffix, int mode)
{
	(void)dirs;
	(void)dir;
	(void)suffix;
	(void)mode;
	return NULL;
}
