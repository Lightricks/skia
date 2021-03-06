/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef GrImageTextureMaker_DEFINED
#define GrImageTextureMaker_DEFINED

#include "include/core/SkImage.h"
#include "src/gpu/GrTextureMaker.h"

class SkImage_Lazy;
class SkImage_GpuYUVA;

/** This class manages the conversion of generator-backed images to GrTextures. If the caching hint
    is kAllow the image's ID is used for the cache key. */
class GrImageTextureMaker : public GrTextureMaker {
public:
    GrImageTextureMaker(GrRecordingContext* context, const SkImage* client,
                        SkImage::CachingHint chint, bool useDecal = false);

private:
    GrSurfaceProxyView refOriginalTextureProxyView(bool willBeMipped,
                                                   AllowedTexGenType onlyIfFast) override;

    void makeMipMappedKey(GrUniqueKey* mipMappedKey) override;
    void didCacheMipMappedCopy(const GrUniqueKey& mipMappedKey, uint32_t contextUniqueID) override {
    }

    const SkImage_Lazy*     fImage;
    GrUniqueKey             fOriginalKey;
    SkImage::CachingHint    fCachingHint;

    typedef GrTextureMaker INHERITED;
};

/** This class manages the conversion of generator-backed YUVA images to GrTextures. */
class GrYUVAImageTextureMaker : public GrTextureMaker {
public:
    GrYUVAImageTextureMaker(GrContext* context, const SkImage* client, bool useDecal = false);

    // This could be made more nuanced and compare all of the texture proxy resolutions, but
    // it's probably not worth the effort.
    bool hasMixedResolutions() const override { return true; }
protected:
    // TODO: consider overriding this, for the case where the underlying generator might be
    //       able to efficiently produce a "stretched" texture natively (e.g. picture-backed)
    //          GrTexture* generateTextureForParams(const CopyParams&) override;
    GrSurfaceProxyView refOriginalTextureProxyView(bool willBeMipped,
                                                   AllowedTexGenType onlyIfFast) override;

    void makeMipMappedKey(GrUniqueKey* mipMappedKey) override;
    void didCacheMipMappedCopy(const GrUniqueKey& mipMappedKey, uint32_t contextUniqueID) override {
    }

    std::unique_ptr<GrFragmentProcessor> createFragmentProcessor(
        const SkMatrix& textureMatrix,
        const SkRect& constraintRect,
        FilterConstraint filterConstraint,
        bool coordsLimitedToConstraintRect,
        const GrSamplerState::Filter* filterOrNullForBicubic) override;

private:
    const SkImage_GpuYUVA*  fImage;
    GrUniqueKey             fOriginalKey;

    typedef GrTextureMaker INHERITED;
};

#endif
