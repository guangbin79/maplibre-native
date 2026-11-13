#include <mbgl/renderer/sources/render_tile_source.hpp>

#include <mbgl/renderer/buckets/debug_bucket.hpp>
#include <mbgl/renderer/render_tile.hpp>
#include <mbgl/renderer/render_tree.hpp>
#include <mbgl/renderer/paint_parameters.hpp>
#include <mbgl/renderer/tile_parameters.hpp>
#include <mbgl/renderer/tile_render_data.hpp>
#include <mbgl/tile/vector_tile.hpp>
#include <mbgl/util/constants.hpp>
#include <mbgl/util/math.hpp>

namespace mbgl {

using namespace style;

class TileSourceRenderItem : public RenderItem {
public:
    TileSourceRenderItem(Immutable<std::vector<RenderTile>> renderTiles_, std::string name_)
        : renderTiles(std::move(renderTiles_)),
          name(std::move(name_)) {}

private:
    void upload(gfx::UploadPass&) const override;
    void render(PaintParameters&) const override;
    bool hasRenderPass(RenderPass) const override { return false; }
    const std::string& getName() const override { return name; }

    Immutable<std::vector<RenderTile>> renderTiles;
    std::string name;
};

void TileSourceRenderItem::upload(gfx::UploadPass& parameters) const {
    for (auto& tile : *renderTiles) {
        tile.upload(parameters);
    }
}

void TileSourceRenderItem::render(PaintParameters& parameters) const {
    for (auto& tile : *renderTiles) {
        tile.finishRender(parameters);
    }
}

RenderTileSource::RenderTileSource(Immutable<style::Source::Impl> impl_)
    : RenderSource(std::move(impl_)),
      renderTiles(makeMutable<std::vector<RenderTile>>()) {
    tilePyramid.setObserver(this);
}

RenderTileSource::~RenderTileSource() = default;

bool RenderTileSource::isLoaded() const {
    return tilePyramid.isLoaded();
}

std::unique_ptr<RenderItem> RenderTileSource::createRenderItem() {
    return std::make_unique<TileSourceRenderItem>(renderTiles, baseImpl->id);
}

void RenderTileSource::prepare(const SourcePrepareParameters& parameters) {
    bearing = static_cast<float>(parameters.transform.state.getBearing());
    filteredRenderTiles = nullptr;
    renderTilesSortedByY = nullptr;
    auto tiles = makeMutable<std::vector<RenderTile>>();
    tiles->reserve(tilePyramid.getRenderedTiles().size());
    for (auto& entry : tilePyramid.getRenderedTiles()) {
        tiles->emplace_back(entry.first, entry.second);
        tiles->back().prepare(parameters);
    }
    featureState.coalesceChanges(*tiles);
    renderTiles = std::move(tiles);
}

void RenderTileSource::updateFadingTiles() {
    tilePyramid.updateFadingTiles();
}

bool RenderTileSource::hasFadingTiles() const {
    return tilePyramid.hasFadingTiles();
}

RenderTiles RenderTileSource::getRenderTiles() const {
    if (!filteredRenderTiles) {
        // Ownership pin: RenderTiles owns only the wrapper vector, not the
        // RenderTile storage (owned by the `renderTiles` Immutable member).
        // A layer caches the returned vector across frames while prepare()
        // swaps `renderTiles` — the wrapper targets would dangle. The deleter
        // captures the Immutable copy so storage lives as long as any holder.
        auto owner = renderTiles;
        auto* raw = new std::vector<std::reference_wrapper<const RenderTile>>();
        for (const auto& renderTile : *renderTiles) {
            if (renderTile.holdForFade()) {
                continue;
            }
            raw->emplace_back(renderTile);
        }
        filteredRenderTiles = RenderTiles(raw, [owner](auto* p) { delete p; });
    }
    return filteredRenderTiles;
}

RenderTiles RenderTileSource::getRenderTilesSortedByYPosition() const {
    if (!renderTilesSortedByY) {
        const auto comp = [bearing = this->bearing](const RenderTile& a, const RenderTile& b) {
            Point<float> pa(static_cast<float>(a.id.canonical.x), static_cast<float>(a.id.canonical.y));
            Point<float> pb(static_cast<float>(b.id.canonical.x), static_cast<float>(b.id.canonical.y));

            auto par = util::rotate(pa, bearing);
            auto pbr = util::rotate(pb, bearing);

            return std::tie(b.id.canonical.z, par.y, par.x) < std::tie(a.id.canonical.z, pbr.y, pbr.x);
        };

        // Ownership pin: same lifetime hole as getRenderTiles() — sort below
        // dereferences every wrapper; without the pin a cross-frame Immutable
        // swap frees the storage mid-use (SIGSEGV in introsort, observed on
        // Phytium D3000M after ~70min sim-nav camera follow).
        auto owner = renderTiles;
        auto* raw = new std::vector<std::reference_wrapper<const RenderTile>>();
        raw->reserve(renderTiles->size());
        for (const auto& renderTile : *renderTiles) {
            raw->emplace_back(renderTile);
        }
        std::sort(raw->begin(), raw->end(), comp);
        renderTilesSortedByY = RenderTiles(raw, [owner](auto* p) { delete p; });
    }
    return renderTilesSortedByY;
}

const Tile* RenderTileSource::getRenderedTile(const UnwrappedTileID& tileID) const {
    return tilePyramid.getRenderedTile(tileID);
}

std::unordered_map<std::string, std::vector<Feature>> RenderTileSource::queryRenderedFeatures(
    const ScreenLineString& geometry,
    const TransformState& transformState,
    const std::unordered_map<std::string, const RenderLayer*>& layers,
    const RenderedQueryOptions& options,
    const mat4& projMatrix) const {
    return tilePyramid.queryRenderedFeatures(geometry, transformState, layers, options, projMatrix, featureState);
}

std::vector<Feature> RenderTileSource::querySourceFeatures(const SourceQueryOptions& options) const {
    return tilePyramid.querySourceFeatures(options);
}

void RenderTileSource::setFeatureState(const std::optional<std::string>& sourceLayerID,
                                       const std::string& featureID,
                                       const FeatureState& state) {
    featureState.updateState(sourceLayerID, featureID, state);
}

void RenderTileSource::getFeatureState(FeatureState& state,
                                       const std::optional<std::string>& sourceLayerID,
                                       const std::string& featureID) const {
    featureState.getState(state, sourceLayerID, featureID);
}

void RenderTileSource::removeFeatureState(const std::optional<std::string>& sourceLayerID,
                                          const std::optional<std::string>& featureID,
                                          const std::optional<std::string>& stateKey) {
    featureState.removeState(sourceLayerID, featureID, stateKey);
}

void RenderTileSource::reduceMemoryUse() {
    tilePyramid.reduceMemoryUse();
}

void RenderTileSource::dumpDebugLogs() const {
    tilePyramid.dumpDebugLogs();
}

// RenderTileSetSource implementation

RenderTileSetSource::RenderTileSetSource(Immutable<style::Source::Impl> impl_)
    : RenderTileSource(std::move(impl_)) {}

RenderTileSetSource::~RenderTileSetSource() = default;

uint8_t RenderTileSetSource::getMaxZoom() const {
    return cachedTileset ? cachedTileset->zoomRange.max : util::TERRAIN_RGB_MAXZOOM;
}

void RenderTileSetSource::update(Immutable<style::Source::Impl> baseImpl_,
                                 const std::vector<Immutable<style::LayerProperties>>& layers,
                                 const bool needsRendering,
                                 const bool needsRelayout,
                                 const TileParameters& parameters) {
    std::swap(baseImpl, baseImpl_);

    enabled = needsRendering;

    const auto& implTileset = getTileset();
    // In Continuous mode, keep the existing tiles if the new cachedTileset is
    // not yet available, thus providing smart style transitions without
    // flickering. In other modes, allow clearing the tile pyramid first, before
    // the early return in order to avoid render tests being flaky.
    bool canUpdateTileset = implTileset || parameters.mode != MapMode::Continuous;
    if (canUpdateTileset && cachedTileset != implTileset) {
        cachedTileset = implTileset;

        // TODO: this removes existing buckets, and will cause flickering.
        // Should instead refresh tile data in place.
        tilePyramid.clearAll();
    }

    if (!cachedTileset) return;

    updateInternal(*cachedTileset, layers, needsRendering, needsRelayout, parameters);
}

} // namespace mbgl
