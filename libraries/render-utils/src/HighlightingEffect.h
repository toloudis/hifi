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


class HighlightingEffectConfig : public render::Job::Config {
    Q_OBJECT
    Q_PROPERTY(float lineThickness MEMBER lineThickness WRITE setLineThickness)

public:
    // TODO FIXME: disabled! enable when entities need highlighting
    HighlightingEffectConfig() : render::Job::Config(false) {}

    // pixels
    float lineThickness{ 4.0 };
    glm::vec4 color = glm::vec4(1.0f, 0.0f, 1.0f, 1.0f);
public slots:
    void setLineThickness(float thickness) { lineThickness = thickness; emit dirty(); }

signals:
    void dirty();
};


class HighlightingEffect {
public:
    HighlightingEffect();

    using Config = HighlightingEffectConfig;
    using JobModel = render::Job::ModelI<HighlightingEffect, render::ItemBounds, Config>;

    void configure(const Config& config);
    void run(const render::SceneContextPointer& sceneContext, const render::RenderContextPointer& renderContext, const render::ItemBounds& inItems);

private:
    // plumber lets me run separate shaders for skinned and non-skinned geometry
    render::ShapePlumberPointer _fillStencilShapePlumber;
    render::ShapePlumberPointer _drawShapePlumber;

    float _lineThickness;

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
