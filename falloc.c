#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>

#include "bmpalloc.h"
#include "falloc.h"

static off_t _fsize(int fd)
{
	off_t curr, size;
	curr = lseek(fd, 0, SEEK_CUR);
	size = lseek(fd, 0, SEEK_END);
	lseek(fd, curr, SEEK_SET);
	return size;
}

static inline
size_t _log2size_t(size_t v)
{
	size_t r, t;
	for (r = (FALLOC_SIZE_T_BIT - 1); r >= 0; --r) {
		t = ((size_t)1) << r;
		if (v & t) {
			v ^= t;
			if (v)
				r++;
			break;
		}
	}
	return r;
}

static void _chk_magic(struct falloc_allocator *allocator)
{
	int mlen = strlen(FALLOC_MAGIC);
	unsigned char buffer[sizeof(FALLOC_MAGIC)];
	if (allocator->filelen == 0) { /* Create */
		write(allocator->fd, FALLOC_MAGIC, mlen);
		allocator->err = FALLOC_OK;
		return;
	} else if (allocator->filelen < mlen) {
		allocator->err =  FALLOC_INCOMPATIBLE_FILE;
	} else {
		read(allocator->fd, buffer, mlen);
		if (memcmp(FALLOC_MAGIC, buffer, mlen)) {
			allocator->err = FALLOC_INCOMPATIBLE_FILE;
		}
	}
}

static void _remap(struct falloc_allocator *allocator)
{
	size_t s = (((size_t)1) << allocator->order);
	if (!allocator->order)
		return;
	if (allocator->maptr)
		munmap(allocator->maptr, FALLOC_MMAP_LIMIT);
	ftruncate(allocator->fd, FALLOC_MPOOL_OFF + s);
	allocator->filelen = FALLOC_MPOOL_OFF + s;
	allocator->maptr = mmap(allocator->maptr,
		FALLOC_MMAP_LIMIT, PROT_READ | PROT_WRITE, MAP_SHARED,
	       	allocator->fd, 0);
	/* if (allocator->maptr == MAP_FAILED) { assert(0); } */
	allocator->mpool = allocator->maptr + FALLOC_MPOOL_OFF;
}

/*
 * Check `order` before you call `_init_order` and `_update_order`
 */
static void _init_order(struct falloc_allocator *allocator, size_t order)
{
	size_t i, s;
	size_t  l = order - allocator->min_order + 1;
	size_t tl = allocator->max_order - allocator->min_order + 1;
	assert(allocator->alloc_table = (struct bmp_state**)
		calloc(tl, sizeof(struct bmp_state*)));
	for (i = 0; i < l; ++i) {
		s = 1 << (l - i - 1);
		allocator->alloc_table[i] = new_bmp_state(s);
	}
	allocator->order = order;
	_remap(allocator);
	return;
}

static void _update_order(struct falloc_allocator *allocator, size_t order)
{
	size_t i, s;
	size_t l = order - allocator->min_order + 1;
	if (!allocator->order) {
		_init_order(allocator, order);
		/* `_init_order` will call `_remap` */
		return;
	}
	if (allocator->order == order) { /* Do nothing */
		return;
	} else if (allocator->order < order) { /* Grow */
		for (i = 0; i < (l - 1); ++i) {
			s = 1 << (l - i - 1);
			if (!allocator->alloc_table[i])
				allocator->alloc_table[i] = new_bmp_state(s);
			else
				bmp_state_resize(allocator->alloc_table[i], s);
		}
		allocator->alloc_table[i] = new_bmp_state(1);
	} else { /* Truncate */
		for (i = 0; i < l; ++i) {
			s = 1 << (l - i - 1);
			bmp_state_resize(allocator->alloc_table[i], s);
		}
		l = allocator->order - allocator->min_order + 1;
		for (; i < l; ++i) {
			del_bmp_state(allocator->alloc_table[i]);
			allocator->alloc_table[i] = NULL;
		}
	}
	allocator->order = order;
	_remap(allocator);
}

/* Check `tid` and `id` before you call `_alloc_table_alloc` */
static void _alloc_table_alloc(
	struct falloc_allocator *allocator, size_t tid, size_t id)
{
	size_t i, j, b, n;
	for (i = 0; i < tid; ++i) {
		n = ((size_t)1) << (tid - i);
		b = id * n;
		/* Need some optimizations here */
		for (j = 0; j < n; ++j) {
			bmp_state_alloc(
				allocator->alloc_table[i], b + j);
		}
	}
	while (allocator->alloc_table[i]) {
		bmp_state_alloc(allocator->alloc_table[i], id >> (i - tid));
		i++;
	}
	return;
}

