// imagew-util.c
// Part of ImageWorsener, Copyright (c) 2011 by Jason Summers.
// For more information, see the readme.txt file.

// This file is mainly for portability wrappers, and any code that
// may require unusual header files (malloc.h, strsafe.h).

#include "imagew-config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef IW_WINDOWS
#include <malloc.h>
#endif
#include <stdarg.h>
#include <time.h>

#include "imagew-internals.h"
#ifdef IW_WINDOWS
#include <strsafe.h>
#endif


void iw_free(void *mem)
{
	if(mem) free(mem);
}

void *iw_malloc_lowlevel(size_t n)
{
	return malloc(n);
}

void *iw_realloc_lowlevel(void *m, size_t n)
{
	return realloc(m,n);
}

void *iw_strdup(const char *s)
{
#ifdef IW_WINDOWS
	return _strdup(s);
#else
	return strdup(s);
#endif
}

void *iw_malloc(struct iw_context *ctx, size_t n)
{
	void *mem;

	if(n>ctx->max_malloc) {
		iwpvt_err(ctx,iws_nomem);
		return NULL;
	}
	mem = iw_malloc_lowlevel(n);
	if(!mem) {
		iwpvt_err(ctx,iws_nomem);
		return NULL;
	}
	return mem;
}

void *iw_realloc(struct iw_context *ctx, void *m, size_t n)
{
	void *mem;

	if(n>ctx->max_malloc) {
		iwpvt_err(ctx,iws_nomem);
		return NULL;
	}
	mem = iw_realloc_lowlevel(m,n);
	if(!mem) {
		iwpvt_err(ctx,iws_nomem);
		return NULL;
	}
	return mem;
}

// Allocate a large block of memory, presumably for image data.
// Use this if integer overflow is a possibility when multiplying
// two factors together.
void *iw_malloc_large(struct iw_context *ctx, size_t n1, size_t n2)
{
	if(n1 > ctx->max_malloc/n2) {
		iwpvt_err(ctx,iws_image_too_large);
		return NULL;
	}
	return iw_malloc(ctx,n1*n2);
}

void iw_strlcpy(char *dst, const char *src, size_t dstlen)
{
	size_t n;
	n = strlen(src);
	if(n>dstlen-1) n=dstlen-1;
	memcpy(dst,src,n);
	dst[n]='\0';
}

void iw_vsnprintf(char *buf, size_t buflen, const char *fmt, va_list ap)
{
#ifdef IW_WINDOWS
	StringCchVPrintfA(buf,buflen,fmt,ap);
#else
	vsnprintf(buf,buflen,fmt,ap);
	buf[buflen-1]='\0';
#endif
}

void iw_snprintf(char *buf, size_t buflen, const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	iw_vsnprintf(buf,buflen,fmt,ap);
	va_end(ap);
}

////////////////////////////////////////////
// A simple carry-with-multiply PRNG.

struct iw_prng {
	iw_uint32 multiply;
	iw_uint32 carry;
};

struct iw_prng *iwpvt_prng_create(void)
{
	struct iw_prng *prng;
	prng = (struct iw_prng*)iw_malloc_lowlevel(sizeof(struct iw_prng));
	if(!prng) return NULL;
	memset(prng,0,sizeof(struct iw_prng));
	return prng;
}

void iwpvt_prng_destroy(struct iw_prng *prng)
{
	if(prng) iw_free((void*)prng);
}

void iwpvt_prng_set_random_seed(struct iw_prng *prng, int s)
{
	prng->multiply = ((iw_uint32)0x03333333) + s;
	prng->carry    = ((iw_uint32)0x05555555) + s;
}

iw_uint32 iwpvt_prng_rand(struct iw_prng *prng)
{
	iw_uint64 x;
	x = ((iw_uint64)0xfff0bf23) * prng->multiply + prng->carry;
	prng->carry = (iw_uint32)(x>>32);
	prng->multiply = 0xffffffff - (0xffffffff & x);
	return prng->multiply;
}

////////////////////////////////////////////

