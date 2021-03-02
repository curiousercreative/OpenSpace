/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2021                                                               *
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

#include <modules/spacecraftinstruments/rendering/renderablemodelprojection.h>

#include <openspace/documentation/documentation.h>
#include <openspace/documentation/verifier.h>
#include <openspace/engine/globals.h>
#include <openspace/rendering/renderengine.h>
#include <openspace/scene/scenegraphnode.h>
#include <openspace/scene/scene.h>
#include <openspace/util/spicemanager.h>
#include <openspace/util/time.h>
#include <openspace/util/updatestructures.h>
#include <ghoul/filesystem/filesystem.h>
#include <ghoul/io/model/modelgeometry.h>
#include <ghoul/io/model/modelreader.h>
#include <ghoul/io/texture/texturereader.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/opengl/programobject.h>
#include <ghoul/opengl/texture.h>
#include <ghoul/opengl/textureunit.h>
#include <modules/spacecraftinstruments/util/imagesequencer.h>

namespace {
    constexpr const char* _loggerCat = "RenderableModelProjection";

    constexpr const char* KeyGeomModelFile = "GeometryFile";
    constexpr const char* keyProjection = "Projection";
    constexpr const char* keyBoundingSphereRadius = "BoundingSphereRadius";

    constexpr const char* DestinationFrame = "GALACTIC";

    constexpr const std::array<const char*, 7> MainUniformNames = {
        "_performShading", "directionToSunViewSpace", "modelViewTransform",
        "projectionTransform", "_projectionFading", "baseTexture", "projectionTexture"
    };

    constexpr const std::array<const char*, 5> FboUniformNames = {
        "projectionTexture", "needShadowMap", "ProjectorMatrix", "ModelTransform",
        "boresight"
    };

    constexpr const std::array<const char*, 2> DepthFboUniformNames = {
        "ProjectorMatrix", "ModelTransform"
    };

    constexpr openspace::properties::Property::PropertyInfo PerformShadingInfo = {
        "PerformShading",
        "Perform Shading",
        "If this value is enabled, the model will be shaded based on the relative "
        "location to the Sun. If this value is disabled, shading is disabled and the "
        "entire model is rendered brightly."
    };
} // namespace

