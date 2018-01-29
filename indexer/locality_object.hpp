#pragma once

#include "geometry/point2d.hpp"

#include "base/buffer_vector.hpp"
#include "base/osm_id.hpp"

#include <cstdint>
#include <vector>

namespace indexer
{
class LocalityObject
{
public:
  static osm::Id FromStoredId(uint64_t storedId) { return osm::Id(storedId >> 2 | storedId << 62); }

  void Deserialize(char const * data);

  template <typename ToDo>
  void ForEachPoint(ToDo && toDo) const
  {
    for (auto const & p : m_points)
      toDo(p);
  }

  template <typename ToDo>
  void ForEachTriangle(ToDo && toDo) const
  {
    for (size_t i = 2; i < m_triangles.size(); i += 3)
      toDo(m_triangles[i - 2], m_triangles[i - 1], m_triangles[i]);
  }

  uint64_t GetStoredId() const { return m_id << 2 | m_id >> 62; }

private:
  uint64_t m_id = 0;
  std::vector<m2::PointD> m_points;
  // m_triangles[3 * i], m_triangles[3 * i + 1], m_triangles[3 * i + 2] form triangle.
  buffer_vector<m2::PointD, 32> m_triangles;
};
}  // namespace indexer
