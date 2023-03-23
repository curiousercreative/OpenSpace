/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2023                                                               *
 *                                                                                       *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this  *
 * software and associated documentation files (the "Software"), to deal in the Software *
 * without restriction, including without limitation the rights to use, copy, modify,    *
 * merge, publish, distribute, sublicense, and/or sell copies of the Software, and to    *
 * permit persons to whom the Software is furnished to do so, subject to the following   *
 * conditions:                                                                           *
 *                                                                                       *
 * The above copyright notice and this permission notice shall be included in all copies *
 * or substantial portions of the Software.                                              *
 *                                                                                       *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,   *
 * INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A         *
 * PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT    *
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF  *
 * CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE  *
 * OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                                         *
 ****************************************************************************************/

#include <modules/video/include/renderablevideoplane.h>

#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>

namespace {
    struct [[codegen::Dictionary(RenderableVideoPlane)]] Parameters {

    };
#include "renderablevideoplane_codegen.cpp"
} // namespace

namespace openspace {

documentation::Documentation RenderableVideoPlane::Documentation() {
    return codegen::doc<Parameters>("renderable_video_plane");
}

RenderableVideoPlane::RenderableVideoPlane(const ghoul::Dictionary& dictionary)
    : RenderablePlane(dictionary)
    , _videoPlayer(dictionary)
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    addPropertySubOwner(_videoPlayer);
}

void RenderableVideoPlane::initializeGL() {
    RenderablePlane::initializeGL();
    _videoPlayer.initialize();
}

void RenderableVideoPlane::deinitializeGL() {
    _videoPlayer.destroy();
    RenderablePlane::deinitializeGL();
}

bool RenderableVideoPlane::isReady() const {
    return RenderablePlane::isReady() && _videoPlayer.isInitialized();
}

void RenderableVideoPlane::render(const RenderData& data, RendererTasks& rendererTask) {
    if (!_videoPlayer.isInitialized()) {
        return;
    }

    RenderablePlane::render(data, rendererTask);
}

void RenderableVideoPlane::update(const UpdateData& data) {
    _videoPlayer.update();

    if (!_videoPlayer.isInitialized()) {
        return;
    }

    RenderablePlane::update(data);
}

void RenderableVideoPlane::bindTexture() {
    _videoPlayer.frameTexture()->bind();
}

} // namespace openspace
