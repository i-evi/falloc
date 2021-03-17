#ifndef _FALLOC_H_
#define _FALLOC_H_

#ifdef __cplusplus
	extern "C" {
#endif

#define FALLOC_FILE_ACCESS \
	(S_IWUSR | S_IRUSR | S_IRGRP | S_IROTH)

/* #define FALLOC_DEBUG */

#define FALLOC_MAGIC       "ialoc"
#define FALLOC_MIN_ORDER   9       /* 512 byte */
#define FALLOC_MAX_ORDER   30      /* 1024 MiB */
#define FALLOC_MPOOL_OFF   64
#define FALLOC_MMAP_LIMIT  (((size_t)1 << 32))   /* 4 GiB */
#define FALLOC_SIZE_T_BIT  (sizeof(size_t) << 3)

enum falloc_err_type {
	FALLOC_OK = 0,
	FALLOC_FAILED_OPENFILE,
	FALLOC_INCOMPATIBLE_FILE,
	FALLOC_BIG_BLK_FAILED,
	FALLOC_REACHED_LIMIT
};

/*
 * Byte order: Support Little endian only.
 */
struct falloc_allocator {
	const char *pathname;
	enum falloc_err_type err;
	struct bmp_state **alloc_table;
	size_t order;
	size_t min_order;
	size_t max_order;
	off_t  filelen;             /* Length of mapped file */
	off_t  alloc_size;          /* Allocated memory size */
	off_t  mpool_off;           /* Offset of memory pool */
	int    fd;
	int    endianess;           /* Byte order            */
	void   *maptr;              /* Ptr by mmap           */
	void   *mpool;              /* Ptr of memory pool    */
};

struct falloc_allocator *falloc_open (const char *pathname);

void falloc_close(struct falloc_allocator *allocator);

void falloc_truncate(struct falloc_allocator *allocator);

void falloc_scan(struct falloc_allocator *allocator);

size_t falloc_offset(struct falloc_allocator *allocator, void *ptr);

void *falloc_blk_alloc  (struct falloc_allocator *allocator, size_t size);
void  falloc_blk_free   (struct falloc_allocator *allocator, void *ptr);

void  falloc_print_err  (struct falloc_allocator *allocator);
void  falloc_print_info (struct falloc_allocator *allocator);

#ifdef __cplusplus
	}
#endif

#endif /* _FALLOC_H_ */
