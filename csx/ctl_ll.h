/*
 * ctl_ll.h -- Definitions and utilities for the ctl byte array of CSX.
 *
 * Copyright (C) 2009-2011, Computing Systems Laboratory (CSLab), NTUA.
 * Copyright (C) 2009-2011, Kornilios Kourtis
 * Copyright (C) 2011,      Vasileios Karakasis
 * All rights reserved.
 *
 * This file is distributed under the BSD License. See LICENSE.txt for details.
 */
#ifndef CTX_CTL_LL_H__
#define CTX_CTL_LL_H__


#include <assert.h>
#include <inttypes.h>
#include <dynarray.h>


/*
 * bit functions
 */
static inline void set_bit(uint8_t *byte, int bit)
{
    assert(bit >= 0);
    assert(bit < 8);
    *byte |= (1<<bit);
}

static inline int test_bit(uint8_t *byte, int bit)
{
    assert(bit >= 0);
    assert(bit < 8);
    return (*byte & (1<<bit));
}

// Ctl Array is a byte-based array storing (compressed)
// index information for the sparse matrix
//
// Format:
//   [flags][size][unit-specific ...]
//
//  flags bits:
//          7  6  5  4  3  2  1  0
//         [ ][ ][ ][ ][ ][ ][ ][ ]
//          |  |  |______________|
//    new row  |        |------------> arbitrary patterns
//          row jmp
//        (if enabled)
#define CTL_NR_BIT 7
#define CTL_RJMP_BIT 6
#define CTL_PATTERNS_MAX    ((1 << CTL_RJMP_BIT) - 1)

// size is 8 bits: 0..255
#define CTL_SIZE_MAX ((1<<8) - 1)

#define CTL_PATTERN_MASK ~(1<<CTL_NR_BIT | 1<<CTL_RJMP_BIT)

/* Encode and Decode functions for Ctl arrays */

#ifdef CTL_LL_NOSTATIC
#define CTL_LL_STATIC
#else
#define CTL_LL_STATIC static inline
#endif

/*
 *  put a 32-bit integer unencoded
 */
static inline void da_put_u32(dynarray_t *da, uint32_t val)
{
    uint32_t *uc = (uint32_t *) dynarray_alloc_nr(da, sizeof(val));
    *uc = val;
}

/* put an encoded unsigned long in a dynarray */
static inline void da_put_ul(dynarray_t *da, unsigned long val)
{
    uint8_t *uc;
    const unsigned shift = 7;

    for (;;) {
        uc = (uint8_t *) dynarray_alloc(da);
        *uc = (val & ((1 << shift) - 1));
        if (val < (1 << shift))
            break;

        *uc |= (1 << shift);
        val >>= shift;
    }
}

CTL_LL_STATIC uint64_t u8_get(uint8_t **ctl)
{
    uint8_t ret = **ctl;
    (*ctl)++;

    return (uint64_t)ret;
}

CTL_LL_STATIC uint64_t u16_get(uint8_t **ctl)
{
    uint16_t ret, **u16;

    u16 = (uint16_t **)ctl;
    ret = **u16;
    (*u16)++;

    return (uint64_t)ret;
}

CTL_LL_STATIC uint64_t u32_get(uint8_t **ctl)
{
    uint32_t ret, **u32;

    u32 = (uint32_t **)ctl;
    ret = **u32;
    (*u32)++;

    return (uint64_t)ret;
}

CTL_LL_STATIC uint64_t u64_get(uint8_t **ctl)
{
    uint64_t ret, **u64;

    u64 = (uint64_t **)ctl;
    ret = **u64;
    (*u64)++;

    return ret;
}

uint64_t ul_get(uint8_t **ctl)
{
    unsigned long ret;
    unsigned shift = 7;
    unsigned long uc;

    ret = u8_get(ctl);
    if (ret <= 127)
        goto end;

    ret -= 128;
    for (;;){
        uc = u8_get(ctl);
        if (uc <= 127){
            ret += (uc<<shift);
            break;
        }
        uc -= 128;
        ret += (uc<<shift);
        shift += 7;
    }
end:
    return ret;
}

#endif // CTL_CTX_LL_H

// vim:expandtab:tabstop=8:shiftwidth=4:softtabstop=4__
