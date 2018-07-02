#include "search/city_finder.hpp"
#include "search/feature_loader.hpp"
#include "search/ranking_info.hpp"
#include "search/reverse_geocoder.hpp"
#include "search/search_quality/helpers.hpp"
#include "search/search_quality/sample.hpp"

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

#include "3party/gflags/src/gflags/gflags.h"

using namespace search;
using namespace std;

DEFINE_string(data_path, "", "Path to data directory (resources dir)");
DEFINE_string(mwm_path, "", "Path to mwm files (writable dir)");
DEFINE_string(out_path, "samples.json", "Path to output samples file");

unsigned constexpr kMaxDistanceToObject = 7500;
unsigned constexpr kMinViewPortSize = 100;
unsigned constexpr kMaxViewPortSize = 5000;
unsigned constexpr kMaxTries = 1000;
unsigned constexpr kNumSamplesPerMwm = 10;

// Leaves first letter as is.
void EraseRandom(strings::UniString & uni)
{
  if (uni.size() <= 1)
    return;
  auto index = rand() % (uni.size() - 1) + 1;
  uni.erase(uni.begin() + index);
}

// Leaves first letter as is.
void SwapRandom(strings::UniString & uni)
{
  if (uni.size() <= 2)
    return;
  auto index = rand() % (uni.size() - 2) + 1;
  auto c = uni[index];
  uni[index] = uni[index + 1];
  uni[index + 1] = c;
}

void InsertRandom(uint8_t locale, strings::UniString & uni)
{
  // Not implemented.
}

void AddRandomMisprint(strings::UniString & str)
{
  // todo(@t.yan): Disable for hieroglyphs.
  auto r = rand() % 3;
  if (r == 0)
    return EraseRandom(str);
  if (r == 1)
    return SwapRandom(str);
  // return InsertRandom(lang, str);
}

void AddMisprints(string & str)
{
  auto tokens = strings::Tokenize(str, " ");
  str = {};
  for (size_t i = 0; i < tokens.size(); ++i)
  {
    auto & token = tokens[i];
    auto uni = strings::MakeUniString(token);
    if (uni.size() > 4 && rand() % 3 == 0)
      AddRandomMisprint(uni);
    if (uni.size() > 8 && rand() % 3 == 0)
      AddRandomMisprint(uni);

    str += strings::ToUtf8(uni);
    if (i != tokens.size() - 1)
      str += " ";
  }
}

map<string, vector<string>> kStreetSynonyms = {
    {"улица", {"ул", "у"}},
    {"проспект", {"пр-т", "пр", "пркт", "прт", "пр-кт"}},
    {"переулок", {"пер"}},
    {"проезд", {"пр-д", "пр", "прд"}},
    {"аллея", {"ал"}},
    {"бульвар", {"б-р", "бр"}},
    {"набережная", {"наб", "наб-я"}},
    {"шоссе", {"шос"}},
    {"вyлица", {"вул"}},
    {"площадь", {"пл", "площ"}},
    {"тупик", {"туп"}},
    {"street", {"str", "st"}},
    {"avenue", {"ave", "av"}},
    {"boulevard", {"bld", "blv", "bv", "blvd"}},
    {"drive", {"dr"}},
    {"highway", {"hw", "hwy"}},
    {"road", {"rd"}},
    {"square", {"sq"}}};

void ModifyStreet(string & str)
{
  auto tokens = strings::Tokenize(str, " ");
  str.clear();

  auto const isStreetSynonym = [](string const & s) {
    return kStreetSynonyms.find(s) != kStreetSynonyms.end();
  };

  auto synonymIt = find_if(tokens.begin(), tokens.end(), isStreetSynonym);
  if (synonymIt != tokens.end() &&
      find_if(synonymIt + 1, tokens.end(), isStreetSynonym) == tokens.end())
  {
    // Only one street synonym.
    auto r = rand() % 4;
    if (r == 0 || r == 1)
    {
      auto const & synonyms = kStreetSynonyms[*synonymIt];
      *synonymIt = synonyms[rand() % synonyms.size()];
    }
    if (r == 2)
    {
      tokens.erase(synonymIt);
    }
  }

  if (tokens.empty())
    return;

  for (int i = 0; i < tokens.size(); ++i)
  {
    str += tokens[i];
    if (i != tokens.size() - 1)
      str += " ";
  }
  AddMisprints(str);
}

void ModifyCity(string & str)
{
  AddMisprints(str);
  if (rand() % 4 != 0)
    str = "";
}

void ModifyHouse(uint8_t lang, string & str)
{
  if (str.empty())
    return;

  if (lang == StringUtf8Multilang::GetLangIndex("ru") && isdigit(str[0]))
  {
    auto r = rand() % 5;
    if (r == 0)
      str = "д " + str;
    if (r == 1)
      str = "д" + str;
    if (r == 2)
      str = "дом " + str;
    if (r == 3)
      str = "д. " + str;
  }
}

