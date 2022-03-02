/*****************************************************************************************
 *                                                                                       *
 * OpenSpace                                                                             *
 *                                                                                       *
 * Copyright (c) 2014-2022                                                               *
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

#ifndef __OPENSPACE_MODULE_SKYBROWSER___UTILITY___H__
#define __OPENSPACE_MODULE_SKYBROWSER___UTILITY___H__

#include <openspace/documentation/documentation.h>

namespace openspace::skybrowser {
// Constants
constexpr const double ScreenSpaceZ = -2.1;
constexpr const glm::dvec3 NorthPole = { 0.0 , 0.0 , 1.0 };
constexpr const double CelestialSphereRadius = std::numeric_limits<float>::max();  

// Conversion matrix - J2000 equatorial <-> galactic
// https://arxiv.org/abs/1010.3773v1
const glm::dmat3 conversionMatrix = glm::dmat3({
            -0.054875539390,  0.494109453633, -0.867666135681, // col 0
            -0.873437104725, -0.444829594298, -0.198076389622, // col 1
            -0.483834991775,  0.746982248696,  0.455983794523 // col 2
    });

// Galactic coordinates are projected onto the celestial sphere
// Equatorial coordinates are unit length
// Conversion spherical <-> Cartesian
glm::dvec2 cartesianToSpherical(const glm::dvec3& coords);
glm::dvec3 sphericalToCartesian(const glm::dvec2& coords);

// Conversion J2000 equatorial <-> galactic
glm::dvec3 galacticToEquatorial(const glm::dvec3& coords);
glm::dvec3 equatorialToGalactic(const glm::dvec3& coords);

// Conversion to screen space from local camera / pixels
glm::dvec3 localCameraToScreenSpace3d(const glm::dvec3& coords);
glm::vec2 pixelToScreenSpace2d(const glm::vec2& mouseCoordinate);

// Conversion local camera space <-> galactic / equatorial
glm::dvec3 equatorialToLocalCamera(const glm::dvec3& coords);
glm::dvec3 galacticToLocalCamera(const glm::dvec3&  coords);
glm::dvec3 localCameraToGalactic(const glm::dvec3&  coords);
glm::dvec3 localCameraToEquatorial(const glm::dvec3&  coords);

// Camera roll and direction
double cameraRoll(); // Camera roll is with respect to the equatorial North Pole
glm::dvec3 cameraDirectionGalactic();
glm::dvec3 cameraDirectionEquatorial();

// Window and field of view
float windowRatio();
glm::dvec2 fovWindow();
bool isCoordinateInView(const glm::dvec3& equatorial);

// Animation for target and camera 
double angleBetweenVectors(const glm::dvec3& start, const glm::dvec3& end);
glm::dmat4 incrementalAnimationMatrix(const glm::dvec3& start, const glm::dvec3& end, 
    double deltaTime, double speedFactor = 1.0);       
    
} // namespace openspace::skybrowser

#endif // __OPENSPACE_MODULE_SKYBROWSER___UTILITY___H__
