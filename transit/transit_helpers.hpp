#include "transit/transit_types.hpp"

#include <cstdint>
#include <vector>

namespace routing
{
namespace transit
{
std::vector<uint32_t> GetStopsForGate(std::vector<Stop> const & stops,
                                      std::vector<Gate> const & gates,
                                      uint32_t gateId);
}  // namespace transit
}  // namespace routing