/* Check `tid` and `id` before you call `_alloc_table_free` */
static void _alloc_table_free(
	struct falloc_allocator *allocator, size_t tid, size_t id)
{
	size_t i, j, b, n;
	for (i = 0; i < tid; ++i) {
		n = ((size_t)1) << (tid - i);
		b = id * n;
		/* Need some optimizations here */
		for (j = 0; j < n; ++j) {
			bmp_state_free(
				allocator->alloc_table[i], b + j);
		}
	}
	while (allocator->alloc_table[i]) {
		b = id >> (i - tid);
		if (b & 1) { /* 1 _ */
			if (bmp_state_test(allocator->alloc_table[i], b - 1)) {
				bmp_state_free(allocator->alloc_table[i], b);
				break;
			}
		} else {     /* _ 1 */
			if (bmp_state_test(allocator->alloc_table[i], b + 1)) {
				bmp_state_free(allocator->alloc_table[i], b);
				break;
			}
		}
		bmp_state_free(allocator->alloc_table[i], b);
		i++;
	}
	return;
}

/* Check `order` before call `_try_blk_alloc` */
static size_t _try_blk_alloc(struct falloc_allocator *allocator, size_t order)
{
	size_t id, tid;
	if (!allocator->order)
		_init_order(allocator, order);
	tid = order - allocator->min_order;
	if (!allocator->alloc_table[tid]) {
		_update_order(allocator, order);
	}
	id = bmp_state_get_available(allocator->alloc_table[tid]);
	if (id == BMPALLOC_FAILED) {
		if (allocator->max_order >= allocator->order + 1) {
			_update_order(allocator, allocator->order + 1);
			id = bmp_state_get_available( /* Retry */
				allocator->alloc_table[tid]);
			/* if (id == BMPALLOC_FAILED) { assert(0); } */
		} else {
			allocator->err = FALLOC_REACHED_LIMIT;
			return id;
		}
	}
	_alloc_table_alloc(allocator, tid, id);
	allocator->err = FALLOC_OK;
	return id;
}

struct falloc_allocator *falloc_open(const char *pathname)
{
	int fd;
	struct falloc_allocator *allocator;
	assert(allocator = (struct falloc_allocator*)
		calloc(1, sizeof(struct falloc_allocator)));
	fd = open(pathname, O_CREAT | O_RDWR, FALLOC_FILE_ACCESS);
	if (fd < 0) {
		allocator->err = FALLOC_FAILED_OPENFILE;
		return allocator;
	}
	/* Check header */
	allocator->fd = fd;
	allocator->filelen = _fsize(allocator->fd);
	_chk_magic(allocator);
	if (allocator->err != FALLOC_OK)
		return allocator;
	/* Check file, preparing for mapping */
	allocator->min_order = FALLOC_MIN_ORDER;
	allocator->max_order = FALLOC_MAX_ORDER;
	allocator->mpool_off = FALLOC_MPOOL_OFF;
	if (allocator->filelen <= allocator->mpool_off) {
		ftruncate(allocator->fd, allocator->mpool_off);
		allocator->order = 0;
	} else {
		allocator->order = _log2size_t(
			allocator->filelen - allocator->mpool_off);
		_init_order(allocator, allocator->order);
	}
	allocator->pathname = pathname;
	return allocator;
}

void falloc_close(struct falloc_allocator *allocator)
{
	size_t i, l = allocator->order - allocator->min_order + 1;
	if (allocator) {
		if (allocator->maptr) {
			munmap(allocator->maptr, FALLOC_MMAP_LIMIT);
		}
		if (allocator->order) {
			for (i = 0; i < l; ++i) {
				del_bmp_state(allocator->alloc_table[i]);
				allocator->alloc_table[i] = NULL;
			}
			free(allocator->alloc_table);
		}
		close(allocator->fd);
		free(allocator);
	}
}

void falloc_scan(struct falloc_allocator *allocator)
{
	size_t i, l, step;
	unsigned char *csr = (unsigned char*)allocator->mpool;
	if (!allocator->order)
		return;
	l = allocator->order - allocator->min_order + 1;
	for (i = 0; i < l; ++i) {
		bmp_state_reset(allocator->alloc_table[i]);
	}
	step = ((size_t)1) << allocator->min_order;
	printf("%d\n", step);
	exit(0);
	i = *((size_t*)csr);
	if (i) {
		/**/
	}
}