m2::PointD GenerateNearbyPosition(m2::PointD const & point)
{
  auto pos = point;
  int dX = rand() % (2 * kMaxDistanceToObject) - kMaxDistanceToObject;
  int dY = rand() % (2 * kMaxDistanceToObject) - kMaxDistanceToObject;
  return MercatorBounds::GetSmPoint(pos, dX, dY);
}

m2::RectD GenerateNearbyViewPort(m2::PointD const & point)
{
  auto const size = rand() % (kMaxViewPortSize - kMinViewPortSize) + kMinViewPortSize;
  return MercatorBounds::RectByCenterXYAndSizeInMeters(GenerateNearbyPosition(point), size);
}

bool FindRandomBuilding(FeaturesLoaderGuard const & g, search::ReverseGeocoder const & coder,
                        FeatureType & ft, string & street)
{
  auto const num = g.GetNumFeatures();
  uint32_t fid = rand() % num;
  for (uint32_t i = 0; i < kMaxTries; ++i, ++fid)
  {
    // fid = fid % num;
    if (fid == num)
      fid = 0;
    ft = {};

    if (!g.GetFeatureByIndex(fid, ft))
      continue;

    if (ft.GetHouseNumber().empty())
      continue;

    vector<uint32_t> types;
    ft.ForEachType([&types](uint32_t type) { types.push_back(type); });
    if (!ftypes::IsBuildingChecker::Instance()(types))
      continue;

    auto const streets = coder.GetNearbyFeatureStreets(ft);
    if (streets.second >= streets.first.size())
      continue;

    street = streets.first[streets.second].m_name;
    return true;
  }
  ft = {};
  street = {};
  return false;
}

bool FindRandomCafe(FeaturesLoaderGuard const & g, search::ReverseGeocoder const & coder,
                    FeatureType & ft, string & street, uint32_t & cafeType, string & name)
{
  auto const num = g.GetNumFeatures();
  uint32_t fid = rand() % num;
  set<string> const kCafeTypes = {"restraunt", "bar", "cafe"};
  for (uint32_t i = 0; i < kMaxTries; ++i, ++fid)
  {
    // fid = fid % num;
    if (fid == num)
      fid = 0;
    ft = {};

    if (!g.GetFeatureByIndex(fid, ft))
      continue;

    if (!ft.HasName())
      continue;

    auto const names = ft.GetNames();
    if (!names.GetString(StringUtf8Multilang::kDefaultCode, name))
      continue;

    cafeType = 0;
    ft.ForEachType([&cafeType, &kCafeTypes](uint32_t type) {
      string fullName = classif().GetFullObjectName(type);
      string t1, t2;
      auto pos1 = fullName.find("|");
      if (pos1 != string::npos)
        t1 = fullName.substr(0, pos1);
      if (t1 != "amenity")
        return;
      ++pos1;
      auto pos2 = fullName.find("|", pos1);
      if (pos2 != string::npos)
        t2 = fullName.substr(pos1, pos2 - pos1);
      if (kCafeTypes.find(t2) != kCafeTypes.end())
        cafeType = type;
    });
    if (cafeType == 0)
      continue;

    auto const streets = coder.GetNearbyFeatureStreets(ft);
    if (streets.second < streets.first.size())
      street = streets.first[streets.second].m_name;
    return true;
  }
  ft = {};
  street = {};
  name = {};
  return false;
}

string ModifyAddress(string const & city, string const & street, string const & house, uint8_t lang)
{
  vector<string> address = {city, street, house};
  ModifyCity(address[0]);
  ModifyStreet(address[1]);
  ModifyHouse(lang, address[2]);
  // city street house
  if (rand() % 2 == 0)
  {
    swap(address[0], address[2]);
    // house street city
    if (rand() % 2 == 0)
    {
      // street house city
      swap(address[0], address[1]);
    }
  }
  else if (rand() % 2 == 0)
  {
    // city house street
    swap(address[1], address[2]);
  }
  remove_if(address.begin(), address.end(), [](string const & s) { return s.empty(); });

  string out;
  for (size_t i = 0; i < address.size(); ++i)
  {
    out += address[i];
    if (i != address.size() - 1)
      out += " ";
  }
  return out;
}

string RandomCombine(string const & mandatory, string const & optional)
{
  if (rand() % 2 == 0 || optional.empty())
    return mandatory;
  if (rand() % 2 == 0)
    return optional + " " + mandatory;
  return mandatory + " " + optional;
}

void ModifyCafe(string const & name, string const & type, string & out)
{
  out = RandomCombine(name, type);
  AddMisprints(out);
}

