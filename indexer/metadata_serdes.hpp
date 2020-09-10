#pragma once

#include "coding/map_uint32_to_val.hpp"
#include "coding/reader.hpp"
#include "coding/text_storage.hpp"
#include "coding/write_to_sink.hpp"
#include "coding/writer.hpp"

#include "base/stl_helpers.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace feature
{
class Metadata;
}  // namespace feature

namespace indexer
{
class MetadataDeserializer
{
public:
  enum class Version : uint8_t
  {
    V0 = 0,
    Latest = V0
  };

  struct Header
  {
    template <typename Sink>
    void Serialize(Sink & sink) const
    {
      CHECK_EQUAL(base::Underlying(m_version), base::Underlying(Version::V0), ());
      WriteToSink(sink, static_cast<uint8_t>(m_version));
      WriteToSink(sink, m_stringsOffset);
      WriteToSink(sink, m_stringsSize);
      WriteToSink(sink, m_metadataMapOffset);
      WriteToSink(sink, m_metadataMapSize);
    }

    void Read(Reader & reader);

    Version m_version = Version::Latest;
    // All offsets are relative to the start of the section (offset of header is zero).
    uint32_t m_stringsOffset = 0;
    uint32_t m_stringsSize = 0;
    uint32_t m_metadataMapOffset = 0;
    uint32_t m_metadataMapSize = 0;
  };

  static std::unique_ptr<MetadataDeserializer> Load(Reader & reader);

  // Tries to get metadata of the feature with id |featureId|. Returns false if table
  // does not have entry for the feature.
  // This method is threadsafe.
  WARN_UNUSED_RESULT bool Get(uint32_t featureId, feature::Metadata & meta);

private:
  using Map = MapUint32ToValue<std::vector<std::pair<uint8_t, uint32_t>>>;

  std::shared_ptr<Reader> m_stringsSubreader;
  coding::BlockedTextStorageReader m_strings;
  std::mutex m_stringsMutex;
  std::unique_ptr<Map> m_map;
  std::unique_ptr<Reader> m_mapSubreader;
  Version m_version = Version::Latest;
};

class MetadataBuilder
{
public:
  void Put(uint32_t featureId, feature::Metadata const & meta);
  void Freeze(Writer & writer) const;

private:
  std::unordered_map<std::string, uint32_t> m_stringToId;
  std::unordered_map<uint32_t, std::string> m_idToString;
  MapUint32ToValueBuilder<std::vector<std::pair<uint8_t, uint32_t>>> m_builder;
};
}  // namespace indexer
