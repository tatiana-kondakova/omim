#include "search/city_finder.hpp"
#include "search/feature_loader.hpp"
#include "search/ranking_info.hpp"
#include "search/reverse_geocoder.hpp"
//#include "search/search_quality/helpers.hpp"
//#include "search/search_quality/sample.hpp"
#include "search/utils.hpp"

#include "indexer/categories_holder.hpp"
#include "indexer/classificator_loader.hpp"
#include "indexer/data_source.hpp"
#include "indexer/feature_algo.hpp"
#include "indexer/search_string_utils.hpp"

#include "coding/file_name_utils.hpp"

#include "platform/local_country_file.hpp"
#include "platform/local_country_file_utils.hpp"
#include "platform/platform.hpp"

#include "geometry/mercator.hpp"

#include "base/macros.hpp"
#include "base/string_utils.hpp"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include <boost/format.hpp>

#include "3party/gflags/src/gflags/gflags.h"

using namespace search;
using namespace std;

DEFINE_string(data_path, "", "Path to data directory (resources dir)");
DEFINE_string(mwm_path, "", "Path to mwm files (writable dir)");
DEFINE_string(out_path, "samples.json", "Path to output samples file");

unsigned constexpr kNumSamplesPerMwm = 10;

char const * kKmlHead =
    "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
    "<kml xmlns=\"http://earth.google.com/kml/2.2\">\n"
    "<Document>\n"
      "<name>%1%</name>\n"
      "<description><![CDATA[MapDescription]]></description>\n"
      "<visibility>0</visibility>\n"
      "<Style id=\"placemark-green\">\n"
        "<IconStyle>\n"
          "<Icon>\n"
            "<href>http://www.mapswithme.com/placemarks/placemark-green.png</href>\n"
          "</Icon>\n"
        "</IconStyle>\n"
      "</Style>\n";

char const * kPlacemarkTemplate =
      "<Placemark>\n"
        "<name>%1%</name>\n"
        "<description><![CDATA[%4%]]></description>\n"
        "<styleUrl>#placemark-green</styleUrl>\n"
        "<Point>\n"
          "<coordinates>%2%,%3%,0.000000</coordinates>\n"
        "</Point>\n"
      "</Placemark>\n";

char const * kKmlFoot =
    "</Document>\n"
    "</kml>\n";

void Encode(string& data) {
    string buffer;
    buffer.reserve(data.size());
    for(size_t pos = 0; pos != data.size(); ++pos) {
        switch(data[pos]) {
            case '&':  buffer.append("&amp;");       break;
            case '\"': buffer.append("&quot;");      break;
            case '\'': buffer.append("&apos;");      break;
            case '<':  buffer.append("&lt;");        break;
            case '>':  buffer.append("&gt;");        break;
            default:   buffer.append(&data[pos], 1); break;
        }
    }
    data.swap(buffer);
}

int main(int argc, char * argv[])
{
  srand(static_cast<unsigned>(time(0)));
  //ChangeMaxNumberOfOpenFiles(kMaxOpenFiles);

  google::SetUsageMessage("Samples generation tool.");
  google::ParseCommandLineFlags(&argc, &argv, true);

  Platform & platform = GetPlatform();

  string countriesFile = COUNTRIES_FILE;
  if (!FLAGS_data_path.empty())
  {
    platform.SetResourceDir(FLAGS_data_path);
    countriesFile = my::JoinFoldersToPath(FLAGS_data_path, COUNTRIES_FILE);
  }

  if (!FLAGS_mwm_path.empty())
    platform.SetWritableDirForTests(FLAGS_mwm_path);

  LOG(LINFO, ("writable dir =", platform.WritableDir()));
  LOG(LINFO, ("resources dir =", platform.ResourcesDir()));

  ofstream out;
  out.open(FLAGS_out_path);
  if (!out.is_open())
  {
    LOG(LERROR, ("Can't open output file", FLAGS_out_path));
    return -1;
  }

  classificator::Load();
  FrozenDataSource dataSource;
//  search::ReverseGeocoder const coder(dataSource);

//  CategoriesHolder const & cats = GetDefaultCategories();

  vector<platform::LocalCountryFile> mwms;
  platform::FindAllLocalMapsAndCleanup(numeric_limits<int64_t>::max() /* the latest version */,
                                       mwms);
boost::format fmt = boost::format(kKmlHead) % "World";
std::string kml = fmt.str();
  for (auto & mwm : mwms)
  {
    mwm.SyncWithDisk();
    auto res = dataSource.RegisterMap(mwm);
    auto mwmInfo = res.first.GetInfo();
    MwmSet::MwmHandle handle(dataSource.GetMwmHandleById(res.first));
    auto popularityRanks =
        search::RankTable::Load(handle.GetValue<MwmValue>()->m_cont, POPULARITY_RANKS_FILE_TAG);
    std::multimap<uint8_t, FeatureID, std::greater<uint8_t>> accumulatedResults;

    FeaturesLoaderGuard g(dataSource, res.first);
    search::ForEachOfTypesInRect(dataSource,
                                 search::GetCategoryTypes("attractions", "en", GetDefaultCategories()),
                                 mwmInfo->m_bordersRect,[&](FeatureID const & id) {
                                                          uint8_t popularity = 0;
                                                          if (popularityRanks)
                                                            popularity = popularityRanks->Get(id.m_index);
                                                          accumulatedResults.emplace(popularity, id);
                                 });

//  boost::format fmt = boost::format(kKmlHead) % mwm.GetCountryName();
//  std::string kml = fmt.str();
    auto it = accumulatedResults.begin();
    uint8_t max_popularity = 0;
    if(it != accumulatedResults.end())
      max_popularity = it->first;
    for (size_t i = 0; i < accumulatedResults.size() && ((it->first > max_popularity / 2) || (i < kNumSamplesPerMwm && it->first > 1)); ++i, ++it)
    {
      FeatureType ft;
      g.GetFeatureByIndex(it->second.m_index, ft);
      string name;
      if (ft.GetNames().GetString(StringUtf8Multilang::kEnglishCode, name) || ft.GetNames().GetString(StringUtf8Multilang::kDefaultCode, name))
      {
        auto center = feature::GetCenter(ft);
        Encode(name);
        std::string description = ft.GetMetadata().GetWikiURL();
        fmt = boost::format(kPlacemarkTemplate) % name % MercatorBounds::XToLon(center.x) % MercatorBounds::YToLat(center.y) % description;
        kml += fmt.str();
      }
    }
//  kml += kKmlFoot;
//  cout << kml <<"\n-------------------------------------------\n";
    dataSource.DeregisterMap(mwm.GetCountryFile());
  }
  kml += kKmlFoot;
  cout << kml;
  return 0;
}
