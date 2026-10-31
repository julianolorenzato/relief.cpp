/**
 * @file textures_module.h
 * @brief Pipeline stage that inspects the textures attached to the loaded
 *        (original) mesh: an image viewer with map, channel and mip selection.
 */
#pragma once
#include <QWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <vector>
#include "relief/mesh.h"
#include "gui/module.h"

/**
 * @brief Module hosting a zoomable/pannable image viewer for the maps of the
 *        global original mesh (color, normal, height, planes maps and planes
 *        depth maps). The controls pick the map, the channel(s) shown and the
 *        mip level. Only the maps the mesh actually has are listed.
 */
class TexturesModule : public Module {
    Q_OBJECT

public:
    using Module::Module;

public slots:
    /**
     * @brief Rebuilds the map list from the freshly loaded original mesh.
     * @param original   Mesh whose textures are shown.
     * @param simplified Unused.
     */
    void onMeshLoaded(mesh::Mesh* original, mesh::Mesh* simplified) override;

    /**
     * @brief Rebuilds the map list (the mesh's textures may have changed).
     */
    void onMeshUpdated() override;

protected:
    /**
     * @brief Builds the image viewer.
     * @return Widget placed in the module's viewport area.
     */
    QWidget* buildContent() override;

    /**
     * @brief Builds the map, channel and mip level selectors.
     * @return Widget placed in the module's side panel.
     */
    QWidget* buildControls() override;

    /**
     * @brief Implements wheel zoom (about the cursor) on the viewer's viewport.
     */
    bool eventFilter(QObject* watched, QEvent* event) override;

    /**
     * @brief Applies a fit-to-view that was requested while the module was hidden.
     */
    void showEvent(QShowEvent* event) override;

private slots:
    /// Bakes the planes maps into the original mesh and notifies the other modules.
    void onBake();
    /// Refills the channel combo and mip slider for the selected map, then refits the view.
    void onMapChanged();
    /// Re-renders the image for the current channel / mip / contrast selection.
    void refreshImage();
    /// Fits the whole image in the viewer.
    void fitView();

private:
    /**
     * @brief One selectable way of displaying a map's channels.
     *        `source[k]` is the RGBA channel (0-3) shown in output channel k, or -1 for
     *        black. When `grey` is set only `source[0]` is used and it is shown as grey.
     */
    struct ChannelView {
        QString name;
        int source[3] = {-1, -1, -1};
        bool grey = false;
    };

    /**
     * @brief A non-owning view of one mesh texture. Exactly one of `data8` /
     *        `data16` is set; both hold `channels` interleaved channels, row-major.
     */
    struct MapSource {
        QString label;
        const uint8_t* data8 = nullptr;
        const uint16_t* data16 = nullptr;
        int channels = 4;  ///< 1..4; missing channels read as 0, and alpha as 1.
        int width = 0;
        int height = 0;
        std::vector<ChannelView> views;
    };

    /// Collects the non-empty textures of originalMesh_ into maps_ and updates the map combo.
    void rebuildMaps();
    /// @return The number of mip levels of `m` (down to 1x1).
    static int levelCount(const MapSource& m);
    /**
     * @brief Box-filters level `level` of `m` into floats in [0, 1].
     * @return width * height * 4 floats (RGBA; channels the map lacks are 0, alpha 1).
     */
    static std::vector<float> buildLevel(const MapSource& m, int level, int& outW, int& outH);

    // ── Viewer ────────────────────────────────────────────────────────────────
    QGraphicsView* view_ = nullptr;
    QGraphicsScene* scene_ = nullptr;
    QGraphicsPixmapItem* pixmapItem_ = nullptr;

    // ── Controls ──────────────────────────────────────────────────────────────
    QComboBox* bakeResCombo_ = nullptr;
    QPushButton* bakeBtn_ = nullptr;
    QComboBox* mapCombo_ = nullptr;
    QComboBox* channelCombo_ = nullptr;
    QSlider* mipSlider_ = nullptr;
    QLabel* mipLabel_ = nullptr;
    QCheckBox* stretchCheck_ = nullptr;
    QLabel* infoLabel_ = nullptr;

    std::vector<MapSource> maps_;
    bool fitPending_ = false;
};
