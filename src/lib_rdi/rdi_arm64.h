// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)
#ifndef RDI_ARM64_H
#define RDI_ARM64_H
#define RDI_RegCodeARM64_XList \
  X(nil, 0) \
  X(x0, 1) \
  X(x1, 2) \
  X(x2, 3) \
  X(x3, 4) \
  X(x4, 5) \
  X(x5, 6) \
  X(x6, 7) \
  X(x7, 8) \
  X(x8, 9) \
  X(x9, 10) \
  X(x10, 11) \
  X(x11, 12) \
  X(x12, 13) \
  X(x13, 14) \
  X(x14, 15) \
  X(x15, 16) \
  X(x16, 17) \
  X(x17, 18) \
  X(x18, 19) \
  X(x19, 20) \
  X(x20, 21) \
  X(x21, 22) \
  X(x22, 23) \
  X(x23, 24) \
  X(x24, 25) \
  X(x25, 26) \
  X(x26, 27) \
  X(x27, 28) \
  X(x28, 29) \
  X(x29, 30) \
  X(x30, 31) \
  X(sp, 32) \
  X(pc, 33) \
  X(cpsr, 34) \
  X(v0, 35) \
  X(v1, 36) \
  X(v2, 37) \
  X(v3, 38) \
  X(v4, 39) \
  X(v5, 40) \
  X(v6, 41) \
  X(v7, 42) \
  X(v8, 43) \
  X(v9, 44) \
  X(v10, 45) \
  X(v11, 46) \
  X(v12, 47) \
  X(v13, 48) \
  X(v14, 49) \
  X(v15, 50) \
  X(v16, 51) \
  X(v17, 52) \
  X(v18, 53) \
  X(v19, 54) \
  X(v20, 55) \
  X(v21, 56) \
  X(v22, 57) \
  X(v23, 58) \
  X(v24, 59) \
  X(v25, 60) \
  X(v26, 61) \
  X(v27, 62) \
  X(v28, 63) \
  X(v29, 64) \
  X(v30, 65) \
  X(v31, 66) \
  X(fpsr, 67) \
  X(fpcr, 68)

typedef RDI_U8 RDI_RegCodeARM64;
typedef enum RDI_RegCodeARM64Enum
{
#define X(name, value) RDI_RegCodeARM64_##name = value,
  RDI_RegCodeARM64_XList
#undef X
} RDI_RegCodeARM64Enum;
#endif
