#include <stdio.h>
#include <string.h>

#include "falloc.h"

struct falloc_allocator *allocator;

void *falloc(size_t size)
{
	return falloc_blk_alloc(allocator, size);
}

void ffree (void *ptr)
{
	falloc_blk_free(allocator, ptr);
}

int main()
{
	allocator = falloc_open("mem.bin");
	
	char *buffer = (char*)falloc(256);
	strcpy(buffer, "\n ** Hello falloc **\n");
	printf("%s\n", buffer);
	ffree(buffer);

	/* Do not free these buffers. They can be found by falloc_scan. */
	for (int i = 0; i < 10; ++i) {
		buffer = (char*)falloc(256);
		sprintf(buffer, "Buffer: %d\n", i);
	}
	
	falloc_print_info(allocator);
	falloc_close(allocator);
	return 0;
}
