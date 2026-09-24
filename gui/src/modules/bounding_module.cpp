/**
 * @file bounding_module.cpp
 * @brief BoundingModule implementation: builds the side-by-side orbital/relief
 *        preview UI with an empty controls pane.
 */
#include "gui/modules/bounding_module.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QSplitter>
#include <QScrollArea>

// ─── buildUI ─────────────────────────────────────────────────────────────────

void BoundingModule::buildUI()
{
    QHBoxLayout* outerLayout = new QHBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);
    outerLayout->addWidget(splitter);

    // ── Left: Orbital3DView + ReliefView side by side ────────────────────────
    QWidget* viewportsWidget = new QWidget();
    QHBoxLayout* viewportsLayout = new QHBoxLayout(viewportsWidget);
    viewportsLayout->setContentsMargins(0, 0, 0, 0);

    boundingOrbitalWidget_ = new Orbital3DView(RenderMode::Textured, "Bounding");
    boundingOrbitalWidget_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    viewportsLayout->addWidget(boundingOrbitalWidget_);

    boundingReliefWidget_ = new ReliefView();
    viewportsLayout->addWidget(boundingReliefWidget_);

    // Cameras stay in sync across the two viewports so they can be compared directly.
    connect(boundingReliefWidget_,  &ReliefView::cameraChanged,    boundingOrbitalWidget_, &Orbital3DView::syncCamera);
    connect(boundingOrbitalWidget_, &Orbital3DView::cameraChanged, boundingReliefWidget_,  &ReliefView::syncCamera);

    splitter->addWidget(viewportsWidget);

    // ── Right: empty controls pane ────────────────────────────────────────────
    QWidget* controlsContainer = new QWidget();
    QVBoxLayout* containerLayout = new QVBoxLayout(controlsContainer);
    containerLayout->setContentsMargins(0, 0, 0, 0);

    boundingPlaceholderBtn_ = new QPushButton("Add Step");
    containerLayout->addWidget(boundingPlaceholderBtn_);
    containerLayout->addStretch();

    QScrollArea* scrollArea = new QScrollArea();
    scrollArea->setWidget(controlsContainer);
    scrollArea->setWidgetResizable(true);
    scrollArea->setMinimumWidth(220);
    scrollArea->setMaximumWidth(360);
    scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    splitter->addWidget(scrollArea);
}
