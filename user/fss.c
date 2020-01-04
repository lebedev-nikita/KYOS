#include <inc/lib.h>

void umain(int argc, char **argv) 
{
    cprintf("FSS begin\n");
    unsigned int free_space = fs_free_space_in_bytes();
    cprintf("Free space: %u bytes\n", free_space);
    cprintf("FSS end\n");
}