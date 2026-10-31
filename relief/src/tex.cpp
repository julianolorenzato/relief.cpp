/**
 * @file tex.cpp
 * @brief Texture file loading and mip generation.
 */
#include "relief/tex.h"

#include <cmath>
#include <cstring>
#include <stdexcept>

#include "stb_image.h"

namespace tex {

namespace mip {

void boxRule(const float* const s[4], float* d, int ch) {
    for (int c = 0; c < ch; ++c) d[c] = (s[0][c] + s[1][c] + s[2][c] + s[3][c]) * 0.25f;
}

void maxRule(const float* const s[4], float* d, int ch) {
    for (int c = 0; c < ch; ++c) d[c] = std::max({s[0][c], s[1][c], s[2][c], s[3][c]});
}

void minRule(const float* const s[4], float* d, int ch) {
    for (int c = 0; c < ch; ++c) d[c] = std::min({s[0][c], s[1][c], s[2][c], s[3][c]});
}

}  // namespace mip

namespace {

/// Reads one texel at `p` into `out` as floats (integer formats normalized to [0,1]).
void readTexel(const uint8_t* p, float* out, const FormatInfo& fi) {
    for (int c = 0; c < fi.channels; ++c) {
        if (fi.isFloat) {
            std::memcpy(&out[c], p + c * 4, 4);
        } else if (fi.channelBytes == 2) {
            uint16_t v;
            std::memcpy(&v, p + c * 2, 2);
            out[c] = v / 65535.0f;
        } else {
            out[c] = p[c] / 255.0f;
        }
    }
}

/// Writes `in` as one texel at `p`; integer formats clamp to [0,1] and round to nearest.
void writeTexel(const float* in, uint8_t* p, const FormatInfo& fi) {
    for (int c = 0; c < fi.channels; ++c) {
        if (fi.isFloat) {
            std::memcpy(p + c * 4, &in[c], 4);
        } else if (fi.channelBytes == 2) {
            const uint16_t v = uint16_t(std::clamp(in[c], 0.0f, 1.0f) * 65535.0f + 0.5f);
            std::memcpy(p + c * 2, &v, 2);
        } else {
            p[c] = uint8_t(std::clamp(in[c], 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    }
}

/// @return The format with `channels` channels and the given channel size.
PixelFormat makeFormat(int channels, int channelBytes) {
    static constexpr PixelFormat table[3][4] = {
        {PixelFormat::R8, PixelFormat::RG8, PixelFormat::RGB8, PixelFormat::RGBA8},
        {PixelFormat::R16, PixelFormat::RG16, PixelFormat::RGB16, PixelFormat::RGBA16},
        {PixelFormat::R32F, PixelFormat::RG32F, PixelFormat::RGB32F, PixelFormat::RGBA32F},
    };
    return table[channelBytes == 4 ? 2 : channelBytes - 1][channels - 1];
}

}  // namespace

Texture::Texture(const std::string& path, std::optional<PixelFormat> requested, const MipRule& rule) {
    int w, h, fileChannels;
    if (!stbi_info(path.c_str(), &w, &h, &fileChannels))
        throw std::runtime_error("Could not load texture: " + path);

    // Decode at the file's own depth (no stb gamma conversion), asking for the target channel count.
    const int fileBytes = stbi_is_hdr(path.c_str()) ? 4 : stbi_is_16_bit(path.c_str()) ? 2 : 1;
    const int wantChannels = requested ? formatInfo(*requested).channels : fileChannels;
    const PixelFormat decoded = makeFormat(wantChannels, fileBytes);

    void* pixels = nullptr;
    switch (fileBytes) {
        case 1: pixels = stbi_load(path.c_str(), &w, &h, &fileChannels, wantChannels); break;
        case 2: pixels = stbi_load_16(path.c_str(), &w, &h, &fileChannels, wantChannels); break;
        case 4: pixels = stbi_loadf(path.c_str(), &w, &h, &fileChannels, wantChannels); break;
    }
    if (!pixels) throw std::runtime_error("Could not load texture: " + path);

    const auto* bytes = static_cast<const uint8_t*>(pixels);
    const FormatInfo di = formatInfo(decoded);
    const size_t count = size_t(w) * h;

    format = requested.value_or(decoded);
    Mip base;
    base.w = w;
    base.h = h;
    if (format == decoded) {
        base.pixels.assign(bytes, bytes + count * di.texelSize());
    } else {
        const FormatInfo fi = info();
        base.pixels.resize(count * fi.texelSize());
        float texel[4];
        for (size_t i = 0; i < count; ++i) {
            readTexel(bytes + i * di.texelSize(), texel, di);
            writeTexel(texel, &base.pixels[i * fi.texelSize()], fi);
        }
    }
    stbi_image_free(pixels);
    mips.push_back(std::move(base));

    generateMips(rule);
}

Texture::Texture(int width, int height, PixelFormat format) : format(format) {
    mips.push_back({width, height, std::vector<uint8_t>(size_t(width) * height * info().texelSize(), 0)});
}

Texture::Texture(int width, int height, PixelFormat format, const uint8_t* basePixels, const MipRule& rule)
    : format(format) {
    const size_t size = size_t(width) * height * info().texelSize();
    mips.push_back({width, height, std::vector<uint8_t>(basePixels, basePixels + size)});
    generateMips(rule);
}

void Texture::generateMips(const MipRule& rule) {
    if (mips.empty()) return;
    mips.resize(1);
    if (!rule) return;
    const FormatInfo fi = info();
    const size_t texel = fi.texelSize();

    while (mips.back().w > 1 || mips.back().h > 1) {
        const Mip& s = mips.back();
        Mip d;
        d.w = std::max(1, s.w / 2);
        d.h = std::max(1, s.h / 2);
        d.pixels.resize(size_t(d.w) * d.h * texel);

        float in[4][4];
        const float* src[4] = {in[0], in[1], in[2], in[3]};
        float out[4];

        for (int y = 0; y < d.h; ++y)
            for (int x = 0; x < d.w; ++x) {
                const int xs[2] = {std::min(x * 2, s.w - 1), std::min(x * 2 + 1, s.w - 1)};
                const int ys[2] = {std::min(y * 2, s.h - 1), std::min(y * 2 + 1, s.h - 1)};

                for (int i = 0; i < 4; ++i)
                    readTexel(&s.pixels[(size_t(ys[i / 2]) * s.w + xs[i % 2]) * texel], in[i], fi);

                rule(src, out, fi.channels);
                writeTexel(out, &d.pixels[(size_t(y) * d.w + x) * texel], fi);
            }
        mips.push_back(std::move(d));
    }
}

}  // namespace tex
