#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "bmpalloc.h"

#if   BMPDT_SIZE == 1
	#define BMPDT_BITS 8
	#define _L         3
#elif BMPDT_SIZE == 4
	#define _L         5 
	#define BMPDT_BITS 32
#elif BMPDT_SIZE == 8
	#define _L         6
	#define BMPDT_BITS 64
#endif

#define mem_calc(size, l) \
	(((size >> l) << l) - size ? (size >> l) + 1 : size >> l)

#define setbit(p, off, b) \
	if (b)                                                   \
		*((byte*)p + off / 8) |= (1 << (7 - (off % 8))); \
	else                                                     \
		*((byte*)p + off / 8) &=~(1 << (7 - (off % 8)));

#define getbit(p, off) \
	(*((byte*)p + off / 8) & (1 << (7 - (off % 8))))

static void print_binary_char(byte c)
{
	size_t i, j;
	for (i = 7; i >= 0; --i) {
		j = c & (1 << i) ? 1 : 0;
		printf("%d", j);
	}
}

static void print_binary(void *d, int len)
{
	size_t i;
	for (i = 0; i < len; ++i) {
		print_binary_char(*((byte*)d + i));
	}
}

static size_t bitcount(void *p, size_t len)
{
	size_t i, j, s = 0;
	byte c[8];
	byte *b = p;
	for (i = 0; i < 8; ++i) {
		c[i] = 1 << i;
	}
	for (i = 0; i < len; ++i) {
		for (j = 0; j < 8; ++j) {
			if (*b & c[j])
				s++;
		}
		b++;
	}
	return s;
}

static void membitset(void *d, int b, size_t off, size_t nbit)
{
	size_t i, j, l, cnt, poff = 0;
	byte *c;
	if (off) {
		l = off / 8;
		c = (byte*)d + l;
		j = off % 8;
		if (j) {
			cnt = j + nbit;
			for (i = j; i < cnt; ++i) {
				if (i >= 8)
					break;
				if (b)
					*c |= (1 << (7 - i));
				else
					*c &=~(1 << (7 - i));
				nbit--;
			}
		}
		poff = l + (j ? 1 : 0);
	}
	if (!nbit)
		return;
	l = nbit / 8;
	if (b)
		memset((byte*)d + poff, 0xff, l);
	else
		memset((byte*)d + poff, 0x00, l);
	c = (byte*)d + poff + l;
	l = nbit % 8;
	for (i = 0; i < l; ++i) {
		if (b)
			*c |= (1 << (7 - i));
		else
			*c &=~(1 << (7 - i));
	}
}

struct bmp_state *new_bmp_state(size_t size)
{
	size_t bmplen = sizeof(bmpdt) * mem_calc(size, _L);
	struct bmp_state *b;
	assert(b = (struct bmp_state*)calloc(1, sizeof(struct bmp_state)));
	assert(b->bmp = (bmpdt*)malloc(bmplen));
	memset(b->bmp, 0xff, bmplen);
	membitset(b->bmp, 0, 0, size);
	b->size = size;
	return b;
}

void del_bmp_state(struct bmp_state *b)
{
	if (b) {
		free(b->bmp);
		free(b);
	}
}

void bmp_state_reset(struct bmp_state *b)
{
	b->used = 0;
	b->csr = 0;
	membitset(b->bmp, 0, 0, b->size);
}

bool bmp_state_resize(struct bmp_state *b, size_t size)
{
	size_t bmplen = sizeof(bmpdt) * mem_calc(size, _L);
	bmpdt *newbmp = (bmpdt*)realloc(b->bmp, bmplen);
	if (!newbmp)
		return false; /* failed realloc */
	if (size > b->size)
		membitset(newbmp, 0, b->size, size - b->size);
	membitset(newbmp, 1, size, bmplen * 8 - size);
	if (size < b->size) {
		b->used = size - (
			bmplen * 8 - bitcount(b->bmp, bmplen * 8));
	}
	b->size = size;
	b->bmp  = newbmp;
	return true;
}

bool bmp_state_set_cursor(struct bmp_state *b, size_t csr)
{
	if (csr < b->size) {
		b->csr = csr;
		return true;
	}
	return false;
}

size_t bmp_state_get_available(struct bmp_state *b)
{
	bmpdt c;
	size_t i, lim = mem_calc(b->size, _L);
	if (b->used >= b->size)
		return BMPALLOC_FAILED;
	for (i = 0; i < lim; ++i) {
		c = *(b->bmp + b->csr);
		if (c != (bmpdt)-1)
			break;
		b->csr++;
		if (b->csr >= lim)
			b->csr = 0;
	}
	for (i = 0; i < BMPDT_BITS; ++i) {
		if (!getbit(&c, i))
			break;
	}
	return (b->csr << _L) + i;
}

bool bmp_state_test(struct bmp_state *b, size_t id)
{
	if (id >= b->size)
		return false;
	if (getbit(b->bmp, id))
		return true;
	else
		return false;
}


bool bmp_state_alloc(struct bmp_state *b, size_t id)
{
	if (getbit(b->bmp, id))
		return false;
	b->used++;
	setbit(b->bmp, id, 1);
	return true;
}

bool bmp_state_free(struct bmp_state *b, size_t id)
{
	if (getbit(b->bmp, id)) {
		setbit(b->bmp, id, 0);
		b->used--;
		return true;
	}
	return false;
}
