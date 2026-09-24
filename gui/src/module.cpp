/**
 * @file module.cpp
 * @brief Module implementation: wires onMeshLoaded()/onMeshUpdated() to GlobalContext,
 *        notifyMeshUpdate() back out to GlobalContext::onMeshUpdateNotified(), and
 *        assembles the standard content/controls splitter layout in buildUI().
 */
#include "gui/module.h"

#include <QHBoxLayout>
#include <QScrollArea>
#include <QSplitter>

Module::Module(GlobalContext *context, QWidget *parent) : QWidget(parent) {
    connect(context, &GlobalContext::meshLoaded, this, &Module::onMeshLoaded);
    connect(context, &GlobalContext::notifyMeshUpdate, this, &Module::onMeshUpdated);
    connect(this, &Module::notifyMeshUpdate, context, &GlobalContext::onMeshUpdateNotified);
}

void Module::buildUI() {
    QHBoxLayout *outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    outerLayout->addWidget(splitter);

    splitter->addWidget(buildContent());

    QScrollArea *scrollArea = new QScrollArea();
    scrollArea->setWidget(buildControls());
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(220);
    scrollArea->setMaximumWidth(360);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    splitter->addWidget(scrollArea);
}
