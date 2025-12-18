#ifndef ___MAIN__H___
#define ___MAIN__H___

typedef struct {
    char matedata[2048];
    char qat_in1[16*1024];
    char qat_in2[16*1024];
    char qat_out1[16*1024];
	char qat_out2[16*1024];
} db_block_s;

#define HUGE_PAGE_NUM 2
extern char* g_hugepages[HUGE_PAGE_NUM];

#define HUGE_PAGE_NUM 2
#define SIZE_8M (8*1024*1024)
#define SIZE_1G (1024*1024*1024)
extern int g_blk_num;
extern db_block_s* g_blks[HUGE_PAGE_NUM*(SIZE_1G/SIZE_8M)];

#endif
