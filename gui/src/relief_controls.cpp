/**
 * @file relief_controls.cpp
 * @brief relief_controls implementation.
 */
#include "gui/relief_controls.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

namespace relief_controls {

namespace {

/**
 * Builds one "<label>: <value> [slider]" row and appends it to `layout`.
 * The slider stores hundredths internally so it can offer 0.01 resolution
 * over a floating-point range; `slot` is forwarded the descaled value on
 * every change.
 */
void makeLightSliderRow(QVBoxLayout* layout, const char* label, int minHundredths,
                        int maxHundredths, int defaultHundredths, ReliefView* view,
                        void (ReliefView::*slot)(double)) {
    QLabel* valueLbl = new QLabel(
        QString("%1: %2").arg(label).arg(defaultHundredths / 100.0, 0, 'f', 2));
    layout->addWidget(valueLbl);

    QSlider* slider = new QSlider(Qt::Horizontal);
    slider->setMinimum(minHundredths);
    slider->setMaximum(maxHundredths);
    slider->setValue(defaultHundredths);
    layout->addWidget(slider);

    QObject::connect(slider, &QSlider::valueChanged, view,
                     [valueLbl, label, view, slot](int v) {
        double value = v / 100.0;
        valueLbl->setText(QString("%1: %2").arg(label).arg(value, 0, 'f', 2));
        (view->*slot)(value);
    });
}

}  // namespace

QGroupBox* makeParamsGroup(ReliefView* view, QWidget* parent) {
    QGroupBox* group = new QGroupBox("Relief Mapping Parameters", parent);
    QVBoxLayout* layout = new QVBoxLayout(group);

    QCheckBox* enabledCheck = new QCheckBox("Enable Relief Mapping");
    enabledCheck->setChecked(true);
    QObject::connect(enabledCheck, &QCheckBox::toggled, view, &ReliefView::setReliefEnabled);
    layout->addWidget(enabledCheck);

    QHBoxLayout* stepsRow = new QHBoxLayout();
    stepsRow->addWidget(new QLabel("Steps:"));
    QSpinBox* stepsSpin = new QSpinBox();
    stepsSpin->setRange(1, 512);
    stepsSpin->setValue(256);
    QObject::connect(stepsSpin, QOverload<int>::of(&QSpinBox::valueChanged), view,
                     &ReliefView::setSteps);
    stepsRow->addWidget(stepsSpin, 1);
    layout->addLayout(stepsRow);

    QHBoxLayout* depthRow = new QHBoxLayout();
    depthRow->addWidget(new QLabel("Depth Scale:"));
    QDoubleSpinBox* depthScaleSpin = new QDoubleSpinBox();
    depthScaleSpin->setRange(0.0, 2.0);
    depthScaleSpin->setSingleStep(0.01);
    depthScaleSpin->setDecimals(4);
    depthScaleSpin->setValue(0.05);
    QObject::connect(depthScaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), view,
                     &ReliefView::setDepthScale);
    depthRow->addWidget(depthScaleSpin, 1);
    layout->addLayout(depthRow);

    QCheckBox* useAtlasCheck = new QCheckBox("Use Atlas (Island Leaping)");
    useAtlasCheck->setChecked(true);
    QObject::connect(useAtlasCheck, &QCheckBox::toggled, view, &ReliefView::setUseAtlas);
    layout->addWidget(useAtlasCheck);

    QHBoxLayout* texTypeRow = new QHBoxLayout();
    texTypeRow->addWidget(new QLabel("Texture Type:"));
    QComboBox* textureTypeCombo = new QComboBox();
    textureTypeCombo->addItem("Depth Map", 0);
    textureTypeCombo->addItem("Height Map", 1);
    QObject::connect(textureTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), view,
                     [view, textureTypeCombo](int) {
        view->setReliefTextureType(textureTypeCombo->currentData().toInt());
    });
    texTypeRow->addWidget(textureTypeCombo, 1);
    layout->addLayout(texTypeRow);

    QHBoxLayout* debugRow = new QHBoxLayout();
    debugRow->addWidget(new QLabel("Debug:"));
    QComboBox* debugViewCombo = new QComboBox();
    debugViewCombo->addItem("Shaded", 0);
    debugViewCombo->addItem("Step Count", 1);
    debugViewCombo->addItem("Leap Count", 2);
    debugViewCombo->addItem("UV After Relief", 3);
    QObject::connect(debugViewCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), view,
                     [view, debugViewCombo](int) {
        view->setDebugView(debugViewCombo->currentData().toInt());
    });
    debugRow->addWidget(debugViewCombo, 1);
    layout->addLayout(debugRow);

    QPushButton* resetCamBtn = new QPushButton("Reset Camera");
    QObject::connect(resetCamBtn, &QPushButton::clicked, view, &ReliefView::resetCamera);
    layout->addWidget(resetCamBtn);

    return group;
}

QGroupBox* makeLightingGroup(ReliefView* view, QWidget* parent) {
    QGroupBox* group = new QGroupBox("Lighting", parent);
    QVBoxLayout* layout = new QVBoxLayout(group);

    // Matches ReliefView's default lightPos{0.f, 2.f, 1.5f}.
    makeLightSliderRow(layout, "X", -300, 300, 0, view, &ReliefView::setLightX);
    makeLightSliderRow(layout, "Y", 50, 400, 200, view, &ReliefView::setLightY);
    makeLightSliderRow(layout, "Z", -300, 300, 150, view, &ReliefView::setLightZ);

    return group;
}

}  // namespace relief_controls
