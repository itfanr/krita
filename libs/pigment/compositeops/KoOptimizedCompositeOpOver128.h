/*
 * SPDX-FileCopyrightText: 2015 Thorsten Zachmann <zachmann@kde.org>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */

#ifndef KOOPTIMIZEDCOMPOSITEOPOVER128_H_
#define KOOPTIMIZEDCOMPOSITEOPOVER128_H_

#include "KoCompositeOpBase.h"
#include "KoCompositeOpRegistry.h"
#include "KoStreamedMath.h"

#define NATIVE_OPACITY_OPAQUE KoColorSpaceMathsTraits<channels_type>::unitValue
#define NATIVE_OPACITY_TRANSPARENT KoColorSpaceMathsTraits<channels_type>::zeroValue

#define INFO_DEBUG 0

template<typename channels_type, bool alphaLocked, bool allChannelsFlag>
struct OverCompositor128 {
    struct ParamsWrapper {
        ParamsWrapper(const KoCompositeOp::ParameterInfo& params)
            : channelFlags(params.channelFlags)
        {
        }
        const QBitArray &channelFlags;
    };

    struct Pixel {
        channels_type red;
        channels_type green;
        channels_type blue;
        channels_type alpha;
    };

    // \see docs in AlphaDarkenCompositor32
    template<bool haveMask, bool src_aligned, Vc::Implementation _impl>
    static ALWAYS_INLINE void compositeVector(const quint8 *src, quint8 *dst, const quint8 *mask, float opacity, const ParamsWrapper &oparams)
    {
#if INFO_DEBUG
        static quint32 countTotal = 0;
        static quint32 countOne = 0;
        static quint32 countTwo = 0;
        static quint32 countThree = 0;
        static quint32 countFour = 0;

        if (++countTotal % 250000 == 0) {
            qInfo() << "count" << countOne << countTwo << countThree << countFour << countTotal << opacity;
        }
#endif
        Q_UNUSED(oparams);

        Vc::float_v src_alpha;
        Vc::float_v dst_alpha;

        Vc::float_v src_c1;
        Vc::float_v src_c2;
        Vc::float_v src_c3;

        PixelWrapper<channels_type, _impl> dataWrapper;
        dataWrapper.read(const_cast<quint8*>(src), src_c1, src_c2, src_c3, src_alpha);

        //bool haveOpacity = opacity != 1.0;
        const Vc::float_v opacity_norm_vec(opacity);
        src_alpha *= opacity_norm_vec;

        if (haveMask) {
            const Vc::float_v uint8MaxRec1((float)1.0 / 255);
            Vc::float_v mask_vec = KoStreamedMath<_impl>::fetch_mask_8(mask);
            src_alpha *= mask_vec * uint8MaxRec1;
        }

        const Vc::float_v zeroValue(static_cast<float>(NATIVE_OPACITY_TRANSPARENT));
        // The source cannot change the colors in the destination,
        // since its fully transparent
        if ((src_alpha == zeroValue).isFull()) {
#if INFO_DEBUG
            countFour++;
#endif
            return;
        }

        Vc::float_v dst_c1;
        Vc::float_v dst_c2;
        Vc::float_v dst_c3;

        dataWrapper.read(dst, dst_c1, dst_c2, dst_c3, dst_alpha);

        Vc::float_v src_blend;
        Vc::float_v new_alpha;

        const Vc::float_v oneValue(1.0f);
        if ((dst_alpha == oneValue).isFull()) {
            new_alpha = dst_alpha;
            src_blend = src_alpha;
        } else if ((dst_alpha == zeroValue).isFull()) {
            new_alpha = src_alpha;
            src_blend = oneValue;
        } else {
            /**
             * The value of new_alpha can have *some* zero values,
             * which will result in NaN values while division.
             */
            new_alpha = dst_alpha + (oneValue - dst_alpha) * src_alpha;
            Vc::float_m mask = (new_alpha == zeroValue);
            src_blend = src_alpha / new_alpha;
            src_blend.setZero(mask);
        }

        if (!(src_blend == oneValue).isFull()) {
#if INFO_DEBUG
            ++countOne;
#endif

            dst_c1 = src_blend * (src_c1 - dst_c1) + dst_c1;
            dst_c2 = src_blend * (src_c2 - dst_c2) + dst_c2;
            dst_c3 = src_blend * (src_c3 - dst_c3) + dst_c3;

            dataWrapper.write(dst, dst_c1, dst_c2, dst_c3, new_alpha);
        } else {
#if INFO_DEBUG
                ++countTwo;
#endif
            dataWrapper.write(dst, src_c1, src_c2, src_c3, new_alpha);
        }
    }

