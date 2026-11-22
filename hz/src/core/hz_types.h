#ifndef HZ_TYPES_H
#define HZ_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/* As ps2_types.h: ported logic uses these rather than raw stdint types, so
 * that widening anything past the original's word sizes is a decision made
 * in one place. */
typedef uint8_t  u8;
typedef int8_t   s8;
typedef uint16_t u16;
typedef int16_t  s16;
typedef uint32_t u32;
typedef int32_t  s32;
typedef uint64_t u64;
typedef int64_t  s64;

#endif /* HZ_TYPES_H */
