/**
 * @file relief_controls.h
 * @brief Control groups that drive a ReliefView's relief-mapping parameters
 *        and point light, shared by the modules that embed one.
 */
#pragma once
#include <QGroupBox>
#include <QWidget>
#include "gui/relief_view.h"

namespace relief_controls {

/**
 * @brief Builds the "Relief Mapping Parameters" group: enable toggle, steps,
 *        depth scale, atlas island leaping, texture type, debug view and a
 *        camera reset button. Wireframe/backface culling are left to the caller.
 * @param view   View the controls drive; also the parent of the connections.
 * @param parent Parent widget of the returned group.
 * @return The new group box.
 */
QGroupBox* makeParamsGroup(ReliefView* view, QWidget* parent = nullptr);

/**
 * @brief Builds the "Lighting" group: X/Y/Z sliders for the view's point light.
 * @param view   View whose light the sliders move.
 * @param parent Parent widget of the returned group.
 * @return The new group box.
 */
QGroupBox* makeLightingGroup(ReliefView* view, QWidget* parent = nullptr);

}  // namespace relief_controls
