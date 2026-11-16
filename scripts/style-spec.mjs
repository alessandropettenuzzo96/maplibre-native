import referenceSpec from './style-spec-reference/v8.json' with { type: "json" };

/** @type {any} */
let modifiedReferenceSpec = referenceSpec;

// https://github.com/maplibre/maplibre-native/issues/250
delete modifiedReferenceSpec['layout_symbol']['text-rotation-alignment']["values"]['viewport-glyph']

// https://github.com/maplibre/maplibre-native/issues/251
delete modifiedReferenceSpec['layout_symbol']['icon-overlap'];
delete modifiedReferenceSpec['layout_symbol']['text-overlap'];
modifiedReferenceSpec["layout_symbol"]["text-allow-overlap"]["requires"] = referenceSpec["layout_symbol"]["text-allow-overlap"]["requires"].filter(val => JSON.stringify(val) !== '{"!":"text-overlap"}');
modifiedReferenceSpec["layout_symbol"]["text-allow-overlap"]["requires"] = referenceSpec["layout_symbol"]["icon-allow-overlap"]["requires"].filter(val => JSON.stringify(val) !== '{"!":"icon-overlap"}');

modifiedReferenceSpec.layer.type.values["location-indicator"] = {};
modifiedReferenceSpec["layout_location-indicator"] = {
  "top-image": {
      "type": "resolvedImage",
      "property-type": "data-constant",
      "expression": {
          "interpolated": false,
          "parameters": [
              "zoom"
          ]
      },
      "doc": "Name of image in sprite to use as the top of the location indicator."
  },
  "bearing-image": {
      "type": "resolvedImage",
      "property-type": "data-constant",
      "expression": {
          "interpolated": false,
          "parameters": [
              "zoom"
          ]
      },
      "doc": "Name of image in sprite to use as the middle of the location indicator."
  },
  "shadow-image": {
      "type": "resolvedImage",
      "property-type": "data-constant",
      "expression": {
          "interpolated": false,
          "parameters": [
              "zoom"
          ]
      },
      "doc": "Name of image in sprite to use as the background of the location indicator."
  }
};

modifiedReferenceSpec["paint_location-indicator"] = {
  "perspective-compensation": {
      "type": "number",
      "default": "0.85",
      "property-type": "data-constant",
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "doc": "The amount of the perspective compensation, between 0 and 1. A value of 1 produces a location indicator of constant width across the screen. A value of 0 makes it scale naturally according to the viewing projection."
  },
  "image-tilt-displacement": {
      "type": "number",
      "property-type": "data-constant",
      "default": "0",
      "units": "pixels",
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "doc": "The displacement off the center of the top image and the shadow image when the pitch of the map is greater than 0. This helps producing a three-dimensional appearance."
  },
  "bearing": {
      "type": "number",
      "default": 0,
      "period": 360,
      "units": "degrees",
      "property-type": "data-constant",
      "expression": {
          "interpolated": false,
          "parameters": [ ]
      },
      "transition": true,
      "doc": "The bearing of the location indicator."
  },
  "location": {
      "type": "array",
      "default": [
          0.0,
          0.0,
          0.0
      ],
      "length": 3,
      "value": "number",
      "property-type": "data-constant",
      "expression": {
          "interpolated": true,
          "parameters": []
      },
      "transition": true,
      "doc": "An array of [latitude, longitude, altitude] position of the location indicator."
  },
  "accuracy-radius": {
      "type": "number",
      "units": "meters",
      "default": 0,
      "property-type": "data-constant",
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "transition": true,
      "doc": "The accuracy, in meters, of the position source used to retrieve the position of the location indicator."
  },
  "top-image-size": {
      "type": "number",
      "units": "factor of the original icon size",
      "property-type": "data-constant",
      "default": 1,
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "transition": true,
      "doc": "The size of the top image, as a scale factor applied to the size of the specified image."
  },
  "bearing-image-size": {
      "type": "number",
      "units": "factor of the original icon size",
      "property-type": "data-constant",
      "default": 1,
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "transition": true,
      "doc": "The size of the bearing image, as a scale factor applied to the size of the specified image."
  },
  "shadow-image-size": {
      "type": "number",
      "units": "factor of the original icon size",
      "property-type": "data-constant",
      "default": 1,
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "transition": true,
      "doc": "The size of the shadow image, as a scale factor applied to the size of the specified image."
  },
  "accuracy-radius-color": {
      "type": "color",
      "property-type": "data-constant",
      "default": "#ffffff",
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "transition": true,
      "doc": "The color for drawing the accuracy radius, as a circle. To adjust transparency, set the alpha component of the color accordingly."

  },
  "accuracy-radius-border-color": {
      "type": "color",
      "property-type": "data-constant",
      "default": "#ffffff",
      "expression": {
          "interpolated": true,
          "parameters": [
              "zoom"
          ]
      },
      "transition": true,
      "doc": "The color for drawing the accuracy radius border. To adjust transparency, set the alpha component of the color accordingly."
  }
};

