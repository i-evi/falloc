#ifndef _BMPALLOC_H_
#define _BMPALLOC_H_
/*
 * Bitmap allocator
 */

#ifndef __cplusplus
	#define bool  int
	#define true  1
	#define false 0
#else
	extern "C" {
#endif

#ifdef  byte
#warning use predefined 'byte'
#else
#define byte unsigned char
#endif

#define SIZE_T_MAX  (~((size_t)0))
#define BMPALLOC_FAILED SIZE_T_MAX

typedef byte bmpdt;  /* Bitmap datatype */

#define BMPDT_SIZE 1

struct bmp_state {
	size_t size;
	size_t used;
	size_t csr;
	bmpdt *bmp;
};

struct bmp_state *new_bmp_state(size_t size);

void del_bmp_state(struct bmp_state *b);

bool bmp_state_set_cursor(struct bmp_state *b, size_t csr);

void bmp_state_reset(struct bmp_state *b);
bool bmp_state_resize(struct bmp_state *b, size_t size);

size_t bmp_state_get_available(struct bmp_state *b);

bool bmp_state_alloc(struct bmp_state *b, size_t id);
bool bmp_state_free (struct bmp_state *b, size_t id);
bool bmp_state_test (struct bmp_state *b, size_t id);

#ifdef __cplusplus
	}
#endif

#endif /* _BMPALLOC_H_ */
