#include "transit/transit_helpers.hpp"

#include <algorithm>

using namespace std;

namespace routing
{
namespace transit
{
vector<uint32_t> GetStopsForGate(vector<Stop> const & stops,
                                 vector<Gate> const & gates,
                                 uint32_t gateId)
{
  vector<uint32_t> res;
  for (auto const & gate : gates)
  {
    if (gate.GetFeatureId() != gateId)
      continue;

    auto stopIds = gate.GetStopIds();
    sort(stopIds.begin(), stopIds.end());
    for (auto const & stop : stops)
    {
      if (binary_search(stopIds.begin(), stopIds.end(), stop.GetId()))
        res.push_back(stop.GetFeatureId());
    }
  }
  return res;
}
}  // namespace transit
}  // namespace routing
