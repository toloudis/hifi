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
#include "drawOpaqueStencil_frag.h"

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

    const char model_outline_vert[] = R"SCRIBE(#version 410 core
//  Generated on Thu Feb 18 19:48:48 2016
//
//  model_shadow.vert
//  vertex shader
//
//  Created by Andrzej Kapolka on 3/24/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec4 inTexCoord0;
layout(location = 4) in vec4 inTangent;
layout(location = 5) in vec4 inSkinClusterIndex;
layout(location = 6) in vec4 inSkinClusterWeight;
layout(location = 7) in vec4 inTexCoord1;
struct TransformObject {
    mat4 _model;
    mat4 _modelInverse;
};

layout(location=15) in ivec2 _drawCallInfo;

uniform samplerBuffer transformObjectBuffer;

TransformObject getTransformObject() {
    int offset = 8 * _drawCallInfo.x;
    TransformObject object;
    object._model[0] = texelFetch(transformObjectBuffer, offset);
    object._model[1] = texelFetch(transformObjectBuffer, offset + 1);
    object._model[2] = texelFetch(transformObjectBuffer, offset + 2);
    object._model[3] = texelFetch(transformObjectBuffer, offset + 3);

    object._modelInverse[0] = texelFetch(transformObjectBuffer, offset + 4);
    object._modelInverse[1] = texelFetch(transformObjectBuffer, offset + 5);
    object._modelInverse[2] = texelFetch(transformObjectBuffer, offset + 6);
    object._modelInverse[3] = texelFetch(transformObjectBuffer, offset + 7);

    return object;
}

struct TransformCamera {
    mat4 _view;
    mat4 _viewInverse;
    mat4 _projectionViewUntranslated;
    mat4 _projection;
    mat4 _projectionInverse;
    vec4 _viewport;
};

layout(std140) uniform transformCameraBuffer {
    TransformCamera _camera;
};
TransformCamera getTransformCamera() {
    return _camera;
}



void main(void) {
    // standard transform
    TransformCamera cam = getTransformCamera();
    TransformObject obj = getTransformObject();

    vec3 normal = vec3(0.0, 0.0, 0.0);
    { // transformModelToEyeDir
        vec3 mr0 = vec3(obj._modelInverse[0].x, obj._modelInverse[1].x, obj._modelInverse[2].x);
        vec3 mr1 = vec3(obj._modelInverse[0].y, obj._modelInverse[1].y, obj._modelInverse[2].y);
        vec3 mr2 = vec3(obj._modelInverse[0].z, obj._modelInverse[1].z, obj._modelInverse[2].z);

        vec3 mvc0 = vec3(dot(cam._viewInverse[0].xyz, mr0), dot(cam._viewInverse[0].xyz, mr1), dot(cam._viewInverse[0].xyz, mr2));
        vec3 mvc1 = vec3(dot(cam._viewInverse[1].xyz, mr0), dot(cam._viewInverse[1].xyz, mr1), dot(cam._viewInverse[1].xyz, mr2));
        vec3 mvc2 = vec3(dot(cam._viewInverse[2].xyz, mr0), dot(cam._viewInverse[2].xyz, mr1), dot(cam._viewInverse[2].xyz, mr2));

        normal = vec3(dot(mvc0, inNormal.xyz), dot(mvc1, inNormal.xyz), dot(mvc2, inNormal.xyz));
    }


    { // transformModelToClipPos
        vec4 _eyepos = (obj._model * inPosition) + vec4(-inPosition.w * cam._viewInverse[3].xyz, 0.0);
        vec4 clipPos = cam._projectionViewUntranslated * _eyepos;
        clipPos.xy += normalize(vec2(normal.xy)) * (1.0/512.0);
        gl_Position = clipPos;
    }



}

)SCRIBE";

    const char skin_model_outline_vert[] = R"SCRIBE(#version 410 core
//  Generated on Thu Feb 18 19:48:48 2016
//
//  skin_model_shadow.vert
//  vertex shader
//
//  Created by Andrzej Kapolka on 3/24/14.
//  Copyright 2014 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

layout(location = 0) in vec4 inPosition;
layout(location = 1) in vec4 inNormal;
layout(location = 2) in vec4 inColor;
layout(location = 3) in vec4 inTexCoord0;
layout(location = 4) in vec4 inTangent;
layout(location = 5) in vec4 inSkinClusterIndex;
layout(location = 6) in vec4 inSkinClusterWeight;
layout(location = 7) in vec4 inTexCoord1;
struct TransformObject {
    mat4 _model;
    mat4 _modelInverse;
};

layout(location=15) in ivec2 _drawCallInfo;

uniform samplerBuffer transformObjectBuffer;