// internal use
modifiedReferenceSpec["layout_symbol"]["symbol-screen-space"] = {
    "type": "boolean",
    "default": false,
    "property-type": "data-constant",
    "doc": "Internal use only"
};

// fill-pattern fit, anchor and bounds: proposed for the style spec, not there yet.
// One image per polygon (cover, contain, ...) instead of a repeating pattern.
modifiedReferenceSpec["paint_fill"]["fill-pattern-fit"] = {
  "type": "enum",
  "values": {
    "repeat": {"doc": "The image repeats, aligned to the world origin, or to `fill-pattern-bounds` and `fill-pattern-anchor` when bounds are set."},
    "fill": {"doc": "The image is stretched to the bounds, ignoring its aspect ratio."},
    "contain": {"doc": "The image is scaled to fit inside the bounds, keeping its aspect ratio. The rest is transparent."},
    "cover": {"doc": "The image is scaled to cover the bounds, keeping its aspect ratio, and cropped by the polygon."},
    "none": {"doc": "The image keeps its natural size on screen and is cropped by the polygon."},
    "scale-down": {"doc": "The smaller of `none` and `contain`."},
    "scale-up": {"doc": "The larger of `none` and `cover`."}
  },
  "default": "repeat",
  "doc": "How the image of `fill-pattern` fills each polygon. Every value other than `repeat` draws one image per polygon, placed in `fill-pattern-bounds`; without bounds they behave like `repeat`.",
  "requires": ["fill-pattern"],
  "expression": {"interpolated": false, "parameters": ["zoom"]},
  "property-type": "data-constant"
};
modifiedReferenceSpec["paint_fill"]["fill-pattern-anchor"] = {
  "type": "enum",
  "values": {
    "center": {"doc": "The center of the image is at the center of the bounds."},
    "left": {"doc": "The left edge of the image is at the left edge of the bounds, centered vertically."},
    "right": {"doc": "The right edge of the image is at the right edge of the bounds, centered vertically."},
    "top": {"doc": "The top edge of the image is at the top edge of the bounds, centered horizontally."},
    "bottom": {"doc": "The bottom edge of the image is at the bottom edge of the bounds, centered horizontally."},
    "top-left": {"doc": "The top left corner of the image is at the top left corner of the bounds."},
    "top-right": {"doc": "The top right corner of the image is at the top right corner of the bounds."},
    "bottom-left": {"doc": "The bottom left corner of the image is at the bottom left corner of the bounds."},
    "bottom-right": {"doc": "The bottom right corner of the image is at the bottom right corner of the bounds."}
  },
  "default": "center",
  "doc": "Where the image of `fill-pattern` is placed in `fill-pattern-bounds`.",
  "requires": ["fill-pattern", "fill-pattern-bounds"],
  "expression": {"interpolated": false, "parameters": ["zoom"]},
  "property-type": "data-constant"
};
modifiedReferenceSpec["paint_fill"]["fill-pattern-bounds"] = {
  "type": "array",
  "value": "number",
  "length": 4,
  "default": [0, 0, 0, 0],
  "units": "degrees",
  "doc": "The box that `fill-pattern-fit` and `fill-pattern-anchor` place the image in, as `[west, south, east, north]` in longitude and latitude. Tiles cut polygons, so the box of the whole polygon comes from the data, for example with `[\"semiliteral\", [[\"number\", [\"get\", \"w\"]], [\"number\", [\"get\", \"s\"]], [\"number\", [\"get\", \"e\"]], [\"number\", [\"get\", \"n\"]]]]`.",
  "requires": ["fill-pattern"],
  "expression": {"interpolated": false, "parameters": ["feature"]},
  "property-type": "data-driven"
};

export default modifiedReferenceSpec;
