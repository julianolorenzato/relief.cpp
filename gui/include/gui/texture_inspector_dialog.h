/**
 * @file texture_inspector_dialog.h
 * @brief Modal dialog for visually inspecting the baked mip pyramids fed to
 *        ReliefView, one channel and mip level at a time.
 */
#pragma once
#include <QDialog>
#include <QImage>
#include <vector>
#include "relief/textures.h"
#include "relief/uv_atlas.h"

class QComboBox;
class QSlider;
class QLabel;

/// @brief Modal dialog to inspect the baked mip pyramids (color, relief,
///        normal, offset) fed to ReliefView: pick a map, a channel, and a mip
///        level, and see it as an image. Data pointers are read-only views
///        into ReliefTestModule's state; the dialog does not own or outlive
///        the caller's data (it's modal, so the parent can't mutate them
///        while it's open).
class TextureInspectorDialog : public QDialog
{
    Q_OBJECT

public:
    TextureInspectorDialog(const MipPyramid *colorMap,
                            const MipPyramid *reliefMap,
                            const MipPyramid *normalMap,
                            const OffsetMapResult *offsetMap,
                            QWidget *parent = nullptr);

private slots:
    /// Switches the active map and rebuilds the channel combo for it.
    void onMapChanged(int);
    /// Switches the active channel and refreshes the preview image.
    void onChannelChanged(int);
    /// Switches the active mip level and refreshes the preview image.
    void onMipChanged(int);

private:
    /// One inspectable map's display metadata plus its mip data (owned or referenced).
    struct MapInfo
    {
        QString label;
        const std::vector<std::vector<float>> *externalMips = nullptr; ///< For color/relief/normal: owned by caller, address is stable.
        std::vector<std::vector<float>> localMips;                     ///< For offset map: owned copy (single level).
        bool useLocalMips = false;
        int width = 0, height = 0, channels = 0;
        QStringList channelNames; ///< Size == channels + 1; index 0 reserved for "RGB (combined)".

        /// Resolved relative to `this` at call time (not cached), so it stays valid
        /// even after this MapInfo is moved into a vector that later reallocates.
        const std::vector<std::vector<float>> &mips() const { return useLocalMips ? localMips : *externalMips; }
    };

    /// Rebuilds `maps` from the non-null constructor-provided pyramids and repopulates the map combo.
    void rebuildMapList();
    /// Repopulates the channel combo for the currently selected map.
    void refreshChannelCombo();
    /// Re-renders the preview image for the current map/channel/mip selection.
    void refreshImage();
    /// Renders one channel (or "RGB combined" when channelIndex == 0) of the current map/mip as a QImage.
    QImage renderChannel(int channelIndex) const;

    const MipPyramid *colorMap_, *reliefMap_, *normalMap_;
    const OffsetMapResult *offsetMap_;

    std::vector<MapInfo> maps;

    QComboBox *mapCombo = nullptr;
    QComboBox *channelCombo = nullptr;
    QSlider *mipSlider = nullptr;
    QLabel *mipLbl = nullptr;
    QLabel *imageLbl = nullptr;
    QLabel *rangeLbl = nullptr;
};
