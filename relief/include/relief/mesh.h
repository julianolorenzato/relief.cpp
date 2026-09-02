/**
 * @file mesh.h
 * @brief Triangle mesh data type: vertices, faces, OBJ/glTF I/O, and basic
 *        topology queries.
 */
#pragma once
#include <vector>
#include <map>
#include <string>
#include <cstdint>
#include <Eigen/Dense>

/// A single mesh vertex: position, accumulated quadric, UV, and (optionally) envelope planes.
struct Vertex {
    Eigen::Vector3d pos  = Eigen::Vector3d::Zero();
    Eigen::Matrix4d Q    = Eigen::Matrix4d::Zero();
    Eigen::Vector2d uv   = Eigen::Vector2d::Zero();
    bool            removed = false;

    /// Outward-oriented planes (n.x,n.y,n.z,d) of original faces already
    /// absorbed by this vertex over the course of its collapses (see
    /// Simplifier::envelopeConstraint). Empty when the envelope
    /// constraint is disabled.
    std::vector<Eigen::Vector4d> envelope;
};

/// A single triangular mesh face.
struct Face {
    int  v[3];             ///< Vertex indices.
    bool removed = false;
};

/**
 * @brief Triangle mesh with position/UV/texture data and OBJ/glTF I/O.
 */
class Mesh {
public:
    std::vector<Vertex> vertices;
    std::vector<Face>   faces;

    /// @brief Loads a mesh from a Wavefront OBJ file.
    /// @param path Path to the .obj file.
    /// @return true on success.
    bool loadOBJ(const std::string& path);
    /// @brief Saves the mesh to a Wavefront OBJ file.
    /// @param path Destination path.
    /// @return true on success.
    bool saveOBJ(const std::string& path) const;
    /// @brief Loads a mesh (and its embedded textures) from a glTF/GLB file.
    /// @param path Path to the .gltf/.glb file.
    /// @return true on success.
    bool loadGLTF(const std::string& path);
    /// @brief Saves the mesh to a glTF/GLB file.
    /// @param path Destination path.
    /// @return true on success.
    bool saveGLTF(const std::string& path) const;

    /// Textures extracted from the source GLTF (RGBA, row-major).
    std::vector<uint8_t> textureData;
    int textureWidth  = 0;
    int textureHeight = 0;

    std::vector<uint8_t> normalTextureData;
    int normalTextureWidth  = 0;
    int normalTextureHeight = 0;

    /// @return Number of non-removed faces.
    int faceCount() const;
    /// @return Number of non-removed vertices.
    int vertexCount() const;

    /// Edge classification: boundary = referenced by exactly 1 face (same
    /// criterion used by Simplifier::addBoundaryConstraints). Used both
    /// internally by the simplifier and by the viewport so both see the same
    /// thing.
    struct EdgeInfo {
        int  v1, v2;
        bool boundary;
        int  faceId; ///< Reference face (always valid; unique when boundary == true).
    };
    /// @brief Classifies every edge of the current mesh as boundary or interior.
    /// @return One EdgeInfo per unique edge.
    std::vector<EdgeInfo> classifyEdges() const;
};
