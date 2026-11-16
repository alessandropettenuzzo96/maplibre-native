#include <mln/renderer/layers/fill_layer_tweaker.hpp>

#include <mln/gfx/context.hpp>
#include <mln/gfx/drawable.hpp>
#include <mln/gfx/renderable.hpp>
#include <mln/gfx/renderer_backend.hpp>
#include <mln/renderer/layer_group.hpp>
#include <mln/renderer/layers/render_fill_layer.hpp>
#include <mln/renderer/render_tile.hpp>
#include <mln/renderer/render_tree.hpp>
#include <mln/renderer/paint_parameters.hpp>
#include <mln/renderer/paint_property_binder.hpp>
#include <mln/shaders/shader_source.hpp>
#include <mln/shaders/shader_program_base.hpp>
#include <mln/shaders/fill_layer_ubo.hpp>
#include <mln/style/layers/fill_layer_properties.hpp>
#include <mln/util/convert.hpp>
#include <mln/util/std.hpp>

#include <algorithm>
#include <cmath>

namespace mln {

using namespace style;
using namespace shaders;

namespace {

/// fill-pattern-fit for the shaders: 0 is repeat without fill-pattern-bounds (as before
/// fit and bounds existed), then repeat, fill, contain, cover, none, scale-down, scale-up.
float fillPatternFit(const FillLayerProperties& props) {
    if (props.layerImpl().paint.get<FillPatternBounds>().value.isUndefined()) {
        return 0.0f;
    }
    return 1.0f + static_cast<float>(props.evaluated.get<FillPatternFit>());
}

/// fill-pattern-anchor as fractions of the bounds, 0 at the top left.
std::array<float, 2> fillPatternAnchor(const SymbolAnchorType anchor) {
    switch (anchor) {
        case SymbolAnchorType::Left:
            return {0.0f, 0.5f};
        case SymbolAnchorType::Right:
            return {1.0f, 0.5f};
        case SymbolAnchorType::Top:
            return {0.5f, 0.0f};
        case SymbolAnchorType::Bottom:
            return {0.5f, 1.0f};
        case SymbolAnchorType::TopLeft:
            return {0.0f, 0.0f};
        case SymbolAnchorType::TopRight:
            return {1.0f, 0.0f};
        case SymbolAnchorType::BottomLeft:
            return {0.0f, 1.0f};
        case SymbolAnchorType::BottomRight:
            return {1.0f, 1.0f};
        case SymbolAnchorType::Center:
        default:
            return {0.5f, 0.5f};
    }
}

/// `fill-pattern-scale`, never zero.
float fillPatternScale(const FillLayerProperties& props) {
    return std::max(props.evaluated.get<FillPatternScale>(), 1e-6f);
}

/// Screen pixels per tile unit for the natural size of the image. A repeating pattern grows with
/// the map between integer zoom levels and crossfades, as always: its scale goes through
/// `from_scale` and `to_scale`. The fits other than repeat keep the natural size on screen, scaled
/// by `fill-pattern-scale`, and the shaders ignore the crossfade scales for them.
float fillPatternTileRatio(
    const float tileRatio, const float fit, const float scale, const float zoom, const uint8_t intZoom) {
    return fit > 1.5f ? tileRatio * std::exp2(zoom - static_cast<float>(intZoom)) / scale : tileRatio;
}

} // namespace

void FillLayerTweaker::execute(LayerGroupBase& layerGroup, const PaintParameters& parameters) {
    if (layerGroup.empty()) {
        return;
    }

    auto& context = parameters.context;
    const auto& props = static_cast<const FillLayerProperties&>(*evaluatedProperties);
    const auto& evaluated = props.evaluated;
    const auto& crossfade = props.crossfade;

#if !defined(NDEBUG)
    const auto label = layerGroup.getName() + "-update-uniforms";
    const auto debugGroup = parameters.encoder->createDebugGroup(label.c_str());
#endif

    if (!evaluatedPropsUniformBuffer || propertiesUpdated) {
        const FillEvaluatedPropsUBO propsUBO = {
            .color = evaluated.get<FillColor>().constantOr(FillColor::defaultValue()),
            .outline_color = evaluated.get<FillOutlineColor>().constantOr(FillOutlineColor::defaultValue()),
            .opacity = evaluated.get<FillOpacity>().constantOr(FillOpacity::defaultValue()),
            .fade = crossfade.t,
            .from_scale = crossfade.fromScale * fillPatternScale(props),
            .to_scale = crossfade.toScale * fillPatternScale(props),
        };
        context.emplaceOrUpdateUniformBuffer(evaluatedPropsUniformBuffer, &propsUBO);
        propertiesUpdated = false;
    }
    auto& layerUniforms = layerGroup.mutableUniformBuffers();
    layerUniforms.set(idFillEvaluatedPropsUBO, evaluatedPropsUniformBuffer);

    const auto& translation = evaluated.get<FillTranslate>();
    const auto anchor = evaluated.get<FillTranslateAnchor>();
    const auto zoom = static_cast<float>(parameters.state.getZoom());
    const auto intZoom = parameters.state.getIntegerZoom();
    const auto patternFit = fillPatternFit(props);
    const auto patternAnchor = fillPatternAnchor(evaluated.get<FillPatternAnchor>());
    const auto patternScale = fillPatternScale(props);

#if MLN_UBO_CONSOLIDATION
    int i = 0;
    std::vector<FillDrawableUnionUBO> drawableUBOVector(layerGroup.getDrawableCount());
    std::vector<FillTilePropsUnionUBO> tilePropsUBOVector(layerGroup.getDrawableCount());
#endif

    visitLayerGroupDrawables(layerGroup, [&](gfx::Drawable& drawable) {
        if (!drawable.getTileID() || !checkTweakDrawable(drawable)) {
            return;
        }

        const UnwrappedTileID tileID = drawable.getTileID()->toUnwrapped();

        auto* binders = static_cast<FillBinders*>(drawable.getBinders());
        const auto* tile = drawable.getRenderTile();
        if (!binders || !tile) {
            assert(false);
            return;
        }

        const auto& bucket = drawable.getBucket();
        const float sdfPattern = bucket && bucket->isSDFPattern(id) ? 1.0f : 0.0f;

        const auto& fillPatternValue = evaluated.get<FillPattern>().constantOr(
            Faded<expression::Image>{.from = "", .to = ""});
        const auto patternPosA = tile->getPattern(fillPatternValue.from.id());
        const auto patternPosB = tile->getPattern(fillPatternValue.to.id());
        binders->setPatternParameters(patternPosA, patternPosB, crossfade);

        constexpr bool inViewportPixelUnits = false; // from RenderTile::translatedMatrix
        constexpr bool nearClipped = false;
        const auto matrix = getTileMatrix(
            tileID, parameters, translation, anchor, nearClipped, inViewportPixelUnits, drawable);

        // from FillPatternProgram::layoutUniformValues
        const auto tileRatio = 1.0f / tileID.pixelsToTileUnits(1.0f, intZoom);
        const auto patternTileRatio = fillPatternTileRatio(tileRatio, patternFit, patternScale, zoom, intZoom);
        const int32_t tileSizeAtNearestZoom = static_cast<int32_t>(
            util::tileSize_D * parameters.state.zoomScale(intZoom - tileID.canonical.z));
        const int32_t pixelX = static_cast<int32_t>(
            tileSizeAtNearestZoom *
            (tileID.canonical.x + tileID.wrap * parameters.state.zoomScale(tileID.canonical.z)));
        const int32_t pixelY = tileSizeAtNearestZoom * tileID.canonical.y;

        Size textureSize = {0, 0};
        if (const auto& tex = drawable.getTexture(idFillImageTexture)) {
            textureSize = tex->getSize();
        }

#if !MLN_UBO_CONSOLIDATION
        auto& drawableUniforms = drawable.mutableUniformBuffers();
#endif
        switch (static_cast<RenderFillLayer::FillVariant>(drawable.getType())) {
            case RenderFillLayer::FillVariant::Fill: {
#if MLN_UBO_CONSOLIDATION
                drawableUBOVector[i].fillDrawableUBO = {
#else
                const FillDrawableUBO drawableUBO = {
#endif
                    .matrix = util::cast<float>(matrix),

                    .color_t = std::get<0>(binders->get<FillColor>()->interpolationFactor(zoom)),
                    .opacity_t = std::get<0>(binders->get<FillOpacity>()->interpolationFactor(zoom)),
                    .pad1 = 0,
                    .pad2 = 0
                };

#if !MLN_UBO_CONSOLIDATION
                drawableUniforms.createOrUpdate(idFillDrawableUBO, &drawableUBO, context);
#endif
                break;
            }
            case RenderFillLayer::FillVariant::FillOutline: {
#if MLN_UBO_CONSOLIDATION
                drawableUBOVector[i].fillOutlineDrawableUBO = {
#else
                const FillOutlineDrawableUBO drawableUBO = {
#endif
                    .matrix = util::cast<float>(matrix),

                    .outline_color_t = std::get<0>(binders->get<FillOutlineColor>()->interpolationFactor(zoom)),
                    .opacity_t = std::get<0>(binders->get<FillOpacity>()->interpolationFactor(zoom)),
                    .pad1 = 0,
                    .pad2 = 0
                };

#if !MLN_UBO_CONSOLIDATION
                drawableUniforms.createOrUpdate(idFillDrawableUBO, &drawableUBO, context);
#endif
                break;
            }
            case RenderFillLayer::FillVariant::FillPattern: {
#if MLN_UBO_CONSOLIDATION
                drawableUBOVector[i].fillPatternDrawableUBO = {
#else
                const FillPatternDrawableUBO drawableUBO = {
#endif
                    .matrix = util::cast<float>(matrix),
                    .pixel_coord_upper = {static_cast<float>(pixelX >> 16), static_cast<float>(pixelY >> 16)},

                    .pixel_coord_lower = {static_cast<float>(pixelX & 0xFFFF), static_cast<float>(pixelY & 0xFFFF)},
                    .tile_ratio = patternTileRatio,

                    .pattern_from_t = std::get<0>(binders->get<FillPattern>()->interpolationFactor(zoom)),
                    .pattern_to_t = std::get<0>(binders->get<FillPattern>()->interpolationFactor(zoom)),
                    .opacity_t = std::get<0>(binders->get<FillOpacity>()->interpolationFactor(zoom)),
                    .color_t = std::get<0>(binders->get<FillColor>()->interpolationFactor(zoom)),
                    .fit = patternFit,
                    .anchor = patternAnchor
                };

#if MLN_UBO_CONSOLIDATION
                tilePropsUBOVector[i].fillPatternTilePropsUBO = FillPatternTilePropsUBO {
#else
                const FillPatternTilePropsUBO tilePropsUBO = {
#endif
                    .pattern_from = patternPosA ? util::cast<float>(patternPosA->tlbr()) : std::array<float, 4>{0},

                    .pattern_to = patternPosB ? util::cast<float>(patternPosB->tlbr()) : std::array<float, 4>{0},

                    .texsize = {static_cast<float>(textureSize.width), static_cast<float>(textureSize.height)},
                    .sdf = sdfPattern, .pad2 = 0
                };

#if !MLN_UBO_CONSOLIDATION
                drawableUniforms.createOrUpdate(idFillDrawableUBO, &drawableUBO, context);
                drawableUniforms.createOrUpdate(idFillTilePropsUBO, &tilePropsUBO, context);
#endif
                break;
            }
            case RenderFillLayer::FillVariant::FillOutlinePattern: {
#if MLN_UBO_CONSOLIDATION
                drawableUBOVector[i].fillOutlinePatternDrawableUBO = {
#else
                const FillOutlinePatternDrawableUBO drawableUBO = {
#endif
                    .matrix = util::cast<float>(matrix),
                    .pixel_coord_upper = {static_cast<float>(pixelX >> 16), static_cast<float>(pixelY >> 16)},

                    .pixel_coord_lower = {static_cast<float>(pixelX & 0xFFFF), static_cast<float>(pixelY & 0xFFFF)},
                    .tile_ratio = patternTileRatio,

                    .pattern_from_t = std::get<0>(binders->get<FillPattern>()->interpolationFactor(zoom)),
                    .pattern_to_t = std::get<0>(binders->get<FillPattern>()->interpolationFactor(zoom)),
                    .opacity_t = std::get<0>(binders->get<FillOpacity>()->interpolationFactor(zoom)),
                    .color_t = std::get<0>(binders->get<FillColor>()->interpolationFactor(zoom)),
                    .fit = patternFit,
                    .anchor = patternAnchor
                };

#if MLN_UBO_CONSOLIDATION
                tilePropsUBOVector[i].fillOutlinePatternTilePropsUBO = FillOutlinePatternTilePropsUBO {
#else
                const FillOutlinePatternTilePropsUBO tilePropsUBO = {
#endif
                    .pattern_from = patternPosA ? util::cast<float>(patternPosA->tlbr()) : std::array<float, 4>{0},

                    .pattern_to = patternPosB ? util::cast<float>(patternPosB->tlbr()) : std::array<float, 4>{0},

                    .texsize = {static_cast<float>(textureSize.width), static_cast<float>(textureSize.height)},
                    .sdf = sdfPattern, .pad2 = 0
                };

#if !MLN_UBO_CONSOLIDATION
                drawableUniforms.createOrUpdate(idFillDrawableUBO, &drawableUBO, context);
                drawableUniforms.createOrUpdate(idFillTilePropsUBO, &tilePropsUBO, context);
#endif
                break;
            }
            case RenderFillLayer::FillVariant::FillOutlineTriangulated: {
#if MLN_UBO_CONSOLIDATION
                drawableUBOVector[i].fillOutlineTriangulatedDrawableUBO = {
#else
                const FillOutlineTriangulatedDrawableUBO drawableUBO = {
#endif
                    .matrix = util::cast<float>(matrix),
                    .ratio = 1.0f / tileID.pixelsToTileUnits(1.0f, parameters.state.getZoom()),
                    .pad1 = 0,
                    .pad2 = 0,
                    .pad3 = 0
                };

#if !MLN_UBO_CONSOLIDATION
                drawableUniforms.createOrUpdate(idFillDrawableUBO, &drawableUBO, context);
#endif
                break;
            }
            default: {
#ifndef NDEBUG
                mln::Log::Error(mln::Event::Render, "Invalid fill variant type supplied during drawable update!");
#endif
                break;
            }
        }
#if MLN_UBO_CONSOLIDATION
        drawable.setUBOIndex(i++);
#endif
    });

#if MLN_UBO_CONSOLIDATION
    const size_t drawableUBOVectorSize = sizeof(FillDrawableUnionUBO) * drawableUBOVector.size();
    if (!drawableUniformBuffer || drawableUniformBuffer->getSize() < drawableUBOVectorSize) {
        drawableUniformBuffer = context.createUniformBuffer(
            drawableUBOVector.data(), drawableUBOVectorSize, false, true);
    } else {
        drawableUniformBuffer->update(drawableUBOVector.data(), drawableUBOVectorSize);
    }

    const size_t tilePropsUBOVectorSize = sizeof(FillTilePropsUnionUBO) * tilePropsUBOVector.size();
    if (!tilePropsUniformBuffer || tilePropsUniformBuffer->getSize() < tilePropsUBOVectorSize) {
        tilePropsUniformBuffer = context.createUniformBuffer(
            tilePropsUBOVector.data(), tilePropsUBOVectorSize, false, true);
    } else {
        tilePropsUniformBuffer->update(tilePropsUBOVector.data(), tilePropsUBOVectorSize);
    }

    layerUniforms.set(idFillDrawableUBO, drawableUniformBuffer);
    layerUniforms.set(idFillTilePropsUBO, tilePropsUniformBuffer);
#endif
}

} // namespace mln
