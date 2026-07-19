#pragma once
#include <QDialog>
#include <QImage>
#include <vector>
#include "relief/textures.h"
#include "relief/uv_atlas.h"

class QComboBox;
class QSlider;
class QLabel;

// Modal dialog to inspect the baked mip pyramids (color, relief, normal, offset)
// fed to ReliefView: pick a map, a channel, and a mip level, and see it as an image.
// Data pointers are read-only views into ReliefTestModule's state; the dialog does
// not own or outlive the caller's data (it's modal, so the parent can't mutate
// them while it's open).
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
    void onMapChanged(int);
    void onChannelChanged(int);
    void onMipChanged(int);

private:
    struct MapInfo
    {
        QString label;
        const std::vector<std::vector<float>> *externalMips = nullptr; // for color/relief/normal: owned by caller, address is stable
        std::vector<std::vector<float>> localMips;                     // for offset map: owned copy (single level)
        bool useLocalMips = false;
        int width = 0, height = 0, channels = 0;
        QStringList channelNames; // size == channels + 1; index 0 reserved for "RGB (combined)"

        // Resolved relative to `this` at call time (not cached), so it stays valid
        // even after this MapInfo is moved into a vector that later reallocates.
        const std::vector<std::vector<float>> &mips() const { return useLocalMips ? localMips : *externalMips; }
    };

    void rebuildMapList();
    void refreshChannelCombo();
    void refreshImage();
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
