#ifdef GL_ES
precision mediump float;
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

out highp vec4 fragColor;

// Image coordinate of a fill pattern for fill-pattern-fit. Repeat wraps; one
// image per polygon stays half a pixel inside the image, so that filtering does
// not blend in the opposite edge of the image.
vec2 get_fit_image_coord(const vec2 pos, const float fit, const vec2 image_pixels) {
    if (fit < 1.5) {
        return mod(pos, 1.0);
    }
    vec2 half_pixel = 0.5 / max(image_pixels, vec2(1.0));
    return clamp(pos, half_pixel, 1.0 - half_pixel);
}

// 0 outside the image when one image fills the polygon (contain, none, scale-down).
float get_fit_image_alpha(const vec2 pos, const float fit) {
    if (fit < 1.5) {
        return 1.0;
    }
    return step(0.0, pos.x) * step(0.0, pos.y) * step(pos.x, 1.0) * step(pos.y, 1.0);
}
