#define BYTE_INVERSE_16(n) (n >> 8) | (n << 8)
#define BYTE_INVERSE_32(n)                                                  \
  BYTE_INVERSE_16((uint16_t)(n >> 16)) >> 16 | BYTE_INVERSE_16((uint16_t)n) \
                                                   << 16
