#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define GPIO_BASE        0x20200000 
#define BLOCK_SIZE      4096

/* GPIO macros */
#define INP_GPIO_O4(b)  *(volatile unsigned int *)(0x00000000+b) &= (unsigned int)0xFFFF8FFF
#define OUT_GPIO_O4(b)  *(volatile unsigned int *)(0x00000000+b) |= (unsigned int)0x00001000

#define GPIO_SET_O4(b)  *(volatile unsigned int *)(0x0000001C/4+b) = 1<<4
#define GPIO_CLR_O4(b)  *(volatile unsigned int *)(0x00000028/4+b) = 1<<4

/* main function */
int main(int argc, char *argv[])
{
    unsigned long addr_p = GPIO_BASE;
    int fd;
    void *map;
    int i;

    if ((fd = open("/dev/mem", O_RDWR|O_SYNC)) < 0)
    {
        perror("open");
        exit(1);
    }

    map = mmap(NULL, BLOCK_SIZE, PROT_READ|PROT_WRITE, MAP_SHARED, fd, addr_p);

    if (map == MAP_FAILED) {
        perror("mmap");
        exit(1);
    }

    /* INP_GPIO_O4(map); */
    OUT_GPIO_O4(map);

    for(i = 0; i<10; i++)
    {
        GPIO_SET_O4(map);
        sleep(1);
        GPIO_CLR_O4(map);
        sleep(1);
    }

    munmap(map, BLOCK_SIZE);
    close(fd);

    return 0;
}
