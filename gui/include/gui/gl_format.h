/**
 * @file gl_format.h
 * @brief OpenGL format enums matching a tex::PixelFormat, shared by the views' texture uploads.
 */
#pragma once
#include <QOpenGLFunctions>

#include "relief/tex.h"

namespace glfmt {

/// @return The sized internal format of `f` (e.g. GL_RGBA8, GL_RG16, GL_RGB32F). Integer
///         formats are normalized (UNorm): the shader reads them as floats in [0,1].
inline GLenum internalFormat(tex::PixelFormat f) {
    static constexpr GLenum table[3][4] = {
        {GL_R8, GL_RG8, GL_RGB8, GL_RGBA8},
        {GL_R16, GL_RG16, GL_RGB16, GL_RGBA16},
        {GL_R32F, GL_RG32F, GL_RGB32F, GL_RGBA32F},
    };
    const tex::FormatInfo fi = tex::formatInfo(f);
    return table[fi.isFloat ? 2 : fi.channelBytes - 1][fi.channels - 1];
}

/// @return The client pixel format of `f` (GL_RED, GL_RG, GL_RGB or GL_RGBA).
inline GLenum pixelFormat(tex::PixelFormat f) {
    switch (tex::formatInfo(f).channels) {
        case 1: return GL_RED;
        case 2: return GL_RG;
        case 3: return GL_RGB;
        default: return GL_RGBA;
    }
}

/// @return The client component type of `f` (GL_UNSIGNED_BYTE, GL_UNSIGNED_SHORT or GL_FLOAT).
inline GLenum pixelType(tex::PixelFormat f) {
    const tex::FormatInfo fi = tex::formatInfo(f);
    if (fi.isFloat) return GL_FLOAT;
    return fi.channelBytes == 2 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
}

}  // namespace glfmt