TransformObject getTransformObject() {
    int offset = 8 * _drawCallInfo.x;
    TransformObject object;
    object._model[0] = texelFetch(transformObjectBuffer, offset);
    object._model[1] = texelFetch(transformObjectBuffer, offset + 1);
    object._model[2] = texelFetch(transformObjectBuffer, offset + 2);
    object._model[3] = texelFetch(transformObjectBuffer, offset + 3);

    object._modelInverse[0] = texelFetch(transformObjectBuffer, offset + 4);
    object._modelInverse[1] = texelFetch(transformObjectBuffer, offset + 5);
    object._modelInverse[2] = texelFetch(transformObjectBuffer, offset + 6);
    object._modelInverse[3] = texelFetch(transformObjectBuffer, offset + 7);

    return object;
}

struct TransformCamera {
    mat4 _view;
    mat4 _viewInverse;
    mat4 _projectionViewUntranslated;
    mat4 _projection;
    mat4 _projectionInverse;
    vec4 _viewport;
};

layout(std140) uniform transformCameraBuffer {
    TransformCamera _camera;
};
TransformCamera getTransformCamera() {
    return _camera;
}



const int MAX_TEXCOORDS = 2;
const int MAX_CLUSTERS = 128;
const int INDICES_PER_VERTEX = 4;

layout(std140) uniform skinClusterBuffer {
    mat4 clusterMatrices[MAX_CLUSTERS];
};

void skinPosition(vec4 skinClusterIndex, vec4 skinClusterWeight, vec4 inPosition, out vec4 skinnedPosition) {
    vec4 newPosition = vec4(0.0, 0.0, 0.0, 0.0);

    for (int i = 0; i < INDICES_PER_VERTEX; i++) {
        mat4 clusterMatrix = clusterMatrices[int(skinClusterIndex[i])];
        float clusterWeight = skinClusterWeight[i];
        newPosition += clusterMatrix * inPosition * clusterWeight;
    }

    skinnedPosition = newPosition;
}

void skinPositionNormal(vec4 skinClusterIndex, vec4 skinClusterWeight, vec4 inPosition, vec3 inNormal,
                        out vec4 skinnedPosition, out vec3 skinnedNormal) {
    vec4 newPosition = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 newNormal = vec4(0.0, 0.0, 0.0, 0.0);

    for (int i = 0; i < INDICES_PER_VERTEX; i++) {
        mat4 clusterMatrix = clusterMatrices[int(skinClusterIndex[i])];
        float clusterWeight = skinClusterWeight[i];
        newPosition += clusterMatrix * inPosition * clusterWeight;
        newNormal += clusterMatrix * vec4(inNormal.xyz, 0.0) * clusterWeight;
    }

    skinnedPosition = newPosition;
    skinnedNormal = newNormal.xyz;
}

void skinPositionNormalTangent(vec4 skinClusterIndex, vec4 skinClusterWeight, vec4 inPosition, vec3 inNormal, vec3 inTangent,
                               out vec4 skinnedPosition, out vec3 skinnedNormal, out vec3 skinnedTangent) {
    vec4 newPosition = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 newNormal = vec4(0.0, 0.0, 0.0, 0.0);
    vec4 newTangent = vec4(0.0, 0.0, 0.0, 0.0);

    for (int i = 0; i < INDICES_PER_VERTEX; i++) {
        mat4 clusterMatrix = clusterMatrices[int(skinClusterIndex[i])];
        float clusterWeight = skinClusterWeight[i];
        newPosition += clusterMatrix * inPosition * clusterWeight;
        newNormal += clusterMatrix * vec4(inNormal.xyz, 0.0) * clusterWeight;
        newTangent += clusterMatrix * vec4(inTangent.xyz, 0.0) * clusterWeight;
    }

    skinnedPosition = newPosition;
    skinnedNormal = newNormal.xyz;
    skinnedTangent = newTangent.xyz;
}