namespace openspace {

documentation::Documentation RenderableModelProjection::Documentation() {
    using namespace documentation;

    return {
        "Renderable Model Projection",
        "newhorizons_renderable_modelprojection",
        {
            {
                KeyGeomModelFile,
                new OrVerifier({ new StringVerifier, new StringListVerifier }),
                Optional::No,
                "The file or files that are used for rendering of this model"
            },
            {
                keyProjection,
                new ReferencingVerifier("newhorizons_projectioncomponent"),
                Optional::No,
                "Contains information about projecting onto this planet."
            },
            {
                PerformShadingInfo.identifier,
                new BoolVerifier,
                Optional::Yes,
                PerformShadingInfo.description
            },
            {
                keyBoundingSphereRadius,
                new DoubleVerifier,
                Optional::Yes,
                "The radius of the bounding sphere of this object. This has to be a "
                "radius that is larger than anything that is rendered by it. It has to "
                "be at least as big as the convex hull of the object. The default value "
                "is 10e9 meters."
            }
        }
    };
}

RenderableModelProjection::RenderableModelProjection(const ghoul::Dictionary& dictionary)
    : Renderable(dictionary)
    , _performShading(PerformShadingInfo, true)
{
    documentation::testSpecificationAndThrow(
        Documentation(),
        dictionary,
        "RenderableModelProjection"
    );

    if (dictionary.hasKey(KeyGeomModelFile)) {
        std::string file;

        if (dictionary.hasValue<std::string>(KeyGeomModelFile)) {
            // Handle single file
            file = absPath(dictionary.value<std::string>(KeyGeomModelFile));
            _geometry = ghoul::io::ModelReader::ref().loadModel(
                file,
                ghoul::io::ModelReader::ForceRenderInvisible::No,
                ghoul::io::ModelReader::NotifyInvisibleDropped::Yes
            );
        }
        else if (dictionary.hasValue<ghoul::Dictionary>(KeyGeomModelFile)) {
            LWARNING("Loading a model with several files is deprecated and will be "
                "removed in a future release"
            );

            ghoul::Dictionary fileDictionary = dictionary.value<ghoul::Dictionary>(
                KeyGeomModelFile
            );
            std::vector<std::unique_ptr<ghoul::modelgeometry::ModelGeometry>> geometries;

            for (std::string_view k : fileDictionary.keys()) {
                // Handle each file
                file = absPath(fileDictionary.value<std::string>(k));
                geometries.push_back(ghoul::io::ModelReader::ref().loadModel(
                    file,
                    ghoul::io::ModelReader::ForceRenderInvisible::No,
                    ghoul::io::ModelReader::NotifyInvisibleDropped::Yes
                ));
            }

            if (!geometries.empty()) {
                std::unique_ptr<ghoul::modelgeometry::ModelGeometry> combinedGeometry =
                    std::move(geometries[0]);

                // Combine all models into one ModelGeometry
                for (unsigned int i = 1; i < geometries.size(); ++i) {
                    for (ghoul::io::ModelMesh& mesh : geometries[i]->meshes()) {
                        combinedGeometry->meshes().push_back(
                            std::move(mesh)
                        );
                    }

                    for (ghoul::modelgeometry::ModelGeometry::TextureEntry& texture :
                        geometries[i]->textureStorage())
                    {
                        combinedGeometry->textureStorage().push_back(
                            std::move(texture)
                        );
                    }
                }
                _geometry = std::move(combinedGeometry);
                _geometry->calculateBoundingRadius();
            }
        }
    }

    addPropertySubOwner(_projectionComponent);

    _projectionComponent.initialize(
        identifier(),
        dictionary.value<ghoul::Dictionary>(keyProjection)
    );

    double boundingSphereRadius = 1.0e9;
    if (dictionary.hasValue<double>(keyBoundingSphereRadius)) {
        boundingSphereRadius = dictionary.value<double>(keyBoundingSphereRadius);
    }
    setBoundingSphere(boundingSphereRadius);

    if (dictionary.hasValue<bool>(PerformShadingInfo.identifier)) {
        _performShading = dictionary.value<bool>(PerformShadingInfo.identifier);
    }

    addProperty(_performShading);
}

RenderableModelProjection::~RenderableModelProjection() {} // NOLINT

bool RenderableModelProjection::isReady() const {
    return (_programObject != nullptr) && _projectionComponent.isReady();
}

void RenderableModelProjection::initializeGL() {
    _programObject = global::renderEngine->buildRenderProgram(
        "ModelShader",
        absPath("${MODULE_SPACECRAFTINSTRUMENTS}/shaders/renderableModel_vs.glsl"),
        absPath("${MODULE_SPACECRAFTINSTRUMENTS}/shaders/renderableModel_fs.glsl")
    );

    ghoul::opengl::updateUniformLocations(
        *_programObject,
        _mainUniformCache,
        MainUniformNames
    );

    _fboProgramObject = ghoul::opengl::ProgramObject::Build(
        "ProjectionPass",
        absPath(
            "${MODULE_SPACECRAFTINSTRUMENTS}/shaders/renderableModelProjection_vs.glsl"
        ),
        absPath(
            "${MODULE_SPACECRAFTINSTRUMENTS}/shaders/renderableModelProjection_fs.glsl"
        )
    );

    ghoul::opengl::updateUniformLocations(
        *_fboProgramObject,
        _fboUniformCache,
        FboUniformNames
    );

    _depthFboProgramObject = ghoul::opengl::ProgramObject::Build(
        "DepthPass",
        absPath("${MODULE_SPACECRAFTINSTRUMENTS}/shaders/renderableModelDepth_vs.glsl"),
        absPath("${MODULE_SPACECRAFTINSTRUMENTS}/shaders/renderableModelDepth_fs.glsl")
    );

    ghoul::opengl::updateUniformLocations(
        *_depthFboProgramObject,
        _depthFboUniformCache,
        DepthFboUniformNames
    );

    _projectionComponent.initializeGL();

    float bs = boundingSphere();
    _geometry->initialize();
    setBoundingSphere(bs); // ignore bounding sphere set by geometry.
}

void RenderableModelProjection::deinitializeGL() {
    if (_geometry) {
        _geometry->deinitialize();
    }

    _geometry = nullptr;

    _projectionComponent.deinitialize();

    global::renderEngine->removeRenderProgram(_programObject.get());
    _programObject = nullptr;
}

ghoul::opengl::Texture& RenderableModelProjection::baseTexture() const {
    return _projectionComponent.projectionTexture();
}

void RenderableModelProjection::render(const RenderData& data, RendererTasks&) {
    if (_projectionComponent.needsClearProjection()) {
        _projectionComponent.clearAllProjections();
    }

    _up = data.camera.lookUpVectorCameraSpace();

    if (_shouldCapture && _projectionComponent.doesPerformProjection()) {
        project();
    }

    _programObject->activate();

    attitudeParameters(_time);
    _imageTimes.clear();

    // Calculate variables to be used as uniform variables in shader
    const glm::dvec3 bodyPosition = data.modelTransform.translation;

    // Model transform and view transform needs to be in double precision
    const glm::dmat4 modelTransform =
        glm::translate(glm::dmat4(1.0), data.modelTransform.translation) * // Translation
        glm::dmat4(data.modelTransform.rotation) * // Rotation
        glm::scale(glm::dmat4(1.0), glm::dvec3(data.modelTransform.scale)); // Scale
    const glm::dmat4 modelViewTransform = data.camera.combinedViewMatrix() *
                                          modelTransform;
    const glm::vec3 directionToSun = glm::normalize(
        _sunPosition - glm::vec3(bodyPosition)
    );
    const glm::vec3 directionToSunViewSpace = glm::normalize(glm::mat3(
        data.camera.combinedViewMatrix()
    ) * directionToSun);

    _programObject->setUniform(_mainUniformCache.performShading, _performShading);
    _programObject->setUniform(
        _mainUniformCache.directionToSunViewSpace,
        directionToSunViewSpace
    );
    _programObject->setUniform(
        _mainUniformCache.modelViewTransform,
        glm::mat4(modelViewTransform)
    );
    _programObject->setUniform(
        _mainUniformCache.projectionTransform,
        data.camera.projectionMatrix()
    );
    _programObject->setUniform(
        _mainUniformCache.projectionFading,
        _projectionComponent.projectionFading()
    );

    ghoul::opengl::TextureUnit baseUnit;
    baseUnit.activate();
    _programObject->setUniform(_mainUniformCache.baseTexture, baseUnit);

    ghoul::opengl::TextureUnit projectionUnit;
    projectionUnit.activate();
    _projectionComponent.projectionTexture().bind();
    _programObject->setUniform(_mainUniformCache.projectionTexture, projectionUnit);

    _geometry->render(*_programObject, false);

    _programObject->deactivate();
}

void RenderableModelProjection::update(const UpdateData& data) {
    if (_programObject->isDirty()) {
        _programObject->rebuildFromFile();

        ghoul::opengl::updateUniformLocations(
            *_programObject,
            _mainUniformCache,
            MainUniformNames
        );
    }

    if (_fboProgramObject->isDirty()) {
        _fboProgramObject->rebuildFromFile();

        ghoul::opengl::updateUniformLocations(
            *_fboProgramObject,
            _fboUniformCache,
            FboUniformNames
        );
    }

    _projectionComponent.update();

    if (_depthFboProgramObject->isDirty()) {
        _depthFboProgramObject->rebuildFromFile();

        ghoul::opengl::updateUniformLocations(
            *_depthFboProgramObject,
            _depthFboUniformCache,
            DepthFboUniformNames
        );
    }

    const double time = data.time.j2000Seconds();
    const double integrateFromTime = data.previousFrameTime.j2000Seconds();

    // Only project new images if time changed since last update.
    if (time > integrateFromTime) {
        if (ImageSequencer::ref().isReady()) {
            if (_projectionComponent.doesPerformProjection()) {
                _shouldCapture = ImageSequencer::ref().imagePaths(
                    _imageTimes,
                    _projectionComponent.projecteeId(),
                    _projectionComponent.instrumentId(),
                    time,
                    integrateFromTime
                );
            }
        }
    }

    glm::dmat3 stateMatrix = data.modelTransform.rotation;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            _transform[i][j] = static_cast<float>(stateMatrix[i][j]);
        }
    }

    glm::dvec3 p =
        global::renderEngine->scene()->sceneGraphNode("Sun")->worldPosition() -
        data.modelTransform.translation;

    _sunPosition = static_cast<glm::vec3>(p);
}

