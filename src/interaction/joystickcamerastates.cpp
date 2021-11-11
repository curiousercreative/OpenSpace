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

#include <openspace/interaction/joystickcamerastates.h>

#include <openspace/engine/globals.h>
#include <openspace/scripting/scriptengine.h>
#include <ghoul/logging/logmanager.h>
#include <ghoul/misc/exception.h>
#include <cmath>
#include <utility>

namespace {
    constexpr const char* _loggerCat = "JoystickCameraStates";
} // namespace

namespace openspace::interaction {

JoystickCameraStates::JoystickCameraStates(double sensitivity, double velocityScaleFactor)
    : CameraInteractionStates(sensitivity, velocityScaleFactor)
{}

void JoystickCameraStates::updateStateFromInput(
                                           const JoystickInputStates& joystickInputStates,
                                                double deltaTime)
{
    std::pair<bool, glm::dvec2> globalRotation = { false, glm::dvec2(0.0) };
    std::pair<bool, double> zoom = { false, 0.0 };
    std::pair<bool, glm::dvec2> localRoll = { false, glm::dvec2(0.0) };
    std::pair<bool, glm::dvec2> globalRoll = { false, glm::dvec2(0.0) };
    std::pair<bool, glm::dvec2> localRotation = { false, glm::dvec2(0.0) };

    for (const JoystickInputState& joystickInputState : joystickInputStates) {
        if (joystickInputState.name.empty()) {
            continue;
        }

        JoystickCameraState* joystickCameraState =
            getJoystickCameraState(joystickInputState.name);

        if (!joystickCameraState) {
            continue;
        }

        for (int i = 0; i < JoystickInputState::MaxAxes; ++i) {
            AxisInformation t = joystickCameraState->axisMapping[i];
            if (t.type == AxisType::None) {
                continue;
            }

            float rawValue = joystickInputStates.axis(joystickInputState.name, i);
            float value = rawValue;

            if (t.isSticky) {
                value = rawValue - joystickCameraState->prevAxisValues[i];
                joystickCameraState->prevAxisValues[i] = rawValue;
            }

            if (std::fabs(value) <= t.deadzone) {
                continue;
            }

            if (t.invert) {
                value *= -1.f;
            }

            if (t.normalize || t.type == AxisType::Property) {
                value = (value + 1.f) / 2.f;
            }

            if (t.type == AxisType::Property) {
                value = value * (t.maxValue - t.minValue)  + t.minValue;
            }

            if (std::abs(t.sensitivity) > std::numeric_limits<double>::epsilon()) {
                value = static_cast<float>(value * t.sensitivity * _sensitivity);
            }
            else {
                value = static_cast<float>(value * _sensitivity);
            }

            switch (t.type) {
                case AxisType::None:
                    break;
                case AxisType::OrbitX:
                    globalRotation.first = true;
                    globalRotation.second.x += value;
                    break;
                case AxisType::OrbitY:
                    globalRotation.first = true;
                    globalRotation.second.y += value;
                    break;
                case AxisType::Zoom:
                case AxisType::ZoomIn:
                    zoom.first = true;
                    zoom.second += value;
                    break;
                case AxisType::ZoomOut:
                    zoom.first = true;
                    zoom.second -= value;
                    break;
                case AxisType::LocalRollX:
                    localRoll.first = true;
                    localRoll.second.x += value;
                    break;
                case AxisType::LocalRollY:
                    localRoll.first = true;
                    localRoll.second.y += value;
                    break;
                case AxisType::GlobalRollX:
                    globalRoll.first = true;
                    globalRoll.second.x += value;
                    break;
                case AxisType::GlobalRollY:
                    globalRoll.first = true;
                    globalRoll.second.y += value;
                    break;
                case AxisType::PanX:
                    localRotation.first = true;
                    localRotation.second.x += value;
                    break;
                case AxisType::PanY:
                    localRotation.first = true;
                    localRotation.second.y += value;
                    break;
                case AxisType::Property:
                    std::string script = "openspace.setPropertyValue(\"" +
                        t.propertyUri + "\", " + std::to_string(value) + ");";

                    global::scriptEngine->queueScript(
                        script,
                        scripting::ScriptEngine::RemoteScripting(t.isRemote)
                    );
                    break;
            }
        }

        for (int i = 0; i < JoystickInputState::MaxButtons; ++i) {
            auto itRange = joystickCameraState->buttonMapping.equal_range(i);
            for (auto it = itRange.first; it != itRange.second; ++it) {
                bool active = global::joystickInputStates->button(
                    joystickInputState.name,
                    i,
                    it->second.action
                );

                if (active) {
                    global::scriptEngine->queueScript(
                        it->second.command,
                        scripting::ScriptEngine::RemoteScripting(it->second.synchronization)
                    );
                }
            }
        }
    }

    if (globalRotation.first) {
        _globalRotationState.velocity.set(globalRotation.second, deltaTime);
    }
    else {
        _globalRotationState.velocity.decelerate(deltaTime);
    }

    if (zoom.first) {
        _truckMovementState.velocity.set(glm::dvec2(zoom.second), deltaTime);
    }
    else {
        _truckMovementState.velocity.decelerate(deltaTime);
    }

    if (localRoll.first) {
        _localRollState.velocity.set(localRoll.second, deltaTime);
    }
    else {
        _localRollState.velocity.decelerate(deltaTime);
    }

    if (globalRoll.first) {
        _globalRollState.velocity.set(globalRoll.second, deltaTime);
    }
    else {
        _globalRollState.velocity.decelerate(deltaTime);
    }

    if (localRotation.first) {
        _localRotationState.velocity.set(localRotation.second, deltaTime);
    }
    else {
        _localRotationState.velocity.decelerate(deltaTime);
    }
}

void JoystickCameraStates::setAxisMapping(const std::string& joystickName,
                                          int axis, AxisType mapping,
                                          AxisInvert shouldInvert,
                                          AxisNormalize shouldNormalize,
                                          bool isSticky,
                                          double sensitivity)
{
    ghoul_assert(axis < JoystickInputState::MaxAxes, "axis must be < MaxAxes");

    JoystickCameraState* joystickCameraState = findOrAddJoystickCameraState(joystickName);
    if (!joystickCameraState) {
        return;
    }

    joystickCameraState->axisMapping[axis].type = mapping;
    joystickCameraState->axisMapping[axis].invert = shouldInvert;
    joystickCameraState->axisMapping[axis].normalize = shouldNormalize;
    joystickCameraState->axisMapping[axis].isSticky = isSticky;
    joystickCameraState->axisMapping[axis].sensitivity = sensitivity;

    joystickCameraState->prevAxisValues[axis] =
        global::joystickInputStates->axis(joystickName, axis);
}

void JoystickCameraStates::setAxisMappingProperty(const std::string& joystickName,
                                                  int axis,
                                                  const std::string& propertyUri,
                                                  float min, float max,
                                                  AxisInvert shouldInvert,
                                                  bool isSticky, double sensitivity,
                                                  bool isRemote)
{
    ghoul_assert(axis < JoystickInputState::MaxAxes, "axis must be < MaxAxes");

    JoystickCameraState* joystickCameraState = findOrAddJoystickCameraState(joystickName);
    if (!joystickCameraState) {
        return;
    }

    joystickCameraState->axisMapping[axis].type = AxisType::Property;
    joystickCameraState->axisMapping[axis].invert = shouldInvert;
    joystickCameraState->axisMapping[axis].isSticky = isSticky;
    joystickCameraState->axisMapping[axis].sensitivity = sensitivity;
    joystickCameraState->axisMapping[axis].propertyUri = propertyUri;
    joystickCameraState->axisMapping[axis].minValue = min;
    joystickCameraState->axisMapping[axis].maxValue = max;
    joystickCameraState->axisMapping[axis].isRemote = isRemote;

    joystickCameraState->prevAxisValues[axis] =
        global::joystickInputStates->axis(joystickName, axis);
}

JoystickCameraStates::AxisInformation JoystickCameraStates::axisMapping(
                                                          const std::string& joystickName,
                                                                        int axis) const
{
    const JoystickCameraState* joystickCameraState = getJoystickCameraState(joystickName);
    if (!joystickCameraState) {
        JoystickCameraStates::AxisInformation dummy;
        return dummy;
    }

    return joystickCameraState->axisMapping[axis];
}

void JoystickCameraStates::setDeadzone(const std::string& joystickName, int axis,
                                       float deadzone)
{
    JoystickCameraState* joystickCameraState = findOrAddJoystickCameraState(joystickName);
    if (!joystickCameraState) {
        return;
    }

    joystickCameraState->axisMapping[axis].deadzone = deadzone;
}

float JoystickCameraStates::deadzone(const std::string& joystickName, int axis) const {
    const JoystickCameraState* joystickCameraState = getJoystickCameraState(joystickName);
    if (!joystickCameraState) {
        return 0.0f;
    }

    return joystickCameraState->axisMapping[axis].deadzone;
}

void JoystickCameraStates::bindButtonCommand(const std::string& joystickName,
                                             int button, std::string command,
                                             JoystickAction action,
                                             ButtonCommandRemote remote,
                                             std::string documentation)
{
    JoystickCameraState* joystickCameraState = findOrAddJoystickCameraState(joystickName);
    if (!joystickCameraState) {
        return;
    }

    joystickCameraState->buttonMapping.insert({
        button,
        { std::move(command), action, remote, std::move(documentation) }
    });
}

void JoystickCameraStates::clearButtonCommand(const std::string& joystickName,
                                              int button)
{
    JoystickCameraState* joystickCameraState = getJoystickCameraState(joystickName);
    if (!joystickCameraState) {
        return;
    }

    for (auto it = joystickCameraState->buttonMapping.begin();
         it != joystickCameraState->buttonMapping.end(); )
    {
        // If the current iterator is the button that we are looking for, delete it
        // (std::multimap::erase will return the iterator to the next element for us)
        if (it->first == button) {
            it = joystickCameraState->buttonMapping.erase(it);
        }
        else {
            ++it;
        }
    }
}

std::vector<std::string> JoystickCameraStates::buttonCommand(
                                                          const std::string& joystickName,
                                                             int button) const
{
    std::vector<std::string> result;
    const JoystickCameraState* joystickCameraState = getJoystickCameraState(joystickName);
    if (!joystickCameraState) {
        return result;
    }

    auto itRange = joystickCameraState->buttonMapping.equal_range(button);
    for (auto it = itRange.first; it != itRange.second; ++it) {
        result.push_back(it->second.command);
    }
    return result;
}

JoystickCameraStates::JoystickCameraState* JoystickCameraStates::getJoystickCameraState(
                                                          const std::string& joystickName)
{
    for (JoystickCameraState& joystickCameraState : _joystickCameraStates) {
        if (joystickCameraState.joystickName == joystickName) {
            return &joystickCameraState;
        }
    }

    return nullptr;
}

const JoystickCameraStates::JoystickCameraState*
    JoystickCameraStates::getJoystickCameraState(const std::string& joystickName) const
{
    for (const JoystickCameraState& joystickCameraState : _joystickCameraStates) {
        if (joystickCameraState.joystickName == joystickName) {
            return &joystickCameraState;
        }
    }

    LWARNING(fmt::format("Cannot find JoystickCameraState with name '{}'", joystickName));
    return nullptr;
}

JoystickCameraStates::JoystickCameraState*
    JoystickCameraStates::findOrAddJoystickCameraState(const std::string& joystickName)
{
    JoystickCameraState* joystickCameraState = getJoystickCameraState(joystickName);
    if (!joystickCameraState) {
        if (_joystickCameraStates.size() < JoystickInputStates::MaxNumJoysticks) {
            _joystickCameraStates.push_back(JoystickCameraState());
            joystickCameraState = &_joystickCameraStates.back();
            joystickCameraState->joystickName = joystickName;
        }
        else {
            LWARNING(fmt::format("Cannot add more joysticks, only {} joysticks are "
                "supported", JoystickInputStates::MaxNumJoysticks));
            return nullptr;
        }
    }
    return joystickCameraState;
}


} // namespace openspace::interaction
