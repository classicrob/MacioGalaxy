#include <algorithm>
#include <cmath>
#include <petari/editor/import.hpp>
#ifdef __APPLE__
#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#endif

namespace PetariEditor::Imports {
    std::optional< Image > decodeImage(const Bytes& bytes, std::string* error) {
#ifdef __APPLE__
        CFDataRef data = CFDataCreate(nullptr, bytes.data(), bytes.size());
        CGImageSourceRef source = data ? CGImageSourceCreateWithData(data, nullptr) : nullptr;
        CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, nullptr) : nullptr;
        if (source) {
            CFRelease(source);
        }
        if (data) {
            CFRelease(data);
        }
        if (!image) {
            *error = "Texture is not a supported image (use PNG or JPEG).";
            return {};
        }
        const auto w = CGImageGetWidth(image), h = CGImageGetHeight(image);
        if (!w || !h || w > 4096 || h > 4096) {
            CGImageRelease(image);
            *error = "Texture dimensions must be between 1 and 4096 pixels.";
            return {};
        }
        Image out{static_cast< int >(w), static_cast< int >(h), Bytes(w * h * 4)};
        CGColorSpaceRef color = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef ctx = CGBitmapContextCreate(out.rgba.data(), w, h, 8, w * 4, color, kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big);
        CGColorSpaceRelease(color);
        if (!ctx) {
            CGImageRelease(image);
            *error = "Could not decode texture pixels.";
            return {};
        }
        CGContextDrawImage(ctx, CGRectMake(0, 0, w, h), image);
        CGContextRelease(ctx);
        CGImageRelease(image);
        for (size_t i = 0; i < out.rgba.size(); i += 4) {
            const unsigned a = out.rgba[i + 3];
            if (a) {
                for (int c = 0; c < 3; ++c) {
                    out.rgba[i + c] = std::min(255u, (out.rgba[i + c] * 255u + a / 2) / a);
                }
            }
        }
        return out;
#else
        *error = "This build has no image decoder.";
        return {};
#endif
    }
    namespace {
        using RGB = std::array< int, 3 >;
        unsigned rgb565(const RGB& c) {
            return ((c[0] >> 3) << 11) | ((c[1] >> 2) << 5) | (c[2] >> 3);
        }
        RGB unpack(unsigned v) {
            return {int((v >> 11) * 255 / 31), int(((v >> 5) & 63) * 255 / 63), int((v & 31) * 255 / 31)};
        }
        void word(Bytes& b, unsigned v) {
            b.push_back(v >> 8);
            b.push_back(v & 255);
        }
    }  // namespace
    Texture encodeTexture(const Image& image, TextureFormat format, std::string* error) {
        Texture out;
        if (image.width < 1 || image.height < 1 || image.width > 1024 || image.height > 1024 ||
            image.rgba.size() != size_t(image.width) * image.height * 4) {
            *error = "GX textures need 1–1024 pixels per side and complete RGBA data.";
            return out;
        }
        if (format == TextureFormat::Auto) {
            format = TextureFormat::CMPR;
            for (size_t i = 3; i < image.rgba.size(); i += 4) {
                if (image.rgba[i] < 250) {
                    format = TextureFormat::RGB5A3;
                    break;
                }
            }
        }
        out.format = format;
        out.width = image.width;
        out.height = image.height;
        const auto pixel = [&](int x, int y) {
            return &image.rgba[(size_t(std::min(y, image.height - 1)) * image.width + std::min(x, image.width - 1)) * 4];
        };
        if (format == TextureFormat::I8 || format == TextureFormat::RGB5A3) {
            const int bw = format == TextureFormat::I8 ? 8 : 4;
            for (int y = 0; y < image.height; y += 4) {
                for (int x = 0; x < image.width; x += bw) {
                    for (int dy = 0; dy < 4; ++dy) {
                        for (int dx = 0; dx < bw; ++dx) {
                            const auto* c = pixel(x + dx, y + dy);
                            if (format == TextureFormat::I8) {
                                out.data.push_back((77 * c[0] + 150 * c[1] + 29 * c[2] + 128) >> 8);
                            } else {
                                word(out.data, c[3] >= 224 ? 0x8000 | ((c[0] >> 3) << 10) | ((c[1] >> 3) << 5) | (c[2] >> 3) :
                                                             ((c[3] >> 5) << 12) | ((c[0] >> 4) << 8) | ((c[1] >> 4) << 4) | (c[2] >> 4));
                            }
                        }
                    }
                }
            }
        } else if (format == TextureFormat::CMPR) {
            for (int y = 0; y < image.height; y += 8) {
                for (int x = 0; x < image.width; x += 8) {
                    for (int block = 0; block < 4; ++block) {
                        const int bx = x + (block % 2) * 4, by = y + (block / 2) * 4;
                        RGB lo{255, 255, 255}, hi{0, 0, 0};
                        bool alpha = false;
                        for (int i = 0; i < 16; ++i) {
                            const auto* c = pixel(bx + i % 4, by + i / 4);
                            alpha |= c[3] < 128;
                            if (c[3] >= 128) {
                                for (int j = 0; j < 3; ++j) {
                                    lo[j] = std::min(lo[j], int(c[j]));
                                    hi[j] = std::max(hi[j], int(c[j]));
                                }
                            }
                        }
                        unsigned a = rgb565(hi), b = rgb565(lo);
                        if (alpha) {
                            if (a > b) {
                                std::swap(a, b);
                            }
                        } else {
                            if (a < b) {
                                std::swap(a, b);
                            }
                            if (a == b) {
                                if (a < 65535) {
                                    ++a;
                                } else {
                                    --b;
                                }
                            }
                        }
                        word(out.data, a);
                        word(out.data, b);
                        RGB p[4] = {unpack(a), unpack(b), {}, {}};
                        for (int j = 0; j < 3; ++j) {
                            p[2][j] = alpha ? (p[0][j] + p[1][j]) / 2 : (2 * p[0][j] + p[1][j]) / 3;
                            p[3][j] = (p[0][j] + 2 * p[1][j]) / 3;
                        }
                        for (int row = 0; row < 4; ++row) {
                            unsigned packed = 0;
                            for (int col = 0; col < 4; ++col) {
                                const auto* c = pixel(bx + col, by + row);
                                int best = 3, err = 1 << 30;
                                if (c[3] >= 128 || !alpha) {
                                    for (int k = 0; k < (alpha ? 3 : 4); ++k) {
                                        int e = 0;
                                        for (int j = 0; j < 3; ++j) {
                                            e += (int(c[j]) - p[k][j]) * (int(c[j]) - p[k][j]);
                                        }
                                        if (e < err) {
                                            err = e;
                                            best = k;
                                        }
                                    }
                                }
                                packed |= unsigned(best) << (6 - 2 * col);
                            }
                            out.data.push_back(packed);
                        }
                    }
                }
            }
        } else {
            *error = "Unsupported GX texture format.";
        }
        return out;
    }
}  // namespace PetariEditor::Imports
