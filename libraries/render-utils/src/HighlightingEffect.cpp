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

// location of color uniform in shader
int colorBufferSlot = 0;

HighlightingEffect::HighlightingEffect() {
    Parameters parameters;
    _colorBuffer = gpu::BufferView(std::make_shared<gpu::Buffer>(sizeof(Parameters), (const gpu::Byte*) &parameters));
}

void HighlightingEffect::init() {
    _colorBuffer.edit<Parameters>()._color = glm::vec4(1.0, 0.0, 1.0, 1.0);

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

    _outlineShapePlumber = std::make_shared<render::ShapePlumber>();
    {
        auto state = std::make_shared<gpu::State>();
        state->setCullMode(gpu::State::CULL_BACK);
        state->setColorWriteMask(true, true, true, true);

        // outline is a geometry redraw over the top.  depth TEST but don't write.
        // only draw pixels in front of depth buf.
        // if we are drawing a second pass on top of this with the fully lit geom, then we would not need to depth test
        state->setDepthTest(true, false, gpu::LESS); 
//        state->setDepthTest(false, false, gpu::LESS_EQUAL);

        auto modelVertex = gpu::Shader::createVertex(std::string(model_shadow_vert));
        auto modelPixel = gpu::Shader::createPixel(std::string(Solid_frag));
        gpu::ShaderPointer modelProgram = gpu::Shader::createProgram(modelVertex, modelPixel);
        _outlineShapePlumber->addPipeline(
            render::ShapeKey::Filter::Builder().withoutSkinned(),
            modelProgram, state);

        auto skinVertex = gpu::Shader::createVertex(std::string(skin_model_shadow_vert));
        auto skinPixel = gpu::Shader::createPixel(std::string(Solid_frag));
        gpu::ShaderPointer skinProgram = gpu::Shader::createProgram(skinVertex, skinPixel);
        _outlineShapePlumber->addPipeline(
            render::ShapeKey::Filter::Builder().withSkinned(),
            skinProgram, state);
    }

}

void HighlightingEffect::drawHighlightedItems(RenderArgs* args, const render::SceneContextPointer& sceneContext, const render::ItemBounds& inItems) {
    // for now just highlight a single item.
    auto& scene = sceneContext->_scene;
    auto& item = scene->getItem(inItems[0].id);
    // if a set of items, group them so that skinned and unskinned are done in two sets.


    auto outlinePipeline = _outlineShapePlumber->pickPipeline(args, render::ShapeKey());
    auto outlineSkinnedPipeline = _outlineShapePlumber->pickPipeline(args, render::ShapeKey::Builder().withSkinned());

    if (item.getShapeKey().isSkinned()) {
        args->_pipeline = outlineSkinnedPipeline;
        args->_batch->setPipeline(outlineSkinnedPipeline->pipeline);
    }
    else {
        args->_pipeline = outlinePipeline;
        args->_batch->setPipeline(outlinePipeline->pipeline);
    }
    args->_batch->setUniformBuffer(colorBufferSlot, _colorBuffer);
    item.render(args);

    args->_pipeline = nullptr;
}

void HighlightingEffect::run(const render::SceneContextPointer& sceneContext, const render::RenderContextPointer& renderContext, const render::ItemBounds& inItems) {
    // lazy init
    if (!_outlineShapePlumber) {
        init();
    }
    // bail out if no items!
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

        glm::mat4 projMat;
        Transform viewMat;
        args->_viewFrustum->evalProjectionMatrix(projMat);
        args->_viewFrustum->evalViewTransform(viewMat);

        // we will do a 2D post projection scaling in screen space on the underneath (outline) pass.
        const float thickness = 12.0f; // move this to config?
        float a = 1.0 + thickness / args->_viewport.z;
        float b = 1.0 + thickness / args->_viewport.w;
        Transform postProjScaling;
        postProjScaling.setScale(glm::vec3(a, b, 1.0));

        batch.setProjectionTransform(postProjScaling.getMatrix() * projMat);
        batch.setViewTransform(viewMat);


        drawHighlightedItems(args, sceneContext, inItems);

        args->_batch = nullptr;
    });

}

void HighlightingEffect::configure(const Config& config) {
}

