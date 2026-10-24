// The FAT's first free cluster, found ahead of time (fathint.h).
#include <string.h>

#include "fathint.h"
#include "tlog.h"
#include "engine/jobs.h"
#include "engine/stage.h"
#include "ff15/ff.h"
#include "ff15/diskio.h"

extern FATFS EZcardFs;

#define STEP_SECTORS 8  // a step's read: 1,024 clusters of FAT32
_Static_assert(STEP_SECTORS * 512 <= STAGE_BYTES, "a step's sectors fit the stage");

static struct {
	u8 job;
	u32 sector;  // the next FAT sector to read, from the FAT's start
	u32 from;    // the first cluster it looks at: after a hint FatFs has
	u32 frames;  // torii_frames at the start
} fh = {JOB_NONE, 0, 0, 0};

// 1 if the hint is in range (the job then checks the clusters after it)
static int hint_set(const FATFS *fs)
{
	return fs->last_clst >= 2 && fs->last_clst < fs->n_fatent;
}

static int step(struct job *job)
{
	FATFS *fs = &EZcardFs;
	if (job->state == JOB_CANCEL || fs->fs_type != FS_FAT32)
		return JOB_DONE;
	u32 sectors = fs->fsize - fh.sector;
	if (!sectors) {
		tlog("fat: no free cluster found");
		return JOB_DONE;
	}
	sectors = sectors < STEP_SECTORS ? sectors : STEP_SECTORS;
	u32 *fat = (u32 *)stage_buffer();
	if (disk_read(fs->pdrv, (BYTE *)fat, fs->fatbase + fh.sector, sectors) != RES_OK) {
		tlog_error("fat: reading FAT sector %lu failed", (unsigned long)fh.sector);
		return JOB_DONE;
	}
	u32 first = fh.sector * 128u;  // the cluster of the first entry read
	for (u32 i = 0; i < sectors * 128u; i++) {
		u32 c = first + i;
		if (c < 2 || c < fh.from)
			continue;
		if (c >= fs->n_fatent)
			break;
		if ((fat[i] & 0x0FFFFFFFu) == 0) {
			fs->last_clst = c - 1;  // FatFs takes the next free from here
			tlog("fat: free cluster %lu from %lu, found in %lu frames (%lu FAT sectors read)",
			     (unsigned long)c, (unsigned long)fh.from, (unsigned long)(torii_frames - fh.frames),
			     (unsigned long)(fh.sector + sectors - fh.from / 128u));
			return JOB_DONE;
		}
	}
	fh.sector += sectors;
	return JOB_MORE;
}

void fathint_start(void)
{
	FATFS *fs = &EZcardFs;
	tlog_debug("fat: type %u, %lu clusters, next-free hint %lu", fs->fs_type, (unsigned long)fs->n_fatent,
		   (unsigned long)fs->last_clst);
	if (fs->fs_type != FS_FAT32 || job_running(fh.job))
		return;
	// From the hint (a stale one points into used clusters: FatFs would
	// read on from it), else from the start
	fh.from = hint_set(fs) ? fs->last_clst + 1 : 2;
	fh.sector = fh.from / 128u;
	fh.frames = torii_frames;
	fh.job = job_start(step, 0, JOB_OWNER_NONE);
}