void *falloc_blk_alloc(struct falloc_allocator *allocator, size_t size)
{
	void *ptr;
	size_t blk_size, blk_id, order;
	blk_size = size + sizeof(size_t);
	order = _log2size_t(blk_size);
	if (order < allocator->min_order)
		order = allocator->min_order;
	if (order > allocator->max_order) {
		allocator->err = FALLOC_BIG_BLK_FAILED;
		return NULL;
	}
	blk_id = _try_blk_alloc(allocator, order);
	if (allocator->err != FALLOC_OK) {
		return NULL;
	} else {
		ptr = ((unsigned char*)allocator->mpool) +
			(((size_t)1) << order) * blk_id;
		*(size_t*)ptr = size;
		ptr = ((unsigned char*)ptr) + sizeof(size_t);
	}
	return ptr;
}

void falloc_blk_free(struct falloc_allocator *allocator, void *ptr)
{
	size_t tid, id, order, blk_size, size;
	void *blk_ptr = ((unsigned char*)ptr) - sizeof(size_t);
	size = *(size_t*)blk_ptr;
	if (!size) {
		/* Double free */
		return;
	}
	blk_size = size + sizeof(size_t);
	order = _log2size_t(blk_size);
	order = order > allocator->min_order ? order : allocator->min_order;
	tid = order - allocator->min_order;
	id = (((unsigned char*)blk_ptr) -
		((unsigned char*)allocator->mpool)) / (((size_t)1) << order);
	_alloc_table_free(allocator, tid, id);
	*(size_t*)blk_ptr = 0;
}

void falloc_truncate(struct falloc_allocator *allocator)
{
	size_t i, l = allocator->order - allocator->min_order;
	size_t order = allocator->order;
	if (!allocator->order)
		return;
	for (i = l; i > 0 ; --i) {
		if (allocator->alloc_table[i]->used == 0) {
			order--;
		} else {
			break;
		}
	}
	_update_order(allocator, order);
}

size_t falloc_offset(struct falloc_allocator *allocator, void *ptr)
{
	unsigned char *p = (unsigned char*)ptr;
	unsigned char *b = (unsigned char*)allocator->mpool;
	if (!b || !p || b > p)
		return ~((size_t)0);
	return p - b;
}

#ifdef FALLOC_DEBUG
#define _mem_calc(size, l) \
	(((size >> l) << l) - size ? (size >> l) + 1 : size >> l)

static void _print_binary_char(unsigned char c)
{
	int i, j;
	for (i = 7; i >= 0; --i) {
		j = c & (1 << i) ? 1 : 0;
		printf("%d", j);
	}
}

static void _print_binary(void *d, int len)
{
	int i;
	for (i = 0; i < len; ++i) {
		_print_binary_char(*((unsigned char*)d + i));
	}
}
static void falloc_print_alloc_table(struct falloc_allocator *allocator)
{
	size_t i, l;
	if (!allocator->order)
		return;
	l = allocator->order - allocator->min_order + 1;
	for (i = 0; i < l; ++i) {
		printf("TAB_O[%d]: ", i);
		_print_binary(allocator->alloc_table[i]->bmp,
			_mem_calc(allocator->alloc_table[i]->size, 3));
		putc('\n', stdout);
	}
}
#endif /* FALLOC_DEBUG */

void falloc_print_err(struct falloc_allocator *allocator)
{
	switch (allocator->err) {
	case FALLOC_OK:
		fputs("Running OK\n", stderr);
		break;
	case FALLOC_FAILED_OPENFILE:
		fputs("Failed open file\n", stderr);
		break;
	case FALLOC_INCOMPATIBLE_FILE:
		fputs("Incompatible file\n", stderr);
		break;
	case FALLOC_BIG_BLK_FAILED:
	case FALLOC_REACHED_LIMIT:
		fputs("Failed alloc\n", stderr);
	default:
		fputs("Unknown err\n", stderr);
		break;
	}
}

void falloc_print_info(struct falloc_allocator *allocator)
{
	if (!allocator) {
		fputs("Bad allocator object\n", stderr);
		return;
	}
	fprintf(stderr, "PATHNAME : %s\n", allocator->pathname);
	fprintf(stderr, "ORDER    : %d\n", allocator->order);
	fprintf(stderr, "MIN ORDER: %d\n", allocator->min_order);
	fprintf(stderr, "MAX ORDER: %d\n", allocator->max_order);
	fprintf(stderr, "FILE LENGTH: %ld\n", allocator->filelen);
	falloc_print_err(allocator);
#ifdef FALLOC_DEBUG
	falloc_print_alloc_table(allocator);
#endif
}
