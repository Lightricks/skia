/*
 * Copyright 2009 The Android Open Source Project
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "SkBitmapProcState_opts_SSE2.h"
#include "SkBitmapProcState_utils.h"
#include "SkColorData.h"
#include "SkTo.h"

#include <emmintrin.h>

// Temporarily go into 64bit so we don't overflow during the add. Since we shift down by 16
// in the end, the result should always fit back in 32bits.
static inline int32_t safe_fixed_add_shift(SkFixed a, SkFixed b) {
    int64_t tmp = a;
    return SkToS32((tmp + b) >> 16);
}

static inline uint32_t ClampX_ClampY_pack_filter(SkFixed f, unsigned max,
                                                 SkFixed one) {
    unsigned i = SkClampMax(f >> 16, max);
    i = (i << 4) | ((f >> 12) & 0xF);
    return (i << 14) | SkClampMax(safe_fixed_add_shift(f, one), max);
}

/*  SSE version of ClampX_ClampY_filter_scale()
 *  portable version is in core/SkBitmapProcState_matrix.h
 */
void ClampX_ClampY_filter_scale_SSE2(const SkBitmapProcState& s, uint32_t xy[],
                                     int count, int x, int y) {
    SkASSERT((s.fInvType & ~(SkMatrix::kTranslate_Mask |
                             SkMatrix::kScale_Mask)) == 0);
    SkASSERT(s.fInvKy == 0);

    const unsigned maxX = s.fPixmap.width() - 1;
    const SkFixed one = s.fFilterOneX;
    const SkFixed dx = s.fInvSx;

    const SkBitmapProcStateAutoMapper mapper(s, x, y);
    const SkFixed fy = mapper.fixedY();
    const unsigned maxY = s.fPixmap.height() - 1;
    // compute our two Y values up front
    *xy++ = ClampX_ClampY_pack_filter(fy, maxY, s.fFilterOneY);
    // now initialize fx
    SkFixed fx = mapper.fixedX();

    // test if we don't need to apply the tile proc
    if (can_truncate_to_fixed_for_decal(fx, dx, count, maxX)) {
        if (count >= 4) {
            // SSE version of decal_filter_scale
            while ((size_t(xy) & 0x0F) != 0) {
                SkASSERT((fx >> (16 + 14)) == 0);
                *xy++ = (fx >> 12 << 14) | ((fx >> 16) + 1);
                fx += dx;
                count--;
            }

            __m128i wide_1    = _mm_set1_epi32(1);
            __m128i wide_dx4  = _mm_set1_epi32(dx * 4);
            __m128i wide_fx   = _mm_set_epi32(fx + dx * 3, fx + dx * 2,
                                              fx + dx, fx);

            while (count >= 4) {
                __m128i wide_out;

                wide_out = _mm_slli_epi32(_mm_srai_epi32(wide_fx, 12), 14);
                wide_out = _mm_or_si128(wide_out, _mm_add_epi32(
                                        _mm_srai_epi32(wide_fx, 16), wide_1));

                _mm_store_si128(reinterpret_cast<__m128i*>(xy), wide_out);

                xy += 4;
                fx += dx * 4;
                wide_fx  = _mm_add_epi32(wide_fx, wide_dx4);
                count -= 4;
            } // while count >= 4
        } // if count >= 4

        while (count-- > 0) {
            SkASSERT((fx >> (16 + 14)) == 0);
            *xy++ = (fx >> 12 << 14) | ((fx >> 16) + 1);
            fx += dx;
        }
    } else {
        // SSE2 only support 16bit interger max & min, so only process the case
        // maxX less than the max 16bit interger. Actually maxX is the bitmap's
        // height, there should be rare bitmap whose height will be greater
        // than max 16bit interger in the real world.
        if ((count >= 4) && (maxX <= 0xFFFF)) {
            while (((size_t)xy & 0x0F) != 0) {
                *xy++ = ClampX_ClampY_pack_filter(fx, maxX, one);
                fx += dx;
                count--;
            }

            __m128i wide_fx   = _mm_set_epi32(fx + dx * 3, fx + dx * 2,
                                              fx + dx, fx);
            __m128i wide_dx4  = _mm_set1_epi32(dx * 4);
            __m128i wide_one  = _mm_set1_epi32(one);
            __m128i wide_maxX = _mm_set1_epi32(maxX);
            __m128i wide_mask = _mm_set1_epi32(0xF);

             while (count >= 4) {
                __m128i wide_i;
                __m128i wide_lo;
                __m128i wide_fx1;

                // i = SkClampMax(f>>16,maxX)
                wide_i = _mm_max_epi16(_mm_srli_epi32(wide_fx, 16),
                                       _mm_setzero_si128());
                wide_i = _mm_min_epi16(wide_i, wide_maxX);

                // i<<4 | EXTRACT_LOW_BITS(fx)
                wide_lo = _mm_srli_epi32(wide_fx, 12);
                wide_lo = _mm_and_si128(wide_lo, wide_mask);
                wide_i  = _mm_slli_epi32(wide_i, 4);
                wide_i  = _mm_or_si128(wide_i, wide_lo);

                // i<<14
                wide_i = _mm_slli_epi32(wide_i, 14);

                // SkClampMax(((f+one))>>16,max)
                wide_fx1 = _mm_add_epi32(wide_fx, wide_one);
                wide_fx1 = _mm_max_epi16(_mm_srli_epi32(wide_fx1, 16),
                                                        _mm_setzero_si128());
                wide_fx1 = _mm_min_epi16(wide_fx1, wide_maxX);

                // final combination
                wide_i = _mm_or_si128(wide_i, wide_fx1);
                _mm_store_si128(reinterpret_cast<__m128i*>(xy), wide_i);

                wide_fx = _mm_add_epi32(wide_fx, wide_dx4);
                fx += dx * 4;
                xy += 4;
                count -= 4;
            } // while count >= 4
        } // if count >= 4

    /*
        while (count-- > 0) {
            *xy++ = ClampX_ClampY_pack_filter(fx, maxX, one);
            fx += dx;
        }
        We'd like to write this as above, but that form allows fx to get 1-iteration too big/small
        when count is 0, and this can trigger a UBSAN error, even though we won't in fact use that
        last (undefined) value for fx.

        Here is an alternative that should always be efficient, but seems much harder to read:

        if (count > 0) {
            for (;;) {
                *xy++ = ClampX_ClampY_pack_filter(fx, maxX, one);
                if (--count == 0) break;
                fx += dx;
            }
        }

        For now, we'll try this variant: more compact than the if/for version, and we hope the
        compiler will get rid of the integer multiply.
     */
        for (int i = 0; i < count; ++i) {
            *xy++ = ClampX_ClampY_pack_filter(fx + i*dx, maxX, one);
        }
    }
}

