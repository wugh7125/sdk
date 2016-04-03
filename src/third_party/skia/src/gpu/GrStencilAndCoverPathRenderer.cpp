
/*
 * Copyright 2012 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include "GrStencilAndCoverPathRenderer.h"
#include "GrContext.h"
#include "GrDrawTargetCaps.h"
#include "GrGpu.h"
#include "GrPath.h"
#include "SkStrokeRec.h"

GrPathRenderer* GrStencilAndCoverPathRenderer::Create(GrContext* context) {
    SkASSERT(NULL != context);
    SkASSERT(NULL != context->getGpu());
    if (context->getGpu()->caps()->pathStencilingSupport()) {
        return SkNEW_ARGS(GrStencilAndCoverPathRenderer, (context->getGpu()));
    } else {
        return NULL;
    }
}

GrStencilAndCoverPathRenderer::GrStencilAndCoverPathRenderer(GrGpu* gpu) {
    SkASSERT(gpu->caps()->pathStencilingSupport());
    fGpu = gpu;
    gpu->ref();
}

GrStencilAndCoverPathRenderer::~GrStencilAndCoverPathRenderer() {
    fGpu->unref();
}

bool GrStencilAndCoverPathRenderer::canDrawPath(const SkPath& path,
                                                const SkStrokeRec& stroke,
                                                const GrDrawTarget* target,
                                                bool antiAlias) const {
    return stroke.isFillStyle() &&
           !antiAlias && // doesn't do per-path AA, relies on the target having MSAA
           target->getDrawState().getStencil().isDisabled();
}

GrPathRenderer::StencilSupport GrStencilAndCoverPathRenderer::onGetStencilSupport(
                                                        const SkPath&,
                                                        const SkStrokeRec& ,
                                                        const GrDrawTarget*) const {
    return GrPathRenderer::kStencilOnly_StencilSupport;
}

void GrStencilAndCoverPathRenderer::onStencilPath(const SkPath& path,
                                                  const SkStrokeRec& stroke,
                                                  GrDrawTarget* target) {
    SkASSERT(!path.isInverseFillType());
    SkAutoTUnref<GrPath> p(fGpu->createPath(path));
    target->stencilPath(p, stroke, path.getFillType());
}

bool GrStencilAndCoverPathRenderer::onDrawPath(const SkPath& path,
                                               const SkStrokeRec& stroke,
                                               GrDrawTarget* target,
                                               bool antiAlias) {
    SkASSERT(!antiAlias);
    SkASSERT(!stroke.isHairlineStyle());

    GrDrawState* drawState = target->drawState();
    SkASSERT(drawState->getStencil().isDisabled());

    SkAutoTUnref<GrPath> p(fGpu->createPath(path));

    SkPath::FillType nonInvertedFill = SkPath::ConvertToNonInverseFillType(path.getFillType());
    target->stencilPath(p, stroke, nonInvertedFill);

    // TODO: Use built in cover operation rather than a rect draw. This will require making our
    // fragment shaders be able to eat varyings generated by a matrix.

    // fill the path, zero out the stencil
    SkRect bounds = p->getBounds();
    SkScalar bloat = drawState->getViewMatrix().getMaxStretch() * SK_ScalarHalf;
    GrDrawState::AutoViewMatrixRestore avmr;

    if (nonInvertedFill == path.getFillType()) {
        GR_STATIC_CONST_SAME_STENCIL(kStencilPass,
            kZero_StencilOp,
            kZero_StencilOp,
            kNotEqual_StencilFunc,
            0xffff,
            0x0000,
            0xffff);
        *drawState->stencil() = kStencilPass;
    } else {
        GR_STATIC_CONST_SAME_STENCIL(kInvertedStencilPass,
            kZero_StencilOp,
            kZero_StencilOp,
            // We know our rect will hit pixels outside the clip and the user bits will be 0
            // outside the clip. So we can't just fill where the user bits are 0. We also need to
            // check that the clip bit is set.
            kEqualIfInClip_StencilFunc,
            0xffff,
            0x0000,
            0xffff);
        SkMatrix vmi;
        bounds.setLTRB(0, 0,
                       SkIntToScalar(drawState->getRenderTarget()->width()),
                       SkIntToScalar(drawState->getRenderTarget()->height()));
        // mapRect through persp matrix may not be correct
        if (!drawState->getViewMatrix().hasPerspective() && drawState->getViewInverse(&vmi)) {
            vmi.mapRect(&bounds);
            // theoretically could set bloat = 0, instead leave it because of matrix inversion
            // precision.
        } else {
            avmr.setIdentity(drawState);
            bloat = 0;
        }
        *drawState->stencil() = kInvertedStencilPass;
    }
    bounds.outset(bloat, bloat);
    target->drawSimpleRect(bounds, NULL);
    target->drawState()->stencil()->setDisabled();
    return true;
}
