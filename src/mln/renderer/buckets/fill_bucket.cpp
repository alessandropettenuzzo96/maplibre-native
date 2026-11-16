#include <mln/renderer/buckets/fill_bucket.hpp>

#include <mln/gfx/fill_generator.hpp>
#include <mln/renderer/bucket_parameters.hpp>
#include <mln/renderer/layers/render_fill_layer.hpp>
#include <mln/style/layers/fill_layer_impl.hpp>
#include <mln/util/logging.hpp>
#include <mln/util/constants.hpp>
#include <mln/util/math.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace mln {

FillBucket::FillBucket(const FillBucket::PossiblyEvaluatedLayoutProperties&,
                       const std::map<std::string, Immutable<style::LayerProperties>>& layerPaintProperties,
                       const float zoom_,
                       const uint32_t /* overscaling */)
    : zoom(zoom_) {
    using namespace style;
    for (const auto& pair : layerPaintProperties) {
        paintPropertyBinders.emplace(std::piecewise_construct,
                                     std::forward_as_tuple(pair.first),
                                     std::forward_as_tuple(getEvaluated<FillLayerProperties>(pair.second), zoom));
        const auto& properties = static_cast<const FillLayerProperties&>(*pair.second);
        if (!properties.layerImpl().paint.get<FillPatternBounds>().value.isUndefined()) {
            patternBoundsValues.emplace(pair.first, properties.evaluated.get<FillPatternBounds>());
            patternBounds.emplace(pair.first, std::make_shared<PatternBoundsVector>());
        }
    }
}

std::array<float, 4> FillBucket::boundsInTile(const std::array<float, 4>& bounds, const CanonicalTileID& tile) {
    const double scale = std::exp2(tile.z);
    const auto x = [&](double lng) {
        return static_cast<float>(((lng + 180.0) / 360.0 * scale - tile.x) * util::EXTENT);
    };
    const auto y = [&](double lat) {
        const double s = std::sin(std::clamp(lat, -util::LATITUDE_MAX, util::LATITUDE_MAX) * std::numbers::pi / 180.0);
        const double mercator = 0.5 - 0.25 * std::log((1.0 + s) / (1.0 - s)) / std::numbers::pi;
        return static_cast<float>((mercator * scale - tile.y) * util::EXTENT);
    };
    // A box that crosses the antimeridian ends past 180 degrees east.
    const double east = bounds[2] < bounds[0] ? bounds[2] + 360.0 : bounds[2];
    // North is up: the smaller y in tile units.
    return {{x(bounds[0]), y(bounds[3]), x(east), y(bounds[1])}};
}

void FillBucket::populatePatternBounds(const GeometryTileFeature& feature,
                                       const std::size_t vertexOffset,
                                       const CanonicalTileID& canonical) {
    const auto count = vertices.elements() - vertexOffset;
    for (const auto& [layerID, value] : patternBoundsValues) {
        const auto bounds = boundsInTile(value.evaluate(feature, zoom, canonical, {{}}), canonical);
        auto& target = *patternBounds.at(layerID);
        for (std::size_t i = 0; i < count; ++i) {
            target.emplace_back(PatternBoundsVertex{bounds});
        }
    }
}

FillBucket::~FillBucket() {
    sharedVertices->release();
    for (auto& pair : patternBounds) {
        pair.second->release();
    }
}

void FillBucket::recordSDFPattern(const std::string& layerID, const bool sdf) {
    const auto [it, inserted] = sdfPatterns.emplace(layerID, sdf);
    if (!inserted && it->second != sdf && mixedSDFPatternLayers.emplace(layerID).second) {
        Log::Warning(Event::Style,
                     "Style sheet warning: Cannot mix SDF and non-SDF fill patterns in layer \"" + layerID + "\"");
    }
}

bool FillBucket::isSDFPattern(const std::string& layerID) const {
    const auto it = sdfPatterns.find(layerID);
    return it != sdfPatterns.end() && it->second;
}

void FillBucket::generateBuffers(const GeometryCollection& geometry) {
// MLN_TRIANGULATE_FILL_OUTLINES is defined in fill_bucket.hpp
#if MLN_TRIANGULATE_FILL_OUTLINES
    gfx::generateFillAndOutineBuffers(geometry,
                                      vertices,
                                      triangles,
                                      triangleSegments,
                                      lineVertices,
                                      lineIndexes,
                                      lineSegments,
                                      basicLines,
                                      basicLineSegments);
#else
    gfx::generateFillAndOutineBuffers(geometry, vertices, triangles, triangleSegments, basicLines, basicLineSegments);
#endif
}

void FillBucket::populateBinders(const GeometryTileFeature& feature,
                                 const ImagePositions& patternPositions,
                                 const PatternLayerMap& patternDependencies,
                                 std::size_t index,
                                 const CanonicalTileID& canonical) {
    for (auto& pair : paintPropertyBinders) {
        const auto it = patternDependencies.find(pair.first);
        if (it != patternDependencies.end()) {
            pair.second.populateVertexVectors(
                feature, vertices.elements(), index, patternPositions, it->second, canonical);
        } else {
            pair.second.populateVertexVectors(feature, vertices.elements(), index, patternPositions, {}, canonical);
        }
    }
}

void FillBucket::addFeature(const GeometryTileFeature& feature,
                            const GeometryCollection& geometry,
                            const ImagePositions& patternPositions,
                            const PatternLayerMap& patternDependencies,
                            std::size_t index,
                            const CanonicalTileID& canonical) {
    const auto vertexOffset = vertices.elements();

    generateBuffers(geometry);
    populateBinders(feature, patternPositions, patternDependencies, index, canonical);
    populatePatternBounds(feature, vertexOffset, canonical);

    if (retainFeaturesById) {
        const auto vertexCount = vertices.elements() - vertexOffset;
        if (vertexCount > 0) {
            retainFeature(feature, vertexOffset, vertexCount);
        }
    }
}

void FillBucket::upload([[maybe_unused]] gfx::UploadPass& uploadPass) {
    uploaded = true;
    // Layout is final: once these are in GPU buffers the CPU copies are dropped, unless the map
    // reads them back to report rendered features.
    if (retainFeaturesById) {
        return;
    }
    sharedVertices->release();
    sharedTriangles->release();
    sharedBasicLineIndexes->release();
#if MLN_TRIANGULATE_FILL_OUTLINES
    sharedLineVertices->release();
    sharedLineIndexes->release();
#endif
    for (auto& pair : paintPropertyBinders) {
        pair.second.release();
    }
    for (auto& pair : patternBounds) {
        pair.second->release();
    }
}

bool FillBucket::hasData() const {
    return !triangleSegments.empty() || !basicLineSegments.empty();
}

float FillBucket::getQueryRadius(const RenderLayer& layer) const {
    using namespace style;
    const auto& evaluated = getEvaluated<FillLayerProperties>(layer.evaluatedProperties);
    const std::array<float, 2>& translate = evaluated.get<FillTranslate>();
    return util::length(translate[0], translate[1]);
}

void FillBucket::update(const FeatureStates& states,
                        const GeometryTileLayer& layer,
                        const std::string& layerID,
                        const ImagePositions& imagePositions) {
    auto it = paintPropertyBinders.find(layerID);
    if (it != paintPropertyBinders.end()) {
        it->second.updateVertexVectors(states, layer, imagePositions);
        uploaded = false;

        sharedVertices->updateModified();
    }
}

} // namespace mln
