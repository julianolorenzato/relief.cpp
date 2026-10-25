/**
 * @file old_relief_view.h
 * @brief Orbital viewport for the mip-hierarchical relief mapping shader that runs on
 *        the mesh's own UVs, including offscreen pixel picking to resolve a screen
 *        click to a texture UV.
 */
#pragma once
#include <QOpenGLTexture>
#include <QPoint>
#include <QPointF>
#include "gui/views/orbital3dview.h"
#include "relief/textures.h"

/**
 * @brief Renders a mesh with old_relief.vert / old_relief.frag, using the baked
 *        color/relief/normal/offset maps. The maps are either built from the mesh itself
 *        (when it carries a height texture) or uploaded one by one with the set*Map()
 *        functions.
 */
class OldReliefView : public Orbital3DView
{
    Q_OBJECT

public:
    explicit OldReliefView(QWidget *parent = nullptr);
    ~OldReliefView() override;

    /// Uploads the relief-mapping color map.
    void setColorMap(const textures::MipPyramid &pyr);
    /// Uploads the relief-mapping depth/height map.
    void setReliefMap(const textures::MipPyramid &pyr);
    /// Uploads the relief-mapping normal map.
    void setNormalMap(const textures::MipPyramid &pyr);
    /// Uploads the cross-seam Offset_Map used to leap relief rays across UV islands.
    void setOffsetMap(const textures::MipPyramid &off);
    /// @return true once the color, relief, normal and offset maps have been uploaded.
    bool hasTextures() const { return colorTex && reliefTex && normalTex && offsetTex; }

signals:
    /// Emitted after a click is resolved to a texture UV (see performPick).
    /// hit is false if the click didn't land on any mesh geometry.
    void pixelPicked(QPointF uv, bool hit);

public slots:
    void setReliefEnabled(bool v);
    void setUseAtlas(bool v);
    void setReliefTextureType(int v);
    void setSteps(int v);
    void setDepthScale(double v);
    void setDebugView(int v);

protected:
    void initPrograms() override;
    void onMeshChanged() override;
    void paintScene() override;
    void afterFrame() override;
    bool isReady() const override { return hasTextures(); }
    QString notReadyMessage() const override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;

private:
    // Render state
    bool reliefEnabled      = true;
    bool useAtlas           = true;
    int  reliefTextureType  = 0;  // 0 = depth map, 1 = height map
    int  steps              = 64;
    float depthScale        = 0.05f;
    int  debugView          = 0;

    // Pixel picking: a click queues a widget-space position here; afterFrame()
    // resolves it after the normal (visible) draw so picking never disturbs
    // the on-screen frame.
    bool pickPending = false;
    QPoint pickPos;
    QPoint pressPos;
    GLuint pickFbo = 0, pickColorTex = 0, pickDepthRbo = 0;
    int pickFboW = 0, pickFboH = 0;
    /// (Re)creates the offscreen pick FBO/textures at size w x h if the size changed.
    void ensurePickFbo(int w, int h);
    /// Deletes the offscreen pick FBO/textures.
    void deletePickFbo();
    /// Renders a UV-encoding pass into the pick FBO and reads back the texel at `widgetPos`, emitting pixelPicked.
    void performPick(const QPoint &widgetPos);

    QOpenGLShaderProgram prog;  ///< Relief shader (old_relief.vert / old_relief.frag).
    QOpenGLTexture *colorTex  = nullptr;
    QOpenGLTexture *reliefTex = nullptr;
    QOpenGLTexture *normalTex = nullptr;
    QOpenGLTexture *offsetTex = nullptr;

    /// Builds and uploads all four relief maps from `mesh_` itself, using its
    /// height texture for the relief map. Needs a current GL context.
    void buildMapsFromMesh();
    /// Binds the shader program, its uniforms and textures.
    /// @param debugView DebugView uniform value (e.g. 3 for the UV pick pass).
    /// @return The bound program, to release after drawing.
    QOpenGLShaderProgram *bindSurfaceProgram(int debugView);
    /// Deletes all owned GL texture objects.
    void deleteTextures();
};
