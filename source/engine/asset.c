// See asset.h.
#include "asset.h"

int asset_check(const struct asset_header *h, u32 file_bytes)
{
	if (file_bytes < ASSET_HEADER_BYTES)
		return ASSET_ERR_SHORT;
	if (h->magic != ASSET_MAGIC)
		return ASSET_ERR_MAGIC;
	if ((h->bpp != 4 && h->bpp != 8) || !h->width || !h->height || h->width > ASSET_MAX_TILES_PER_SIDE ||
	    h->height > ASSET_MAX_TILES_PER_SIDE || h->flags || h->reserved || h->colours > (h->bpp == 4 ? 16 : 256) ||
	    h->tile_bytes != asset_tile_bytes(h))
		return ASSET_ERR_FORMAT;
	if (asset_tile_bytes(h) > ASSET_TILE_BYTES_MAX || file_bytes > ASSET_FILE_MAX)
		return ASSET_ERR_TOO_BIG;
	if (file_bytes < asset_tiles_at(h) + h->tile_bytes)
		return ASSET_ERR_SHORT;
	return ASSET_OK;
}

const char *asset_error_text(int error)
{
	switch (error) {
	case ASSET_OK:
		return "fine";
	case ASSET_ERR_SHORT:
		return "the file is shorter than its header says";
	case ASSET_ERR_MAGIC:
		return "not an asset (no TAS2 magic)";
	case ASSET_ERR_FORMAT:
		return "bpp, size, flags or colours out of range";
	case ASSET_ERR_TOO_BIG:
		return "too big";
	default:
		return "unknown error";
	}
}
