#include "lz77.h"

// Format: a word header (0x10 | size << 8), then blocks of a flag byte and
// eight items, most significant flag bit first. A set bit is a reference:
// two bytes, (length - 3) << 12 | (distance - 1), high byte first; a clear
// bit a literal byte.
u32 lz77_compress(const u8 *in, u32 n, u8 *out)
{
	u32 o = 0;
	out[o++] = 0x10;
	out[o++] = n;
	out[o++] = n >> 8;
	out[o++] = n >> 16;

	u32 i = 0;
	while (i < n) {
		u32 flag_at = o++;
		u8 flags = 0;
		for (int bit = 7; bit >= 0 && i < n; bit--) {
			u32 best_len = 0, best_dist = 0;
			u32 max_len = (n - i < 18) ? n - i : 18;
			// The BIOS copies a byte at a time, so a match may overlap
			// the bytes it makes. Distance 1 is left out: the VRAM
			// version can't use it, so tools avoid it.
			for (u32 dist = 2; dist <= LZ77_WINDOW && dist <= i; dist++) {
				u32 len = 0;
				while (len < max_len && in[i - dist + len] == in[i + len])
					len++;
				if (len > best_len) {
					best_len = len;
					best_dist = dist;
					if (len == max_len)
						break;
				}
			}
			if (best_len >= 3) {
				u32 v = (best_len - 3) << 12 | (best_dist - 1);
				out[o++] = v >> 8;
				out[o++] = v;
				flags |= 1 << bit;
				i += best_len;
			} else {
				out[o++] = in[i++];
			}
		}
		out[flag_at] = flags;
	}
	while (o & 3)
		out[o++] = 0;
	return o;
}

// From the cart, not IWRAM: there it took a frame less a 32 KB chunk (of
// about 4), not worth its 1.1 KB of IWRAM
u32 lz77_compress_fast(const u8 *in, u32 n, u8 *out, u16 *head)
{
	u32 o = 0;
	out[o++] = 0x10;
	out[o++] = n;
	out[o++] = n >> 8;
	out[o++] = n >> 16;
	for (u32 h = 0; h < LZ77_HASH; h++)
		head[h] = 0;  // a place + 1; 0: none yet

	u32 i = 0;
	while (i < n) {
		u32 flag_at = o++;
		u8 flags = 0;
		for (int bit = 7; bit >= 0 && i < n; bit--) {
			u32 max_len = (n - i < 18) ? n - i : 18;
			u32 best_len = 0, best_dist = 0;
			if (n - i >= 3) {
				u32 h = ((in[i] | in[i + 1] << 8 | (u32)in[i + 2] << 16) * 2654435761u) >> 20;
				u32 at = head[h];
				head[h] = (u16)(i + 1);
				// The BIOS copies a byte at a time, so a match may overlap
				// the bytes it makes; distance 1 is left out (the VRAM
				// version can't use it)
				if (at && i + 1 - at >= 2 && i + 1 - at <= 4096) {
					best_dist = i + 1 - at;
					while (best_len < max_len && in[i - best_dist + best_len] == in[i + best_len])
						best_len++;
				}
				if (best_len < max_len && i >= 2) {
					u32 len = 0;
					while (len < max_len && in[i - 2 + len] == in[i + len])
						len++;
					if (len > best_len) {
						best_len = len;
						best_dist = 2;
					}
				}
			}
			if (best_len >= 3) {
				u32 v = (best_len - 3) << 12 | (best_dist - 1);
				out[o++] = v >> 8;
				out[o++] = v;
				flags |= 1 << bit;
				i += best_len;
			} else {
				out[o++] = in[i++];
			}
		}
		out[flag_at] = flags;
	}
	while (o & 3)
		out[o++] = 0;
	return o;
}