void RenderableModelProjection::imageProjectGPU(
                                          const ghoul::opengl::Texture& projectionTexture)
{
    if (_projectionComponent.needsShadowMap()) {
        _projectionComponent.depthMapRenderBegin();
        _depthFboProgramObject->activate();
        _depthFboProgramObject->setUniform(
            _depthFboUniformCache.ProjectorMatrix,
            _projectorMatrix
        );
        _depthFboProgramObject->setUniform(
            _depthFboUniformCache.ModelTransform,
            _transform
        );

        _geometry->render(*_fboProgramObject, false);

        _depthFboProgramObject->deactivate();
        _projectionComponent.depthMapRenderEnd();
    }

    _projectionComponent.imageProjectBegin();
    _fboProgramObject->activate();

    ghoul::opengl::TextureUnit unitFbo;
    unitFbo.activate();
    projectionTexture.bind();
    _fboProgramObject->setUniform(_fboUniformCache.projectionTexture, unitFbo);

    _fboProgramObject->setUniform(
        _fboUniformCache.needShadowMap,
        _projectionComponent.needsShadowMap()
    );

    ghoul::opengl::TextureUnit unitDepthFbo;
    if (_projectionComponent.needsShadowMap()) {
        unitDepthFbo.activate();
        _projectionComponent.depthTexture().bind();
        _fboProgramObject->setUniform("depthTexture", unitDepthFbo);
    }

    _fboProgramObject->setUniform(_fboUniformCache.ProjectorMatrix, _projectorMatrix);
    _fboProgramObject->setUniform(_fboUniformCache.ModelTransform, _transform);
    _fboProgramObject->setUniform(_fboUniformCache.boresight, _boresight);

    _geometry->render(*_fboProgramObject, false);

    _fboProgramObject->deactivate();

    _projectionComponent.imageProjectEnd();
}

