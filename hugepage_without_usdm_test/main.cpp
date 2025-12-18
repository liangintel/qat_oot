
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "hugepage.h"
#include "main.h"

int qat_test();
int aio_test();

char* g_hugepages[HUGE_PAGE_NUM] = {0};

int main(int argc, const char **argv)
{
    aio_test();
    printf("\n\n");

    qat_test();

    for(int i = 0; i<HUGE_PAGE_NUM; i++)
        free_1g_hugepage(g_hugepages[i]);
}
