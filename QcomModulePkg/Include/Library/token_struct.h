#ifndef __TOKEN_STRUCT_H__
#define __TOKEN_STRUCT_H__

#ifdef __cplusplus
extern "C" {
#endif

#if TCT_FEATURE_TOKEN_SUPPORT
typedef struct token_info
{
    UINT32 MAGIC;
    UINT8 pad0[4];
    UINT8 pad1[4];
    UINT8 serialno[16];
    UINT16 version;
    UINT16 CHECKSUM;
    double timestamp;
    UINT64 expired_at;
    UINT8 pad2[48];
    UINT32 OEMTOKEN;
    UINT32 UARTTOKEN;
    UINT32 FASTBOOTTOKEN;
    UINT32 ADBTOKEN;
    UINT32 SMARTLOGTOKEN;
    UINT32 DIAGTOKEN;
    UINT32 ROOTTOKEN;
    UINT32 RETAILTOKEN;
    UINT32 PERFTOKEN;
    UINT32 SMLTOKEN;
    UINT32 ROLLBACKTOKEN;
    UINT8 pad3[100];
}token_info_t;
#endif

typedef struct inproductflag_info
{
    UINT32 MAGIC;
    //UINT8 pad0[4];
    //UINT8 pad1[4];
    UINT32 serialno;
    UINT16 version;
    UINT16 CHECKSUM;
    //UINT8 pad2[64];
    UINT32 inproductionflag;
    UINT8 pad[496];
}inproductflag_info_t;

typedef struct param_info
{
    UINT32 multisim;
    UINT32 ECID;
    UINT8 subvariant[16];
    UINT8 product[16];
    UINT8 variant[16];
    UINT8 name[48];
    /* codename same is model */
    UINT8 model[16];
    UINT8 brand[16];
    UINT8 publicname[16];
    /* make the 21 byte 0 */
    UINT8 cu[21];
}param_info_t;

typedef struct rework_info
{
    UINT32 dowork;
    UINT32 flag;
    UINT32 ECID;
    UINT8 subvariant[16];
    UINT8 product[16];
    UINT8 variant[16];
}rework_info_t;

#define ECID_Rework_flag           0x00000001
#define Subvariant_Rework_flag     0x00000002
#define Product_Rework_flag        0x00000004
#define Variant_Rework_flag        0x00000008

#define TOKEN_STRUCT_SIZE (sizeof(token_struct_t))

#define TOKEN_STRUCT_MAGIC   (0x4e4b544f) //"OTKN"
#define TOKEN_STRUCT_VERSION (2 << 4 | 0) //2.0

#define INPRODUCT_STRUCT_MAGIC   (0x5452504e) //"NPRT"
#define INPRODUCT_STRUCT_VERSION (1 << 4 | 0) //1.0

typedef struct serialno_info
{
    UINT32 MAGIC;
    UINT16 version;
    UINT16 CHECKSUM;
    UINT8 serialno[64];
    UINT8 pad[56];
}serialno_info_t;

#define SERIALNO_STRUCT_MAGIC   (0x73656e6f) //"SENO"
#define SERIALNO_STRUCT_VERSION (1 << 4 | 0) //1.0

/**
 * example:
 *
 * #include <string.h>
 * #include "token_struct.h"
 * #include "token_crc16.h"
 * ...
 *     token_struct_t token;
 *     memset(&token, 0, TOKEN_STRUCT_SIZE);
 *     token.MAGIC = TOKEN_STRUCT_MAGIC;
 *     token.version = TOKEN_STRUCT_VERSION;
 *     ...
 *     token.CHECKSUM = CalculateCrc16((UINT8 *) &token, TOKEN_STRUCT_SIZE, 0);
 * ...
 */


#ifdef __cplusplus
}
#endif

#endif /* __TOKEN_STRUCT_H__ */