    template <bool haveMask, Vc::Implementation _impl>
    static ALWAYS_INLINE void compositeOnePixelScalar(const quint8 *src, quint8 *dst, const quint8 *mask, float opacity, const ParamsWrapper &oparams)
    {
        using namespace Arithmetic;
        const qint32 alpha_pos = 3;

        const channels_type *s = reinterpret_cast<const channels_type*>(src);
        channels_type *d = reinterpret_cast<channels_type*>(dst);

        float srcAlpha = s[alpha_pos];
        PixelWrapper<channels_type, _impl>::normalizeAlpha(srcAlpha);
        srcAlpha *= opacity;

        if (haveMask) {
            const float uint8Rec1 = 1.0f / 255.0f;
            srcAlpha *= float(*mask) * uint8Rec1;
        }

#if INFO_DEBUG
        static int xx = 0;
        bool display = xx > 45 && xx < 50;
        if (display) {
            qInfo() << "O" << s[alpha_pos] << srcAlpha << haveMask << opacity;
        }
#endif

        if (srcAlpha != 0.0f) {

            float dstAlpha = d[alpha_pos];
            PixelWrapper<channels_type, _impl>::normalizeAlpha(dstAlpha);
            float srcBlendNorm;

            if (alphaLocked || dstAlpha == 1.0f) {
                srcBlendNorm = srcAlpha;
            } else if (dstAlpha == 0.0f) {
                dstAlpha = srcAlpha;
                srcBlendNorm = 1.0f;

                if (!allChannelsFlag) {
                    KoStreamedMathFunctions::clearPixel<sizeof(Pixel)>(dst);
                }
            } else {
                dstAlpha += (1.0f - dstAlpha) * srcAlpha;
                srcBlendNorm = srcAlpha / dstAlpha;
            }

#if INFO_DEBUG
            if (display) {
                qInfo() << "params" << srcBlendNorm << allChannelsFlag << alphaLocked << dstAlpha << haveMask;
            }
#endif
            if(allChannelsFlag) {
                if (srcBlendNorm == 1.0f) {
                    if (!alphaLocked) {
                        KoStreamedMathFunctions::copyPixel<sizeof(Pixel)>(src, dst);
                    } else {
                        d[0] = s[0];
                        d[1] = s[1];
                        d[2] = s[2];
                    }
                } else if (srcBlendNorm != 0.0f){
#if INFO_DEBUG
                    if (display) {
                        qInfo() << "calc" << s[0] << d[0] << srcBlendNorm * (s[0] - d[0]) + d[0] << s[0] - d[0] << srcBlendNorm * (s[0] - d[0]) << srcBlendNorm;
                    }
#endif

                    d[0] = PixelWrapper<channels_type, _impl>::lerpMixedUintFloat(d[0], s[0], srcBlendNorm);
                    d[1] = PixelWrapper<channels_type, _impl>::lerpMixedUintFloat(d[1], s[1], srcBlendNorm);
                    d[2] = PixelWrapper<channels_type, _impl>::lerpMixedUintFloat(d[2], s[2], srcBlendNorm);
                }
            } else {
                const QBitArray &channelFlags = oparams.channelFlags;

                if (srcBlendNorm == 1.0f) {
                    if(channelFlags.at(0)) d[0] = s[0];
                    if(channelFlags.at(1)) d[1] = s[1];
                    if(channelFlags.at(2)) d[2] = s[2];
                } else if (srcBlendNorm != 0.0f) {
                    if(channelFlags.at(0)) d[0] = PixelWrapper<channels_type, _impl>::lerpMixedUintFloat(d[0], s[0], srcBlendNorm);
                    if(channelFlags.at(1)) d[1] = PixelWrapper<channels_type, _impl>::lerpMixedUintFloat(d[1], s[1], srcBlendNorm);;
                    if(channelFlags.at(2)) d[2] = PixelWrapper<channels_type, _impl>::lerpMixedUintFloat(d[2], s[2], srcBlendNorm);;
                }
            }

            if (!alphaLocked) {
                PixelWrapper<channels_type, _impl>::denormalizeAlpha(dstAlpha);
                d[alpha_pos] = PixelWrapper<channels_type, _impl>::roundFloatToUint(dstAlpha);
            }
#if INFO_DEBUG
            if (display) {
                qInfo() << "result" << d[0] << d[1] << d[2] << d[3];
            }
            ++xx;
#endif
        }
    }
};

