#ifdef GL_ES
precision highp float;
#else

#if !defined(lowp)
#define lowp
#endif

#if !defined(mediump)
#define mediump
#endif

#if !defined(highp)
#define highp
#endif

#endif

const float PI = 3.141592653589793;

// Unpack a pair of values that have been packed into a single float.
// The packed values are assumed to be 8-bit unsigned integers, and are
// packed like so:
// packedValue = floor(input[0]) * 256 + input[1],
vec2 unpack_float(const float packedValue) {
    int packedIntValue = int(packedValue);
    int v0 = packedIntValue / 256;
    return vec2(v0, packedIntValue - v0 * 256);
}

vec2 unpack_opacity(const float packedOpacity) {
    int intOpacity = int(packedOpacity) / 2;
    return vec2(float(intOpacity) / 127.0, mod(packedOpacity, 2.0));
}

// To minimize the number of attributes needed, we encode a 4-component
// color into a pair of floats (i.e. a vec2) as follows:
// [ floor(color.r * 255) * 256 + color.g * 255,
//   floor(color.b * 255) * 256 + color.a * 255 ]
vec4 decode_color(const vec2 encodedColor) {
    return vec4(
        unpack_float(encodedColor[0]) / 255.0,
        unpack_float(encodedColor[1]) / 255.0
    );
}

// Unpack a pair of paint values and interpolate between them.
float unpack_mix_vec2(const vec2 packedValue, const float t) {
    return mix(packedValue[0], packedValue[1], t);
}

// Unpack a pair of paint values and interpolate between them.
vec4 unpack_mix_color(const vec4 packedColors, const float t) {
    vec4 minColor = decode_color(vec2(packedColors[0], packedColors[1]));
    vec4 maxColor = decode_color(vec2(packedColors[2], packedColors[3]));
    return mix(minColor, maxColor, t);
}

// The offset depends on how many pixels are between the world origin and the edge of the tile:
// vec2 offset = mod(pixel_coord, size)
//
// At high zoom levels there are a ton of pixels between the world origin and the edge of the tile.
// The glsl spec only guarantees 16 bits of precision for highp floats. We need more than that.
//
// The pixel_coord is passed in as two 16 bit values:
// pixel_coord_upper = floor(pixel_coord / 2^16)
// pixel_coord_lower = mod(pixel_coord, 2^16)
//
// The offset is calculated in a series of steps that should preserve this precision:
// Position in the image of fill-pattern for fill-pattern-fit other than repeat
// without bounds. bounds and natural_size are in tile units, the result is 0..1
// across the image (repeat wraps it later). fit: 1 repeat, 2 fill, 3 contain,
// 4 cover, 5 none, 6 scale-down, 7 scale-up.
vec2 get_fit_pattern_pos(const vec4 bounds, const vec2 natural_size, const float fit,
    const vec2 anchor, const vec2 pos) {
    vec2 size = max(bounds.zw - bounds.xy, vec2(1e-3));
    vec2 ratio = size / natural_size;
    float contain = min(ratio.x, ratio.y);
    float cover = max(ratio.x, ratio.y);
    vec2 image = natural_size;
    if (fit < 2.5) {
        image = fit < 1.5 ? natural_size : size;
    } else if (fit < 3.5) {
        image = natural_size * contain;
    } else if (fit < 4.5) {
        image = natural_size * cover;
    } else if (fit < 6.5) {
        image = natural_size * (fit < 5.5 ? 1.0 : min(1.0, contain));
    } else {
        image = natural_size * max(1.0, cover);
    }
    vec2 origin = bounds.xy + (size - image) * anchor;
    return (pos - origin) / image;
}

vec2 get_pattern_pos(const vec2 pixel_coord_upper, const vec2 pixel_coord_lower,
    const vec2 pattern_size, const float tile_units_to_pixels, const vec2 pos) {

    vec2 offset = mod(mod(mod(pixel_coord_upper, pattern_size) * 256.0, pattern_size) * 256.0 + pixel_coord_lower, pattern_size);
    return (tile_units_to_pixels * pos + offset) / pattern_size;
}