int iwpvt_util_randomize(struct iw_prng *prng)
{
	int s;
	s = (int)time(NULL);
	//srand((unsigned int)time(NULL));
	iwpvt_prng_set_random_seed(prng, s);
	return s;
}


int iw_file_to_memory(struct iw_context *ctx, struct iw_iodescr *iodescr,
  void **pmem, size_t *psize)
{
	int ret;
	size_t bytesread;

	*pmem=NULL;
	*psize=0;

	if(!iodescr->getfilesize_fn) return 0;

	ret = (*iodescr->getfilesize_fn)(ctx,iodescr,psize);
	if(!ret) return 0;

	*pmem = iw_malloc(ctx,*psize);

	ret = (*iodescr->read_fn)(ctx,iodescr,*pmem,*psize,&bytesread);
	if(!ret) return 0;
	if(bytesread != *psize) return 0;
	return 1;
}

struct iw_utf8cvt_struct {
	char *dst;
	int dstlen;
	int dp;
};

static void utf8cvt_emitoctet(struct iw_utf8cvt_struct *s, unsigned char c)
{
	if(s->dp > s->dstlen-2) return;
	s->dst[s->dp] = (char)c;
	s->dp++;
}

// Map Unicode characters to ASCII substitutions.
// Not used for codepoints <=127.
static void utf8cvt_emitunichar(struct iw_utf8cvt_struct *s, unsigned int c)
{
	int i;
	int pos;
	struct charmap_struct {
		unsigned int code;
		const char *s;
	};
	static const struct charmap_struct chartable[] = {
	 {0, "?" }, // Default character
	 {0xa9, "(c)" },
	 {0xd7, "x" },
	 {0x2192, "->" },
	 {0x2018, "'" },
	 {0x2019, "'" },
	 {0x201c, "\"" },
	 {0x201d, "\"" },
	 {0, NULL}
	};

	// Try to find the codepoint in the table.
	pos = 0;
	for(i=1;chartable[i].code;i++) {
		if(c==chartable[i].code) {
			pos=i;
			break;
		}
	}

	// Write out the ASCII translation of this code.
	for(i=0;chartable[pos].s[i];i++) {
		utf8cvt_emitoctet(s,(unsigned char)chartable[pos].s[i]);
	}
}

// This UTF-8 converter is intended for use with the UTF-8 strings that are
// hardcoded into this program. It won't work very well with
// user-controlled strings.
void iw_utf8_to_ascii(const char *src, char *dst, int dstlen)
{
	struct iw_utf8cvt_struct s;
	int sp;
	unsigned char c;
	unsigned int pending_char;
	int bytes_expected;

	s.dst = dst;
	s.dstlen = dstlen;
	s.dp = 0;
	pending_char=0;
	bytes_expected=0;

	for(sp=0;src[sp];sp++) {
		c = (unsigned char)src[sp];
		if(c<128) { // Only byte of a 1-byte sequence
			utf8cvt_emitoctet(&s,c);
		}
		else if(c<0xc0) { // Continuation byte
			pending_char = (pending_char<<6)|(c&0x3f);
			bytes_expected--;
			if(bytes_expected<1) {
				utf8cvt_emitunichar(&s,pending_char);
			}
		}
		else if(c<0xe0) { // 1st byte of a 2-byte sequence
			pending_char = c&0x1f;
			bytes_expected=1;
		}
		else if(c<0xf0) { // 1st byte of a 3-byte sequence
			pending_char = c&0x0f;
			bytes_expected=2;
		}
		else if(c<0xf8) { // 1st byte of a 4-byte sequence
			pending_char = c&0x07;
			bytes_expected=3;
		}
	}
	dst[s.dp] = '\0';
}

// Returns 0 if running on a big-endian system, 1 for little-endian.
int iw_get_host_endianness(void)
{
	union en_union {
		unsigned char c[4];
		int ii;
	} en;

	// Test the host's endianness.
	en.c[0]=0;
	en.ii = 1;
	if(en.c[0]!=0) {
		return 1;
	}
	return 0;
}

