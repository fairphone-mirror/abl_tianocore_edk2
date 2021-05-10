#ifndef __TOKEN_STRUCT_H__
#define __TOKEN_STRUCT_H__

#define INPRODUCT_STRUCT_MAGIC   (0x5452504e) // "NPRT"
#define INPRODUCT_STRUCT_VERSION (1 << 4 | 0) // 1.0

typedef struct inproductflag_info
{
    UINT32 MAGIC;
    UINT32 serialno;
    UINT16 version;
    UINT16 CHECKSUM;
    UINT32 inproductionflag;
    UINT8 pad[496];
} inproductflag_info_t;

#endif /* __TOKEN_STRUCT_H__ */
