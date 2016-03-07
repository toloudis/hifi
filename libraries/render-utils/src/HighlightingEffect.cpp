//
//  HighlightingEffect.cpp
//  libraries/render-utils/src
//
//  Created by Dan Toloudis on 2/7/2016.
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#include "HighlightingEffect.h"

#include <gpu/Context.h>
#include <gpu/StandardShaderLib.h>

#include <RenderArgs.h>
#include <ViewFrustum.h>

#include "FramebufferCache.h"

#include "model_shadow_vert.h"
#include "skin_model_shadow_vert.h"
#include "model_outline_vert.h"
#include "skin_model_outline_vert.h"
#include "drawOpaqueStencil_frag.h"

// location of color uniform in shader
int colorBufferSlot = 0;
static const char* lineThicknessName = "lineThickness";
// location of line thickness uniform in shader
gpu::int32 lineThicknessSlotSkinned = 0;
gpu::int32 lineThicknessSlotUnskinned = 0;

HighlightingEffect::HighlightingEffect() {
    Parameters parameters;
    _colorBuffer = gpu::BufferView(std::make_shared<gpu::Buffer>(sizeof(Parameters), (const gpu::Byte*) &parameters));
    _colorBuffer.edit<Parameters>()._color = glm::vec4(1.0, 0.0, 1.0, 1.0);
    _lineThickness = 4.0f;
}

void HighlightingEffect::init() {
    // prep shader
    const char Solid_frag[] = R"SCRIBE(#version 410 core

        struct ColorParams {
            vec4 color;
        };

        uniform colorParamsBuffer {
            ColorParams params;
        };

        out vec4 outFragColor;
        void main(void) {
            outFragColor = params.color;
        }
        
    )SCRIBE";

    _fillStencilShapePlumber = std::make_shared<render::ShapePlumber>();
    _drawShapePlumber = std::make_shared<render::ShapePlumber>();
    {
        // some "random" value assumed to be unused in the current stencil buf
        const gpu::int8 STENCIL_KEY = 0x17;

        auto modelVertex = gpu::Shader::createVertex(std::string(model_shadow_vert));
        auto skinVertex = gpu::Shader::createVertex(std::string(skin_model_shadow_vert));

        ////////////////
        // first pass fill stencil buf

        auto fillStencilState = std::make_shared<gpu::State>();
        fillStencilState->setCullMode(gpu::State::CULL_BACK);
        fillStencilState->setColorWriteMask(false, false, false, false);
        fillStencilState->setDepthTest(false, false, gpu::LESS_EQUAL);
        fillStencilState->setStencilTest(true, 0xFF, gpu::State::StencilTest(STENCIL_KEY, 0xFF, gpu::ALWAYS, gpu::State::STENCIL_OP_REPLACE, gpu::State::STENCIL_OP_REPLACE, gpu::State::STENCIL_OP_REPLACE));

        auto fillStencilPixel = gpu::Shader::createPixel(std::string(drawOpaqueStencil_frag));

        gpu::ShaderPointer modelStencilProgram = gpu::Shader::createProgram(modelVertex, fillStencilPixel);
        _fillStencilShapePlumber->addPipeline(
            render::ShapeKey::Filter::Builder().withoutSkinned(),
            modelStencilProgram, fillStencilState);

        gpu::ShaderPointer skinStencilProgram = gpu::Shader::createProgram(skinVertex, fillStencilPixel);
        _fillStencilShapePlumber->addPipeline(
            render::ShapeKey::Filter::Builder().withSkinned(),
            skinStencilProgram, fillStencilState);

        //////////////////
        // second pass draw anywhere the stencil buf is not set, with a scaled version of the same objects.

        auto modelVertexOutline = gpu::Shader::createVertex(std::string(model_outline_vert));
        auto skinVertexOutline = gpu::Shader::createVertex(std::string(skin_model_outline_vert));

        auto drawState = std::make_shared<gpu::State>();
        drawState->setCullMode(gpu::State::CULL_BACK);
        drawState->setColorWriteMask(true, true, true, true);
        // always draw.  outline is on top of everything even if object is depth culled
        drawState->setDepthTest(false, false, gpu::LESS_EQUAL);
        drawState->setStencilTest(true, 0xFF, gpu::State::StencilTest(STENCIL_KEY, 0xFF, gpu::NOT_EQUAL, gpu::State::STENCIL_OP_KEEP, gpu::State::STENCIL_OP_KEEP, gpu::State::STENCIL_OP_KEEP));


        auto modelPixel = gpu::Shader::createPixel(std::string(Solid_frag));
        gpu::ShaderPointer modelProgram = gpu::Shader::createProgram(modelVertexOutline, modelPixel);
        _drawShapePlumber->addPipeline(
            render::ShapeKey::Filter::Builder().withoutSkinned(),
            modelProgram, drawState);

        auto skinPixel = gpu::Shader::createPixel(std::string(Solid_frag));
        gpu::ShaderPointer skinProgram = gpu::Shader::createProgram(skinVertexOutline, skinPixel);
        _drawShapePlumber->addPipeline(
            render::ShapeKey::Filter::Builder().withSkinned(),
            skinProgram, drawState);


        lineThicknessSlotUnskinned = modelProgram->getUniforms().findLocation(lineThicknessName);
        lineThicknessSlotSkinned = skinProgram->getUniforms().findLocation(lineThicknessName);
    }



}

