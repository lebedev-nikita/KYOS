#include <inc/lib.h>

void umain(int argc, char **argv) 
{
    cprintf("FSS begin\n");
    if (argc < 3) {
        cprintf ("Not enough parametres\n");
        goto end;
    }

    if      (argv[1][0] == 'c' && argv[1][1] == '\0') {
        cprintf("c\n");
    }
    else if (argv[1][0] == 'r' && argv[1][1] == '\0') {
        cprintf("r\n");
    }
    else if (argv[1][0] == 'l' && argv[1][1] == '\0') {
        cprintf("l\n");
    }
    else if (argv[1][0] == 'd' && argv[1][1] == '\0') {
        cprintf("d\n");
    }
    else if (argv[1][0] == 'i' && argv[1][1] == '\0') {
        cprintf("i\n");
    }
    else {
        cprintf("Bad args\n");
    }
end:
    cprintf("FSS end\n");
}