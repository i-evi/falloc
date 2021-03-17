# FALLOC

Allocate and free dynamic memory from file. Similar to `malloc` and `free`. It uses `mmap` to map files, and a simple allocator is employed to manage the mapped memory.

*This project will works on Linux only.
