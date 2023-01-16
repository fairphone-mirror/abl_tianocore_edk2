#ifndef __FP_CONFIG_PERSIST_H__
#define __FP_CONFIG_PERSIST_H__

//#include <Protocol/EFIVerifiedBoot.h>

#define FPCONFIG_STRUCT_VERSION V.1.0


#define FPCONFIG_MAGIC "fpconfig"
#define FPCONFIG_MAGIC_SIZE 8

#define FPCONFIG_CID_OFFSET    8
#define FPCONFIG_CID_SIZE      4

typedef struct {
    CHAR8 magic[FPCONFIG_MAGIC_SIZE];
    CHAR8 cid[FPCONFIG_CID_SIZE];
} FPConfig_t;

#endif //__FP_CONFIG_PERSIST_H__
