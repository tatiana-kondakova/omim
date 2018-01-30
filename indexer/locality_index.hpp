#pragma once
#include "indexer/cell_id.hpp"
#include "indexer/feature_covering.hpp"
#include "indexer/interval_index.hpp"
#include "indexer/locality_object.hpp"
#include "indexer/scales.hpp"

#include "geometry/rect2d.hpp"

#include <memory>

namespace indexer
{
// Geometry index which stores osm::id as object identifier.
// Used for geocoder server, stores only POIs and buildings which have address information.
// Based on IntervalIndex.
template <typename Reader>
class LocalityIndex
{
public:
  explicit LocalityIndex(Reader const & reader)
  {
    m_intervalIndex = std::make_unique<IntervalIndex<Reader, uint64_t>>(reader);
  }

  // Expects void f(osm::id).
  template <typename F>
  void ForEachInRect(F const & f, m2::RectD const & rect) const
  {
    covering::CoveringGetter cov(rect, covering::CoveringMode::ViewportWithLowLevels);
    covering::Intervals const & interval = cov.Get(scales::GetUpperScale());

    // iterate through intervals
    for (auto const & i : interval)
    {
      m_intervalIndex->ForEach(
          [&f](uint64_t stored_id) { f(LocalityObject::FromStoredId(stored_id)); }, i.first,
          i.second);
    }
  }

private:
  std::unique_ptr<IntervalIndex<Reader, uint64_t>> m_intervalIndex;
};
}  // namespace indexer
