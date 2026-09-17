/**
 * @file global_context.cpp
 * @brief GlobalContext implementation: loads a mesh from disk.
 */
#include "gui/global_context.h"

GlobalContext::GlobalContext(QObject *parent) : QObject(parent) {}

bool GlobalContext::loadModel(const QString &path) {
    this->originalMesh_ = std::make_unique<mesh::Mesh>();

    bool success = mesh::io::loadMesh(*this->originalMesh_, path.toStdString());
    if (!success) return false;

    this->simplifiedMesh_ = std::make_unique<mesh::Mesh>(*this->originalMesh_);

    emit meshLoaded(this->originalMesh_.get(), this->simplifiedMesh_.get());
    return true;
}

void GlobalContext::onMeshUpdateNotified() { emit notifyMeshUpdate(); }