/**
 * An optimized version of a composite op for the use in 16 byte
 * colorspaces with alpha channel placed at the last byte of
 * the pixel: C1_C2_C3_A.
 */
template<Vc::Implementation _impl>
class KoOptimizedCompositeOpOver128 : public KoCompositeOp
{
public:
    KoOptimizedCompositeOpOver128(const KoColorSpace* cs)
        : KoCompositeOp(cs, COMPOSITE_OVER, KoCompositeOp::categoryMix()) {}

    using KoCompositeOp::composite;

    virtual void composite(const KoCompositeOp::ParameterInfo& params) const
    {
        if(params.maskRowStart) {
            composite<true>(params);
        } else {
            composite<false>(params);
        }
    }

    template <bool haveMask>
    inline void composite(const KoCompositeOp::ParameterInfo& params) const {
        if (params.channelFlags.isEmpty() ||
            params.channelFlags == QBitArray(4, true)) {

            KoStreamedMath<_impl>::template genericComposite128<haveMask, false, OverCompositor128<float, false, true> >(params);
        } else {
            const bool allChannelsFlag =
                params.channelFlags.at(0) &&
                params.channelFlags.at(1) &&
                params.channelFlags.at(2);

            const bool alphaLocked =
                !params.channelFlags.at(3);

            if (allChannelsFlag && alphaLocked) {
                KoStreamedMath<_impl>::template genericComposite128_novector<haveMask, false, OverCompositor128<float, true, true> >(params);
            } else if (!allChannelsFlag && !alphaLocked) {
                KoStreamedMath<_impl>::template genericComposite128_novector<haveMask, false, OverCompositor128<float, false, false> >(params);
            } else /*if (!allChannelsFlag && alphaLocked) */{
                KoStreamedMath<_impl>::template genericComposite128_novector<haveMask, false, OverCompositor128<float, true, false> >(params);
            }
        }
    }
};

template<Vc::Implementation _impl>
class KoOptimizedCompositeOpOverU64 : public KoCompositeOp
{
public:
    KoOptimizedCompositeOpOverU64(const KoColorSpace* cs)
        : KoCompositeOp(cs, COMPOSITE_OVER, KoCompositeOp::categoryMix()) {}

    using KoCompositeOp::composite;

    virtual void composite(const KoCompositeOp::ParameterInfo& params) const
    {
        if(params.maskRowStart) {
            composite<true>(params);
        } else {
            composite<false>(params);
        }
    }

    template <bool haveMask>
    inline void composite(const KoCompositeOp::ParameterInfo& params) const {
        if (params.channelFlags.isEmpty() ||
            params.channelFlags == QBitArray(4, true)) {

            KoStreamedMath<_impl>::template genericComposite64<haveMask, false, OverCompositor128<quint16, false, true> >(params);
        } else {
            const bool allChannelsFlag =
                params.channelFlags.at(0) &&
                params.channelFlags.at(1) &&
                params.channelFlags.at(2);

            const bool alphaLocked =
                !params.channelFlags.at(3);

            if (allChannelsFlag && alphaLocked) {
                KoStreamedMath<_impl>::template genericComposite64_novector<haveMask, false, OverCompositor128<quint16, true, true> >(params);
            } else if (!allChannelsFlag && !alphaLocked) {
                KoStreamedMath<_impl>::template genericComposite64_novector<haveMask, false, OverCompositor128<quint16, false, false> >(params);
            } else /*if (!allChannelsFlag && alphaLocked) */{
                KoStreamedMath<_impl>::template genericComposite64_novector<haveMask, false, OverCompositor128<quint16, true, false> >(params);
            }
        }
    }
};

#endif // KOOPTIMIZEDCOMPOSITEOPOVER128_H_
