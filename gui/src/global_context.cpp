/**
 * @file global_context.cpp
 * @brief GlobalContext implementation: loads a mesh from disk.
 */
#include "gui/global_context.h"

GlobalContext::GlobalContext(QObject *parent) : QObject(parent) {}

bool GlobalContext::loadModel(const QString &path) {
    this->originalMesh = std::make_unique<mesh::Mesh>();

    bool success = mesh::io::loadMesh(*this->originalMesh, path.toStdString());
    if (!success) return false;

    emit modelLoaded(this->originalMesh.get());
    return true;
}
