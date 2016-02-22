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
#include <render/DrawTask.h>

class HighlightingEffect {
public:
	HighlightingEffect();

    using Config = render::Job::Config;
    using JobModel = render::Job::ModelI<HighlightingEffect, render::ItemBounds, Config>;

    void configure(const Config& config);
    void run(const render::SceneContextPointer& sceneContext, const render::RenderContextPointer& renderContext, const render::ItemBounds& inItems);

private:
    render::ShapePlumberPointer _outlineShapePlumber;

    // Class describing the uniform buffer with all the parameters common to the tone mapping shaders
    class Parameters {
    public:
        glm::vec4 _color;

        Parameters() {}
    };
    gpu::BufferView _colorBuffer;


    void init();
    void drawHighlightedItems(RenderArgs* args, const render::SceneContextPointer& sceneContext, const render::ItemBounds& inItems);

};


#endif // hifi_HighlightingEffect_h
