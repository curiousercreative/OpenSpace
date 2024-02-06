/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2024                                                               *
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

#include <modules/exoplanets/rendering/renderableorbitdisc.h>

#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>
#include <openspace/engine/globals.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scene/scenegraphnode.h>
#include <openspace/scene/scene.h>
#include <openspace/util/distanceconstants.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/opengl/openglstatecache.h>
#include <ghoul/opengl/programobject.h>
#include <ghoul/opengl/textureunit.h>
#include <filesystem>
#include <optional>

namespace {
    constexpr std::array<const char*, 7> UniformNames = {
        "modelViewProjectionTransform", "offset", "opacity",
        "discTexture", "eccentricity", "semiMajorAxis", "multiplyColor"
    };

    constexpr openspace::properties::Property::PropertyInfo TextureInfo = {
        "Texture",
        "Texture",
        "This value is the path to a texture on disk that contains a one-dimensional "
        "texture which is used for the color",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo SizeInfo = {
        "Size",
        "Size",
        "This value specifies the semi-major axis of the orbit, in meter",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo EccentricityInfo = {
        "Eccentricity",
        "Eccentricity",
        "This value determines the eccentricity, that is the deviation from a perfect "
        "sphere, for this orbit",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo OffsetInfo = {
        "Offset",
        "Offset",
        "This property determines the width of the disc. The values specify the lower "
        "and upper deviation from the semi major axis, respectively. The values are "
        "relative to the size of the semi-major axis. That is, 0 means no deviation "
        "from the semi-major axis and 1 is a whole semi-major axis's worth of deviation",
        openspace::properties::Property::Visibility::AdvancedUser
    };

    constexpr openspace::properties::Property::PropertyInfo MultiplyColorInfo = {
        "MultiplyColor",
        "Multiply Color",
        "If set, the disc's texture is multiplied with this color. Useful for applying a "
        "color grayscale images",
        // @VISIBILITY(1.5)
        openspace::properties::Property::Visibility::NoviceUser
    };

    struct [[codegen::Dictionary(RenderableOrbitDisc)]] Parameters {
        // [[codegen::verbatim(TextureInfo.description)]]
        std::filesystem::path texture;

        // [[codegen::verbatim(SizeInfo.description)]]
        float size;

        // [[codegen::verbatim(EccentricityInfo.description)]]
        float eccentricity;

        // [[codegen::verbatim(OffsetInfo.description)]]
        std::optional<glm::vec2> offset;

        // [[codegen::verbatim(MultiplyColorInfo.description)]]
        std::optional<glm::vec3> multiplyColor [[codegen::color()]];
    };
#include "renderableorbitdisc_codegen.cpp"
} // namespace

namespace openspace {

documentation::Documentation RenderableOrbitDisc::Documentation() {
    return codegen::doc<Parameters>("exoplanets_renderableorbitdisc");
}

RenderableOrbitDisc::RenderableOrbitDisc(const ghoul::Dictionary& dictionary)
    : Renderable(dictionary)
    , _texturePath(TextureInfo)
    , _size(SizeInfo, 1.f, 0.f, 3.0e12f)
    , _eccentricity(EccentricityInfo, 0.f, 0.f, 1.f)
    , _offset(OffsetInfo, glm::vec2(0.f), glm::vec2(0.f), glm::vec2(1.f))
    , _multiplyColor(MultiplyColorInfo, glm::vec3(1.f), glm::vec3(0.f), glm::vec3(1.f))
{
    const Parameters p = codegen::bake<Parameters>(dictionary);

    _offset = p.offset.value_or(_offset);
    _offset.onChange([this]() { _planeIsDirty = true; });
    addProperty(_offset);

    _size = p.size;
    _size.onChange([this]() { _planeIsDirty = true; });
    addProperty(_size);

    setBoundingSphere(_size + _offset.value().y * _size);

    _texturePath = p.texture.string();
    _texturePath.onChange([this]() { _texture->loadFromFile(_texturePath.value()); });
    addProperty(_texturePath);

    _multiplyColor = p.multiplyColor.value_or(_multiplyColor);
    _multiplyColor.setViewOption(properties::Property::ViewOptions::Color);
    addProperty(_multiplyColor);

    _eccentricity = p.eccentricity;
    _eccentricity.onChange([this]() { _planeIsDirty = true; });
    addProperty(_eccentricity);

    addProperty(Fadeable::_opacity);
}

bool RenderableOrbitDisc::isReady() const {
    return _shader && _texture && _plane;
}

void RenderableOrbitDisc::initialize() {
    _texture = std::make_unique<TextureComponent>(1);
    _texture->setFilterMode(ghoul::opengl::Texture::FilterMode::AnisotropicMipMap);
    _texture->setWrapping(ghoul::opengl::Texture::WrappingMode::ClampToEdge);
    _plane = std::make_unique<PlaneGeometry>(planeSize());
}

void RenderableOrbitDisc::initializeGL() {
    _shader = global::renderEngine->buildRenderProgram(
        "OrbitDiscProgram",
        absPath("${BASE}/modules/exoplanets/shaders/orbitdisc_vs.glsl"),
        absPath("${BASE}/modules/exoplanets/shaders/orbitdisc_fs.glsl")
    );

    ghoul::opengl::updateUniformLocations(*_shader, _uniformCache, UniformNames);

    _texture->loadFromFile(_texturePath.value());
    _texture->uploadToGpu();

    _plane->initialize();
}

void RenderableOrbitDisc::deinitializeGL() {
    _plane->deinitialize();
    _plane = nullptr;
    _texture = nullptr;

    global::renderEngine->removeRenderProgram(_shader.get());
    _shader = nullptr;
}

void RenderableOrbitDisc::render(const RenderData& data, RendererTasks&) {
    _shader->activate();

    _shader->setUniform(
        _uniformCache.modelViewProjection,
        glm::mat4(calcModelViewProjectionTransform(data))
    );
    _shader->setUniform(_uniformCache.offset, _offset);
    _shader->setUniform(_uniformCache.opacity, opacity());
    _shader->setUniform(_uniformCache.eccentricity, _eccentricity);
    _shader->setUniform(_uniformCache.semiMajorAxis, _size);
    _shader->setUniform(_uniformCache.multiplyColor, _multiplyColor);

    ghoul::opengl::TextureUnit unit;
    unit.activate();
    _texture->bind();
    _shader->setUniform(_uniformCache.texture, unit);

    glEnablei(GL_BLEND, 0);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(false);
    glDisable(GL_CULL_FACE);

    _plane->render();

    _shader->deactivate();

    // Restores GL State
    global::renderEngine->openglStateCache().resetBlendState();
    global::renderEngine->openglStateCache().resetDepthState();
    global::renderEngine->openglStateCache().resetPolygonAndClippingState();
}

void RenderableOrbitDisc::update(const UpdateData&) {
    if (_shader->isDirty()) {
        _shader->rebuildFromFile();
        ghoul::opengl::updateUniformLocations(*_shader, _uniformCache, UniformNames);
    }

    if (_planeIsDirty) {
        _plane->updateSize(planeSize());
        _planeIsDirty = false;
    }

    _texture->update();
}

float RenderableOrbitDisc::planeSize() const {
    float maxRadius = _size + _offset.value().y * _size;
    maxRadius *= (1.f + _eccentricity);
    return maxRadius;
}

} // namespace openspace
