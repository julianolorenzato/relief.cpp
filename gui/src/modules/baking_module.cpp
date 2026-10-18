/**
 * @file baking_module.cpp
 * @brief Implementation of BakingModule.
 */
#include "gui/modules/baking_module.h"
#include <QVBoxLayout>

QWidget* BakingModule::buildContent() {
    return new QWidget();
}

QWidget* BakingModule::buildControls() {
    auto* controlsContainer = new QWidget();
    auto* layout = new QVBoxLayout(controlsContainer);
    layout->addStretch();
    return controlsContainer;
}
