/**
 * @file module.cpp
 * @brief Module implementation: wires onMeshLoaded()/onMeshUpdated() to GlobalContext,
 *        and notifyMeshUpdate() back out to GlobalContext::onMeshUpdateNotified().
 */
#include "gui/module.h"

Module::Module(GlobalContext *context, QWidget *parent) : QWidget(parent) {
    connect(context, &GlobalContext::meshLoaded, this, &Module::onMeshLoaded);
    connect(context, &GlobalContext::notifyMeshUpdate, this, &Module::onMeshUpdated);
    connect(this, &Module::notifyMeshUpdate, context, &GlobalContext::onMeshUpdateNotified);
}
