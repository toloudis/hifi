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
#include "GeometryCache.h"

#include "standardTransformPNTC_vert.h"

// location of color uniform in shader
int colorBufferSlot = 0;

HighlightingEffect::HighlightingEffect() {
	Parameters parameters;
	_colorBuffer = gpu::BufferView(std::make_shared<gpu::Buffer>(sizeof(Parameters), (const gpu::Byte*) &parameters));
	_colorBufferOutline = gpu::BufferView(std::make_shared<gpu::Buffer>(sizeof(Parameters), (const gpu::Byte*) &parameters));
}

void HighlightingEffect::init() {
	_colorBuffer.edit<Parameters>()._color = glm::vec4(1.0, 0.0, 1.0, 1.0);
	_colorBufferOutline.edit<Parameters>()._color = glm::vec4(0.0, 1.0, 0.0, 1.0);

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
    auto solidVS = gpu::ShaderPointer(gpu::Shader::createVertex(std::string(standardTransformPNTC_vert)));
    auto solidPS = gpu::ShaderPointer(gpu::Shader::createPixel(std::string(Solid_frag)));
    auto solidProgram = gpu::ShaderPointer(gpu::Shader::createProgram(solidVS, solidPS));
    gpu::Shader::BindingSet slotBindings;
	slotBindings.insert(gpu::Shader::Binding(std::string("colorParamsBuffer"), colorBufferSlot));
	gpu::Shader::makeProgram(*solidProgram, slotBindings);


	// set up gl state
	auto lineState = std::make_shared<gpu::State>();
	lineState->setColorWriteMask(true, true, true, true);
	// if outline should be over the top of everything, then don't depth test and don't depth write.
	lineState->setDepthTest(false, false, gpu::LESS_EQUAL);
	// could use fill mode now instead, since we are going to achieve "thickness" with a 2d post projection scaling
	//lineState->setFillMode(gpu::State::FILL_LINE);

	_linePassPipeline = gpu::PipelinePointer(gpu::Pipeline::create(solidProgram, lineState));


	// set up gl state
	auto drawState = std::make_shared<gpu::State>();
	drawState->setColorWriteMask(true, true, true, true);
	// if outline should be over the top of everything, then don't depth test and don't depth write.
	drawState->setDepthTest(false, false, gpu::LESS_EQUAL);
	drawState->setFillMode(gpu::State::FILL_FACE);

	_drawPipeline = gpu::PipelinePointer(gpu::Pipeline::create(solidProgram, drawState));
}

void HighlightingEffect::render(RenderArgs* args) {
    // lazy init
    if (!_linePassPipeline) {
        init();
    }

    // draw the thing.
    
	auto framebufferCache = DependencyManager::get<FramebufferCache>();
	auto geometryCache = DependencyManager::get<GeometryCache>();
	gpu::doInBatch(args->_context, [&](gpu::Batch& batch) {
        auto destFbo = framebufferCache->getPrimaryFramebuffer();
        batch.setFramebuffer(destFbo);

		batch.setViewportTransform(args->_viewport);
		
        glm::mat4 projMat;
        Transform viewMat;
        args->_viewFrustum->evalProjectionMatrix(projMat);
		// don't eval this, just draw the object in the center of the view
        //args->_viewFrustum->evalViewTransform(viewMat);
		
		// we will do a 2D post projection scaling in screen space on the underneath (outline) pass.
		const float thickness = 12.0f;
		float a = 1.0 + thickness / args->_viewport.z;
		float b = 1.0 + thickness / args->_viewport.w;
		Transform postProjScaling;
		postProjScaling.setScale(glm::vec3(a, b, 1.0));
		
        batch.setViewTransform(viewMat);

		// hardcode a simple rotated transform
		Transform model;
		model.setTranslation(glm::vec3(0.0, 0.0, -4.0));
		model.setScale(glm::vec3(1.0, 1.0, 1.0));
		model.setRotation(Transform::Quat(glm::vec3(PI*0.3f, PI*0.3f, 0.0f)));
		batch.setModelTransform(model);


		batch.setProjectionTransform(postProjScaling.getMatrix() * projMat);
		batch.setPipeline(_linePassPipeline);
		batch.setUniformBuffer(colorBufferSlot, _colorBuffer);
		geometryCache->renderCube(batch);

		batch.setProjectionTransform(projMat);
		batch.setPipeline(_drawPipeline);
		batch.setUniformBuffer(colorBufferSlot, _colorBufferOutline);
		geometryCache->renderCube(batch);

		static const auto triCount = geometryCache->getCubeTriangleCount();
		// we drew twice so mul by 2
        args->_details._trianglesRendered += (int)triCount * 2;


    });





}