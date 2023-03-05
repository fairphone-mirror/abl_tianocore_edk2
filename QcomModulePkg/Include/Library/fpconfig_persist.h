#ifndef __FP_CONFIG_PERSIST_H__
#define __FP_CONFIG_PERSIST_H__

#define FPCONFIG_STRUCT_VERSION V.1.1

#define FPCONFIG_MAGIC "fpconfig"
#define FPCONFIG_MAGIC_SIZE 8

#define FPCONFIG_CID_OFFSET    8
#define FPCONFIG_CID_SIZE      4

#define FPCONFIG_TFT_OFFSET	   40
#define FPCONFIG_TFT_SIZE	   8


typedef struct {
    CHAR8 magic[FPCONFIG_MAGIC_SIZE];	// offset:0    size:8
    CHAR8 cid[FPCONFIG_CID_SIZE];		// offset:8    size:4
    CHAR8 reserve[28];					// offset:12   size:28
    INT64 tft;							// offset:40   size:8
} FPConfig_t;


#define FPCONFIG_MMAP_SIZE (128 * 1024) //Total size of fpconfig partition.

#endif //__FP_CONFIG_PERSIST_H__