string GetLocalizedCafeType(CategoriesHolder const & cats, uint32_t type, uint8_t lang)
{
  return cats.GetReadableFeatureType(type, lang);
}

string GetLocalizedCityType(FeaturesLoaderGuard const & g, CategoriesHolder const & cats,
                            FeatureID const & fid, uint8_t lang)
{
  FeatureType ft;
  if (!g.GetFeatureByIndex(fid.m_index, ft))
    return {};
  string cityType;
  ft.ForEachType([&cityType, &cats, lang](uint32_t type) {
    if (cityType.empty())
      cityType = cats.GetReadableFeatureType(type, lang);
  });
  return cityType;
}

bool GenerteRequest(FeaturesLoaderGuard const & g, search::ReverseGeocoder const & coder,
                    CategoriesHolder const & cats, vector<int8_t> const & mwmLangCodes,
                    search::CityFinder & cityFinder, ofstream & out)
{
  FeatureType ft;
  string street;
  string cafeStr;
  auto const lang = !mwmLangCodes.empty() ? mwmLangCodes[0] : StringUtf8Multilang::kEnglishCode;
  if (rand() % 2 == 0)
  {
    if (!FindRandomBuilding(g, coder, ft, street))
      return false;
  }
  else
  {
    uint32_t type;
    string name;
    if (!FindRandomCafe(g, coder, ft, street, type, name))
      return false;
    auto const cafeType = GetLocalizedCafeType(
        cats, type, CategoriesHolder::MapLocaleToInteger(StringUtf8Multilang::GetLangByCode(lang)));
    ModifyCafe(name, cafeType, cafeStr);
  }

  auto const house = ft.GetHouseNumber();
  auto const cityId = cityFinder.GetCityFeatureID(feature::GetCenter(ft));
  string cityStr = {};
  if (cityId.m_mwmId == ft.GetID().m_mwmId)
  {
    auto const city =
        cityFinder.GetCityName(feature::GetCenter(ft), StringUtf8Multilang::kDefaultCode);
    auto const cityType =
        city.empty()
            ? ""
            : GetLocalizedCityType(
                  g, cats, cityFinder.GetCityFeatureID(feature::GetCenter(ft)),
                  CategoriesHolder::MapLocaleToInteger(StringUtf8Multilang::GetLangByCode(lang)));
    cityStr = RandomCombine(city, cityType);
  }

  string address =
      street.empty() || house.empty() ? "" : ModifyAddress(cityStr, street, house, lang);
  auto const query = cafeStr.empty() ? address : RandomCombine(cafeStr, address);

  Sample sample;
  sample.m_query = strings::MakeUniString(query);
  sample.m_locale = StringUtf8Multilang::GetLangByCode(lang);
  sample.m_pos = GenerateNearbyPosition(feature::GetCenter(ft));
  sample.m_posAvailable = true;
  sample.m_viewport = GenerateNearbyViewPort(feature::GetCenter(ft));
  sample.m_results.push_back(Sample::Result::Build(ft, Sample::Result::Relevance::Vital));
  auto const irrelevantResultsNumber = 2 + rand() % 15;
  for (size_t i = 0; i < irrelevantResultsNumber; ++i)
  {
    FeatureType irrelevantFt;
    string irrelevantStreet;
    if (FindRandomBuilding(g, coder, irrelevantFt, irrelevantStreet) &&
        (irrelevantFt.GetHouseNumber() != house || irrelevantStreet != street))
    {
      sample.m_results.push_back(
          Sample::Result::Build(irrelevantFt, Sample::Result::Relevance::Irrelevant));
    }
  }

  string json;
  Sample::SerializeToJSONLines({sample}, json);
  out << json;
  return true;
}

int main(int argc, char * argv[])
{
  srand(static_cast<unsigned>(time(0)));
  ChangeMaxNumberOfOpenFiles(kMaxOpenFiles);

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
  search::ReverseGeocoder const coder(dataSource);

  CategoriesHolder const & cats = GetDefaultCategories();

  vector<platform::LocalCountryFile> mwms;
  platform::FindAllLocalMapsAndCleanup(numeric_limits<int64_t>::max() /* the latest version */,
                                       mwms);
  for (auto & mwm : mwms)
  {
    mwm.SyncWithDisk();
    auto res = dataSource.RegisterMap(mwm);

    vector<int8_t> mwmLangCodes;
    auto mwmInfo = res.first.GetInfo();
    mwmInfo->GetRegionData().GetLanguages(mwmLangCodes);

    FeaturesLoaderGuard g(dataSource, res.first);
    search::CityFinder cityFinder(dataSource);
    for (int i = 0; i < kNumSamplesPerMwm; ++i)
      GenerteRequest(g, coder, cats, mwmLangCodes, cityFinder, out);
    dataSource.DeregisterMap(mwm.GetCountryFile());
  }
  return 0;
}
