#include <stdint.h>
#include <string.h>
#include "blake3.h"

#define CHUNK_START (1u << 0)
#define CHUNK_END   (1u << 1)
#define PARENT      (1u << 2)
#define ROOT        (1u << 3)

static const uint32_t iv[] = {
	0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
	0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
};

static void
compress(uint32_t out[static 8], const uint32_t m[static 16], const uint32_t h[static 8], uint64_t t, uint32_t b, uint32_t d)
{
	static const unsigned char s[][16] = {
		{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15},
		{2, 6, 3, 10, 7, 0, 4, 13, 1, 11, 12, 5, 9, 14, 15, 8},
		{3, 4, 10, 12, 13, 2, 7, 14, 6, 5, 9, 0, 11, 15, 8, 1},
		{10, 7, 12, 9, 14, 3, 13, 15, 4, 0, 11, 2, 5, 8, 1, 6},
		{12, 13, 9, 11, 15, 10, 14, 8, 7, 2, 5, 3, 0, 1, 6, 4},
		{9, 14, 11, 5, 8, 12, 15, 1, 13, 3, 0, 10, 2, 6, 4, 7},
		{11, 15, 5, 0, 1, 9, 8, 6, 14, 10, 2, 12, 3, 4, 7, 13},
	};
	uint32_t v[16] = {
		h[0], h[1], h[2], h[3],
		h[4], h[5], h[6], h[7],
		iv[0], iv[1], iv[2], iv[3],
		t, t >> 32, b, d,
	};
	unsigned i;

	for (i = 0; i < 7; ++i) {
#define G(j, a, b, c, d, s) \
		a = a + b + m[s[j * 2]]; \
		d = d ^ a; \
		d = d >> 16 | d << 16; \
		c = c + d; \
		b = b ^ c; \
		b = b >> 12 | b << 20; \
		a = a + b + m[s[j * 2 + 1]]; \
		d = d ^ a; \
		d = d >> 8 | d << 24; \
		c = c + d; \
		b = b ^ c; \
		b = b >> 7 | b << 25;

		G(0, v[0], v[4], v[8],  v[12], s[i])
		G(1, v[1], v[5], v[9],  v[13], s[i])
		G(2, v[2], v[6], v[10], v[14], s[i])
		G(3, v[3], v[7], v[11], v[15], s[i])
		G(4, v[0], v[5], v[10], v[15], s[i])
		G(5, v[1], v[6], v[11], v[12], s[i])
		G(6, v[2], v[7], v[8],  v[13], s[i])
		G(7, v[3], v[4], v[9],  v[14], s[i])
#undef G
	}
	for (i = 0; i < 8; ++i)
		out[i] = v[i] ^ v[8 + i];
}

static void
load(uint32_t d[static 16], unsigned char s[static 64]) {
	uint32_t *end;

	for (end = d + 16; d < end; ++d, s += 4) {
		*d = (uint32_t)s[0]       | (uint32_t)s[1] <<  8
		   | (uint32_t)s[2] << 16 | (uint32_t)s[3] << 24;
	}
}

void
blake3_init(struct blake3 *ctx)
{
	ctx->bytes = 0;
	ctx->block = 0;
	ctx->chunk = 0;
	ctx->cv = ctx->cv_buf;
	memcpy(ctx->cv, iv, sizeof(iv));
}

void
blake3_update(struct blake3 *ctx, const void *buf, size_t len)
{
	const unsigned char *pos = buf;
	uint32_t m[16], flags, *h = ctx->cv;
	uint64_t t;

	while (len > 64 - ctx->bytes) {
		memcpy(ctx->input + ctx->bytes, pos, 64 - ctx->bytes);
		pos += 64 - ctx->bytes;
		len -= 64 - ctx->bytes;
		ctx->bytes = 0;
		flags = 0;
		switch (ctx->block) {
		case 0:  flags |= CHUNK_START; break;
		case 15: flags |= CHUNK_END;   break;
		}
		load(m, ctx->input);
		compress(h, m, h, ctx->chunk, 64, flags);
		if (++ctx->block == 16) {
			ctx->block = 0;
			for (t = ++ctx->chunk; (t & 1) == 0; t >>= 1) {
				h -= 8;
				compress(h, h, iv, 0, 64, PARENT);
			}
			h += 8;
			memcpy(h, iv, sizeof(iv));
		}
	}
	memcpy(ctx->input + ctx->bytes, pos, len);
	ctx->bytes += len;
	ctx->cv = h;
}

void
blake3_out(struct blake3 *ctx, unsigned char *restrict out)
{
	uint32_t flags, *cv, *cv_end, m[16];

	cv = ctx->cv;
	memset(ctx->input + ctx->bytes, 0, 64 - ctx->bytes);
	flags = CHUNK_END;
	if (ctx->block == 0)
		flags |= CHUNK_START;
	if (cv == ctx->cv_buf)
		flags |= ROOT;
	load(m, ctx->input);
	compress(cv, m, cv, ctx->chunk, ctx->bytes, flags);
	while (cv != ctx->cv_buf) {
		cv -= 8;
		flags = PARENT;
		if (cv == ctx->cv_buf)
			flags |= ROOT;
		compress(cv, cv, iv, 0, 64, flags);
	}
	for (cv_end = cv + 8; cv < cv_end; ++cv, out += 4) {
		out[0] = *cv & 0xff;
		out[1] = *cv >> 8 & 0xff;
		out[2] = *cv >> 16 & 0xff;
		out[3] = *cv >> 24 & 0xff;
	}
}