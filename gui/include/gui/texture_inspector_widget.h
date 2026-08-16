/**
 * @file texture_inspector_widget.h
 * @brief Embeddable widget for visually inspecting the baked mip pyramids fed
 *        to ReliefView, one channel and mip level at a time.
 */
#pragma once
#include <QWidget>
#include <QImage>
#include <vector>
#include "relief/textures.h"

class QComboBox;
class QSlider;
class QLabel;

/// @brief Widget to inspect the baked mip pyramids (color, relief, normal,
///        offset) fed to ReliefView: pick a map, a channel, and a mip level,
///        and see it as an image. Data pointers are read-only views into the
///        caller's state, which must outlive this widget; call refreshMaps()
///        whenever that state changes (e.g. a texture is rebaked) to pick up
///        the change.
class TextureInspectorWidget : public QWidget
{
    Q_OBJECT

public:
    TextureInspectorWidget(const MipPyramid *colorMap,
                            const MipPyramid *reliefMap,
                            const MipPyramid *normalMap,
                            const MipPyramid *offsetMap,
                            QWidget *parent = nullptr);

    /// Re-reads the constructor-provided pyramids (e.g. after a rebake),
    /// repopulating the map list and refreshing the preview. Safe to call
    /// whether or not the widget is currently visible.
    void refreshMaps();

private slots:
    /// Switches the active map and rebuilds the channel combo for it.
    void onMapChanged(int);
    /// Switches the active channel and refreshes the preview image.
    void onChannelChanged(int);
    /// Switches the active mip level and refreshes the preview image.
    void onMipChanged(int);

private:
    /// One inspectable map's display metadata plus a view of its mip data,
    /// owned by the caller (address is stable for the widget's lifetime).
    struct MapInfo
    {
        QString label;
        const std::vector<std::vector<float>> *mips = nullptr;
        int width = 0, height = 0, channels = 0;
        QStringList channelNames; ///< Size == channels + 1; index 0 reserved for "RGB (combined)".
    };

    /// Rebuilds `maps` from the non-null constructor-provided pyramids.
    void rebuildMapList();
    /// Repopulates the channel combo for the currently selected map.
    void refreshChannelCombo();
    /// Re-renders the preview image for the current map/channel/mip selection,
    /// or shows a placeholder if no maps are available.
    void refreshImage();
    /// Renders one channel (or "RGB combined" when channelIndex == 0) of the current map/mip as a QImage.
    QImage renderChannel(int channelIndex) const;

    const MipPyramid *colorMap_, *reliefMap_, *normalMap_;
    const MipPyramid *offsetMap_;

    std::vector<MapInfo> maps;

    QComboBox *mapCombo = nullptr;
    QComboBox *channelCombo = nullptr;
    QSlider *mipSlider = nullptr;
    QLabel *mipLbl = nullptr;
    QLabel *imageLbl = nullptr;
    QLabel *rangeLbl = nullptr;
};
