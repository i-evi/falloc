#include <stdio.h>
#include <string.h>

#include "falloc.h"

void view(void *p)
{
	if (p) {
		printf("Found: %s\n", ((char*)p) + sizeof(size_t));
	}
}

int main()
{
	struct falloc_allocator *allocator;
	allocator = falloc_open("mem.bin");
	
	linklist_node_t *ls = falloc_scan(allocator);
	linklist_traverse(ls, view);
	falloc_print_info(allocator);
	falloc_close(allocator);
	return 0;
}

