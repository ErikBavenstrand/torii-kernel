// See sdread.h.
#include "sdread.h"
#include "psram.h"
#include "res.h"
#include "stage.h"

static void set_result(struct sd_read *r, s8 result)
{
	if (r->result != SD_READ_BUSY)
		return;
	r->result = result;
	if (r->finish)
		r->finish(r);
}

static void close_file(struct sd_read *r)
{
	if (r->open) {
		f_close(&r->fil);
		r->open = 0;
	}
}

void sd_read_init(struct sd_read *r, const char *path, void *dst, u32 max, u16 chunk)
{
	r->path = path;
	r->dst = dst;
	r->max = max;
	r->size = r->done = 0;
	r->offset = r->length = 0;
	r->map = 0;
	r->chunk = chunk;
	r->open = r->seek_due = 0;
	r->result = SD_READ_BUSY;
	int psram = psram_holds(dst, max);
	if (!path || !dst || (max & 3) || ((u32)dst & 3) || chunk < SD_READ_CHUNK_MIN || chunk > SD_READ_CHUNK_MAX ||
	    chunk % 512 || (psram && chunk > STAGE_BYTES)) {
		res_set_error("SD read of %s: bad arguments (to %08lX, %lu bytes, chunk %u)", path ? path : "(none)",
			      (unsigned long)(u32)dst, (unsigned long)max, chunk);
		r->result = SD_READ_ERR_ARGS;
	}
}

void sd_read_range(struct sd_read *r, u32 offset, u32 length, u32 *map)
{
	r->offset = offset;
	r->length = length;
	r->map = map;
	// f_read hands whole sectors straight to the card's 16-bit DMA, at the
	// destination plus the bytes before the first sector's start: an offset
	// off a word would put them off a word too
	if ((offset & 3) && r->result == SD_READ_BUSY) {
		res_set_error("SD read: offset %lu isn't a multiple of 4", (unsigned long)offset);
		r->result = SD_READ_ERR_ARGS;
	}
}

void sd_read_cancel(struct sd_read *r)
{
	close_file(r);
	set_result(r, SD_READ_CANCELLED);
}

int sd_read_step(struct sd_read *r)
{
	if (r->result != SD_READ_BUSY)
		return JOB_DONE;
	if (!r->open) {
		// The first step: open
		FRESULT fr = f_open(&r->fil, r->path, FA_READ);
		const char *path = r->path;
		r->path = 0;
		if (fr != FR_OK) {
			res_set_error("SD read: can't open %s (FatFs error %d)", path, fr);
			set_result(r, SD_READ_ERR_OPEN);
			return JOB_DONE;
		}
		r->open = 1;
		r->size = f_size(&r->fil);
		if (r->length) {
			if (r->offset > r->size || r->length > r->size - r->offset) {
				res_set_error("SD read: %s is %lu bytes, short of %lu at %lu", path,
					      (unsigned long)r->size, (unsigned long)r->length,
					      (unsigned long)r->offset);
				close_file(r);
				set_result(r, SD_READ_ERR_READ);
				return JOB_DONE;
			}
			r->size = r->length;
			r->seek_due = r->offset != 0;
			r->fil.cltbl = (DWORD *)r->map;  // f_open nulled it
		}
		if (r->size > r->max) {
			res_set_error("SD read: %s is %lu bytes, more than the %lu it may take", path,
				      (unsigned long)r->size, (unsigned long)r->max);
			close_file(r);
			set_result(r, SD_READ_ERR_SIZE);
			return JOB_DONE;
		}
	} else if (r->seek_due) {
		// A step of its own: without a map, f_lseek walks the cluster chain
		r->seek_due = 0;
		FRESULT fr = f_lseek(&r->fil, r->offset);
		if (fr != FR_OK) {
			res_set_error("SD read: can't seek to %lu (FatFs error %d)", (unsigned long)r->offset, fr);
			close_file(r);
			set_result(r, SD_READ_ERR_READ);
			return JOB_DONE;
		}
		return JOB_MORE;
	} else {
		u32 n = r->size - r->done;
		if (n > r->chunk)
			n = r->chunk;
		int psram = psram_holds(r->dst, r->max);
		u8 *buf = psram ? stage_buffer() : r->dst + r->done;
		if (psram)
			psram_begin();
		UINT got = 0;
		FRESULT fr = f_read(&r->fil, buf, n, &got);
		if (fr != FR_OK || got != n || (psram && !psram_write(r->dst + r->done, buf, (n + 3) & ~3u))) {
			res_set_error("SD read: f_read gave %lu of %lu bytes at %lu (FatFs error %d)",
				      (unsigned long)got, (unsigned long)n, (unsigned long)r->done, fr);
			close_file(r);
			set_result(r, SD_READ_ERR_READ);
			return JOB_DONE;
		}
		r->done += n;
	}
	if (r->done < r->size)
		return JOB_MORE;
	close_file(r);
	set_result(r, SD_READ_OK);
	return JOB_DONE;
}

static int sd_read_job(struct job *job)
{
	struct sd_read *r = job->ctx;
	if (job->state == JOB_CANCEL) {
		sd_read_cancel(r);
		return JOB_DONE;
	}
	return sd_read_step(r);
}

u8 sd_read_start(struct sd_read *r, const char *path, void *dst, u32 max, u16 chunk, u8 owner)
{
	sd_read_init(r, path, dst, max, chunk);
	if (r->result != SD_READ_BUSY)
		return JOB_NONE;
	u8 id = job_start(sd_read_job, r, owner);
	if (id == JOB_NONE)
		set_result(r, SD_READ_ERR_ARGS);
	return id;
}