void main(void) {
    vec4 position = vec4(0.0, 0.0, 0.0, 0.0);
    vec3 normal = vec3(0.0, 0.0, 0.0);
    skinPositionNormal(inSkinClusterIndex, inSkinClusterWeight, inPosition, inNormal.xyz, position, normal);

    // standard transform
    TransformCamera cam = getTransformCamera();
    TransformObject obj = getTransformObject();

    vec3 eyeNormal = vec3(0.0, 0.0, 0.0);
    { // transformModelToEyeDir
        vec3 mr0 = vec3(obj._modelInverse[0].x, obj._modelInverse[1].x, obj._modelInverse[2].x);
        vec3 mr1 = vec3(obj._modelInverse[0].y, obj._modelInverse[1].y, obj._modelInverse[2].y);
        vec3 mr2 = vec3(obj._modelInverse[0].z, obj._modelInverse[1].z, obj._modelInverse[2].z);

        vec3 mvc0 = vec3(dot(cam._viewInverse[0].xyz, mr0), dot(cam._viewInverse[0].xyz, mr1), dot(cam._viewInverse[0].xyz, mr2));
        vec3 mvc1 = vec3(dot(cam._viewInverse[1].xyz, mr0), dot(cam._viewInverse[1].xyz, mr1), dot(cam._viewInverse[1].xyz, mr2));
        vec3 mvc2 = vec3(dot(cam._viewInverse[2].xyz, mr0), dot(cam._viewInverse[2].xyz, mr1), dot(cam._viewInverse[2].xyz, mr2));

        eyeNormal = vec3(dot(mvc0, normal.xyz), dot(mvc1, normal.xyz), dot(mvc2, normal.xyz));
    }

    { // transformModelToClipPos
        vec4 _eyepos = (obj._model * position) + vec4(-position.w * cam._viewInverse[3].xyz, 0.0);

        vec4 clipPos = cam._projectionViewUntranslated * _eyepos;
        clipPos.xy += normalize(vec2(eyeNormal.xy)) * (1.0/512.0);
        gl_Position = clipPos;
    }

}

)SCRIBE";





    _fillStencilShapePlumber = std::make_shared<render::ShapePlumber>();
    _drawShapePlumber = std::make_shared<render::ShapePlumber>();
    {
        const gpu::int8 STENCIL_OPAQUE = 1;
        auto modelVertex = gpu::Shader::createVertex(std::string(model_shadow_vert));
        auto skinVertex = gpu::Shader::createVertex(std::string(skin_model_shadow_vert));

        ////////////////
        // first pass fill stencil buf

        auto fillStencilState = std::make_shared<gpu::State>();
        fillStencilState->setCullMode(gpu::State::CULL_BACK);
        fillStencilState->setColorWriteMask(false, false, false, false);
        fillStencilState->setDepthTest(false, false, gpu::LESS_EQUAL);
        fillStencilState->setStencilTest(true, 0xFF, gpu::State::StencilTest(STENCIL_OPAQUE, 0xFF, gpu::ALWAYS, gpu::State::STENCIL_OP_REPLACE, gpu::State::STENCIL_OP_REPLACE, gpu::State::STENCIL_OP_REPLACE));

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
        drawState->setStencilTest(true, 0xFF, gpu::State::StencilTest(STENCIL_OPAQUE, 0xFF, gpu::NOT_EQUAL, gpu::State::STENCIL_OP_REPLACE, gpu::State::STENCIL_OP_KEEP, gpu::State::STENCIL_OP_REPLACE));


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

    }

}

void HighlightingEffect::drawHighlightedItems(RenderArgs* args, const render::SceneContextPointer& sceneContext, const render::ItemBounds& inItems) {
    // for now just highlight a single item.
    auto& scene = sceneContext->_scene;
    auto& item = scene->getItem(inItems[0].id);
    // if a set of items, group them so that skinned and unskinned are done in two sets.


    Transform viewMat;
    args->_viewFrustum->evalViewTransform(viewMat);
    args->_batch->setViewTransform(viewMat);

    glm::mat4 projMat;
    args->_viewFrustum->evalProjectionMatrix(projMat);
    // we will do a 2D post projection scaling in screen space on the underneath (outline) pass.
//    const float thickness = 48.0f; // move this to config?
//    float a = 1.0 + thickness / args->_viewport.z;
//    float b = 1.0 + thickness / args->_viewport.w;
//    Transform postProjScaling;
//    postProjScaling.setScale(glm::vec3(a, b, 1.0));





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
    args->_batch->setProjectionTransform(projMat);
    item.render(args);

    // PASS 1: draw object scaled but only draw where stencil not set.
    auto pipeline1 = _drawShapePlumber->pickPipeline(args, render::ShapeKey());
    auto pipeline1Skinned = _drawShapePlumber->pickPipeline(args, render::ShapeKey::Builder().withSkinned());

    if (item.getShapeKey().isSkinned()) {
        args->_pipeline = pipeline1Skinned;
        args->_batch->setPipeline(pipeline1Skinned->pipeline);
    }
    else {
        args->_pipeline = pipeline1;
        args->_batch->setPipeline(pipeline1->pipeline);
    }
//    args->_batch->setProjectionTransform(postProjScaling.getMatrix() * projMat);
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

        drawHighlightedItems(args, sceneContext, inItems);

        args->_batch = nullptr;
    });

}

void HighlightingEffect::configure(const Config& config) {
}

