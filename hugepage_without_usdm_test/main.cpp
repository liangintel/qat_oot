
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

int qat_test();
int aio_test();

int main(int argc, const char **argv)
{
    aio_test();
    printf("\n\n");

    qat_test();
}