void HighlightingEffect::drawHighlightedItems(RenderArgs* args, const render::SceneContextPointer& sceneContext, const render::ItemBounds& inItems) {
    // TODO FIXME This should operate with a list of items that want highlighting.  
    // for now just highlight a single item.
    auto& scene = sceneContext->_scene;
    auto& item = scene->getItem(inItems[0].id);
    // if a set of items, group them so that skinned and unskinned are done in two sets.

    // PASS 0: fill stencil with object at normal size
    auto pipeline0 = _fillStencilShapePlumber->pickPipeline(args, render::ShapeKey());
    auto pipeline0Skinned = _fillStencilShapePlumber->pickPipeline(args, render::ShapeKey::Builder().withSkinned());
    
    if (item.getShapeKey().isSkinned()) {
        args->_pipeline = pipeline0Skinned;
        args->_batch->setPipeline(pipeline0Skinned->pipeline);
    }
    else {
        args->_pipeline = pipeline0;
        args->_batch->setPipeline(pipeline0->pipeline);
    }
    item.render(args);

    // PASS 1: draw object scaled but only draw where stencil not set.
    auto pipeline1 = _drawShapePlumber->pickPipeline(args, render::ShapeKey());
    auto pipeline1Skinned = _drawShapePlumber->pickPipeline(args, render::ShapeKey::Builder().withSkinned());

    if (item.getShapeKey().isSkinned()) {
        args->_pipeline = pipeline1Skinned;
        args->_batch->setPipeline(pipeline1Skinned->pipeline);
        args->_batch->_glUniform(lineThicknessSlotSkinned, _lineThickness);
    }
    else {
        args->_pipeline = pipeline1;
        args->_batch->setPipeline(pipeline1->pipeline);
        args->_batch->_glUniform(lineThicknessSlotUnskinned, _lineThickness);
    }

    // set the outline color.
    args->_batch->setUniformBuffer(colorBufferSlot, _colorBuffer);
    item.render(args);

    args->_pipeline = nullptr;
}

void HighlightingEffect::run(const render::SceneContextPointer& sceneContext, const render::RenderContextPointer& renderContext, const render::ItemBounds& inItems) {
    // lazy init
    if (!_fillStencilShapePlumber) {
        init();
    }
    // TODO FIXME This should operate with a list of items that want highlighting.  Currently, bail out if no items!
    if (inItems.size() < 1) {
        return;
    }

    assert(renderContext->args);
    assert(renderContext->args->_viewFrustum);

    auto config = std::static_pointer_cast<Config>(renderContext->jobConfig);

    RenderArgs* args = renderContext->args;

    // draw the thing.

    auto framebufferCache = DependencyManager::get<FramebufferCache>();

    gpu::doInBatch(args->_context, [&](gpu::Batch& batch) {
        args->_batch = &batch;

        batch.enableStereo(false);
        auto destFbo = framebufferCache->getPrimaryFramebuffer();
        batch.setFramebuffer(destFbo);

        batch.setViewportTransform(args->_viewport);
        batch.setStateScissorRect(args->_viewport);

        Transform viewMat;
        args->_viewFrustum->evalViewTransform(viewMat);
        batch.setViewTransform(viewMat);

        glm::mat4 projMat;
        args->_viewFrustum->evalProjectionMatrix(projMat);
        batch.setProjectionTransform(projMat);

        drawHighlightedItems(args, sceneContext, inItems);

        args->_batch = nullptr;
    });

}

void HighlightingEffect::configure(const Config& config) {
    _lineThickness = config.lineThickness;
    _colorBuffer.edit<Parameters>()._color = config.color;
}