/*  SSE version of ClampX_ClampY_nofilter_scale()
 *  portable version is in core/SkBitmapProcState_matrix.h
 */
void ClampX_ClampY_nofilter_scale_SSE2(const SkBitmapProcState& s,
                                    uint32_t xy[], int count, int x, int y) {
    SkASSERT((s.fInvType & ~(SkMatrix::kTranslate_Mask |
                             SkMatrix::kScale_Mask)) == 0);

    // we store y, x, x, x, x, x
    const unsigned maxX = s.fPixmap.width() - 1;
    const SkBitmapProcStateAutoMapper mapper(s, x, y);
    const unsigned maxY = s.fPixmap.height() - 1;
    *xy++ = SkClampMax(mapper.intY(), maxY);
    SkFixed fx = mapper.fixedX();

    if (0 == maxX) {
        // all of the following X values must be 0
        memset(xy, 0, count * sizeof(uint16_t));
        return;
    }

    const SkFixed dx = s.fInvSx;

    // test if we don't need to apply the tile proc
    if ((unsigned)(fx >> 16) <= maxX &&
        (unsigned)((fx + dx * (count - 1)) >> 16) <= maxX) {
        // SSE version of decal_nofilter_scale
        if (count >= 8) {
            while (((size_t)xy & 0x0F) != 0) {
                *xy++ = pack_two_shorts(fx >> 16, (fx + dx) >> 16);
                fx += 2 * dx;
                count -= 2;
            }

            __m128i wide_dx4 = _mm_set1_epi32(dx * 4);
            __m128i wide_dx8 = _mm_add_epi32(wide_dx4, wide_dx4);

            __m128i wide_low = _mm_set_epi32(fx + dx * 3, fx + dx * 2,
                                             fx + dx, fx);
            __m128i wide_high = _mm_add_epi32(wide_low, wide_dx4);

            while (count >= 8) {
                __m128i wide_out_low = _mm_srli_epi32(wide_low, 16);
                __m128i wide_out_high = _mm_srli_epi32(wide_high, 16);

                __m128i wide_result = _mm_packs_epi32(wide_out_low,
                                                      wide_out_high);
                _mm_store_si128(reinterpret_cast<__m128i*>(xy), wide_result);

                wide_low = _mm_add_epi32(wide_low, wide_dx8);
                wide_high = _mm_add_epi32(wide_high, wide_dx8);

                xy += 4;
                fx += dx * 8;
                count -= 8;
            }
        } // if count >= 8

        uint16_t* xx = reinterpret_cast<uint16_t*>(xy);
        while (count-- > 0) {
            *xx++ = SkToU16(fx >> 16);
            fx += dx;
        }
    } else {
        // SSE2 only support 16bit interger max & min, so only process the case
        // maxX less than the max 16bit interger. Actually maxX is the bitmap's
        // height, there should be rare bitmap whose height will be greater
        // than max 16bit interger in the real world.
        if ((count >= 8) && (maxX <= 0xFFFF)) {
            while (((size_t)xy & 0x0F) != 0) {
                *xy++ = pack_two_shorts(SkClampMax((fx + dx) >> 16, maxX),
                                        SkClampMax(fx >> 16, maxX));
                fx += 2 * dx;
                count -= 2;
            }

            __m128i wide_dx4 = _mm_set1_epi32(dx * 4);
            __m128i wide_dx8 = _mm_add_epi32(wide_dx4, wide_dx4);

            __m128i wide_low = _mm_set_epi32(fx + dx * 3, fx + dx * 2,
                                             fx + dx, fx);
            __m128i wide_high = _mm_add_epi32(wide_low, wide_dx4);
            __m128i wide_maxX = _mm_set1_epi32(maxX);

            while (count >= 8) {
                __m128i wide_out_low = _mm_srli_epi32(wide_low, 16);
                __m128i wide_out_high = _mm_srli_epi32(wide_high, 16);

                wide_out_low  = _mm_max_epi16(wide_out_low,
                                              _mm_setzero_si128());
                wide_out_low  = _mm_min_epi16(wide_out_low, wide_maxX);
                wide_out_high = _mm_max_epi16(wide_out_high,
                                              _mm_setzero_si128());
                wide_out_high = _mm_min_epi16(wide_out_high, wide_maxX);

                __m128i wide_result = _mm_packs_epi32(wide_out_low,
                                                      wide_out_high);
                _mm_store_si128(reinterpret_cast<__m128i*>(xy), wide_result);

                wide_low  = _mm_add_epi32(wide_low, wide_dx8);
                wide_high = _mm_add_epi32(wide_high, wide_dx8);

                xy += 4;
                fx += dx * 8;
                count -= 8;
            }
        } // if count >= 8

        uint16_t* xx = reinterpret_cast<uint16_t*>(xy);
        while (count-- > 0) {
            *xx++ = SkClampMax(fx >> 16, maxX);
            fx += dx;
        }
    }
}
