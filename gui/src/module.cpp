/**
 * @file module.cpp
 * @brief Module implementation: wires onModelLoaded() to GlobalContext.
 */
#include "gui/module.h"

Module::Module(GlobalContext *context, QWidget *parent) : QWidget(parent) {
    connect(context, &GlobalContext::modelLoaded, this, &Module::onModelLoaded);
}
