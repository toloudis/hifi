//
//  HighlightingEffect.h
//  libraries/render-utils/src
//
//  Created by Dan Toloudis on 2/7/2016.
//  Copyright 2015 High Fidelity, Inc.
//
//  Distributed under the Apache License, Version 2.0.
//  See the accompanying file LICENSE or http://www.apache.org/licenses/LICENSE-2.0.html
//

#ifndef hifi_HighlightingEffect_h
#define hifi_HighlightingEffect_h

#include <DependencyManager.h>
#include <NumericalConstants.h>

#include <gpu/Resource.h>
#include <gpu/Pipeline.h>

class RenderArgs;

class HighlightingEffect {
public:
    HighlightingEffect();
    virtual ~HighlightingEffect() {}

    void render(RenderArgs* args);

private:

	gpu::PipelinePointer _linePassPipeline;
	gpu::PipelinePointer _drawPipeline;

	// Class describing the uniform buffer with all the parameters common to the tone mapping shaders
	class Parameters {
	public:
		glm::vec4 _color;

		Parameters() {}
	};
	gpu::BufferView _colorBuffer;
	gpu::BufferView _colorBufferOutline;


    void init();
};

#endif // hifi_HighlightingEffect_h