void RenderableModelProjection::attitudeParameters(double time) {
    try {
        _instrumentMatrix = SpiceManager::ref().positionTransformMatrix(
            _projectionComponent.instrumentId(),
            DestinationFrame,
            time
        );

        SpiceManager::FieldOfViewResult res = SpiceManager::ref().fieldOfView(
            _projectionComponent.instrumentId()
        );
        _boresight = std::move(res.boresightVector);
    }
    catch (const SpiceManager::SpiceException&) {
        return;
    }

    double lightTime;
    const glm::dvec3 p = SpiceManager::ref().targetPosition(
        _projectionComponent.projectorId(),
        _projectionComponent.projecteeId(),
        DestinationFrame,
        _projectionComponent.aberration(),
        time,
        lightTime
    );

    const glm::vec3 cpos = p * 10000.0;

    const float distance = glm::length(cpos);
    const double radius = boundingSphere();

    _projectorMatrix = _projectionComponent.computeProjectorMatrix(
        cpos,
        _boresight,
        _up,
        _instrumentMatrix,
        _projectionComponent.fieldOfViewY(),
        _projectionComponent.aspectRatio(),
        static_cast<float>(distance - radius),
        static_cast<float>(distance + radius),
        _boresight
    );
}

void RenderableModelProjection::project() {
    for (const Image& img : _imageTimes) {
        attitudeParameters(img.timeRange.start);
        std::shared_ptr<ghoul::opengl::Texture> projTexture =
            _projectionComponent.loadProjectionTexture(img.path, img.isPlaceholder);
        imageProjectGPU(*projTexture);
    }
    _shouldCapture = false;
}

}  // namespace openspace
