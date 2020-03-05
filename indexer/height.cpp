#include "indexer/height.hpp"

#include "indexer/feature_processor.hpp"

#include "coding/endianness.hpp"
#include "coding/files_container.hpp"
#include "coding/memory_region.hpp"
#include "coding/reader.hpp"
#include "coding/succinct_mapper.hpp"
#include "coding/varint.hpp"
#include "coding/write_to_sink.hpp"
#include "coding/writer.hpp"

#include "base/assert.hpp"
#include "base/checked_cast.hpp"
#include "base/logging.hpp"

#include <unordered_map>

#include "3party/succinct/elias_fano.hpp"
#include "3party/succinct/rs_bit_vector.hpp"

using namespace std;

namespace feature
{
// Height::Header ----------------------------------------------------------------------
void Height::Header::Read(Reader & reader)
{
  NonOwningReaderSource source(reader);
  m_version = static_cast<Version>(ReadPrimitiveFromSource<uint8_t>(source));
  CHECK_EQUAL(static_cast<uint8_t>(m_version), static_cast<uint8_t>(Version::V0), ());
  m_indexOffset = ReadPrimitiveFromSource<uint32_t>(source);
  m_indexSize = ReadPrimitiveFromSource<uint32_t>(source);
}

// Height ------------------------------------------------------------------------------
bool Height::Get(uint32_t id, uint32_t & offset) const
{
  return m_map->GetThreadsafe(id, offset);
}

// static
unique_ptr<Height> Height::Load(Reader & reader)
{
  auto table = make_unique<Height>();

  Header header;
  header.Read(reader);

  CHECK_EQUAL(header.m_version, table->m_version, ());

  table->m_indexSubreader = reader.CreateSubReader(header.m_indexOffset, header.m_indexSize);
  if (!table->m_indexSubreader)
    return {};
  if (!table->Init(*(table->m_indexSubreader)))
    return {};
  return table;
}

bool Height::Init(Reader & reader)
{
  // Decodes block encoded by writeBlockCallback from MetadataIndexBuilder::Freeze.
  //auto const readBlockCallback = [&](NonOwningReaderSource & source, uint32_t blockSize,
  //                                   vector<uint32_t> & values) {
  //  values.resize(blockSize);
  //  values[0] = ReadVarUint<uint32_t>(source);
  //  for (size_t i = 1; i < blockSize && source.Size() > 0; ++i)
  //  {
  //    auto const delta = ReadVarInt<int32_t>(source);
  //    values[i] = values[i - 1] + delta;
  //  }
  //};
  auto const readBlockCallback = [&](NonOwningReaderSource & source, uint32_t blockSize,
                                     vector<uint32_t> & values) {
    values.resize(blockSize);
    for (size_t i = 0; i < blockSize && source.Size() > 0; ++i)
      values[i] = ReadVarUint<uint32_t>(source);
  };

  m_map = Map::Load(reader, readBlockCallback);
  return m_map != nullptr;
}

// HeightBuilder -----------------------------------------------------------------------
void HeightBuilder::Put(uint32_t featureId, uint32_t offset)
{
  m_builder.Put(featureId, offset);
}

void HeightBuilder::Freeze(Writer & writer) const
{
  size_t startOffset = writer.Pos();
  CHECK(coding::IsAlign8(startOffset), ());

  Height::Header header;
  header.Serialize(writer);

  uint64_t bytesWritten = writer.Pos();
  coding::WritePadding(writer, bytesWritten);

//  auto const writeBlockCallback = [](auto & w, auto begin, auto end) {
//    WriteVarUint(w, *begin);
//    auto prevIt = begin;
//    for (auto it = begin + 1; it != end; ++it)
//    {
//      WriteVarInt(w, int32_t(*it) - int32_t(*prevIt));
//      prevIt = it;
//    }
//  };
  auto const writeBlockCallback = [](auto & w, auto begin, auto end) {
    for (auto it = begin; it != end; ++it)
      WriteVarUint(w, *it);
  };

  header.m_indexOffset = base::asserted_cast<uint32_t>(writer.Pos() - startOffset);
  m_builder.Freeze(writer, writeBlockCallback);
  header.m_indexSize =
      base::asserted_cast<uint32_t>(writer.Pos() - header.m_indexOffset - startOffset);

  auto const endOffset = writer.Pos();
  writer.Seek(startOffset);
  header.Serialize(writer);
  writer.Seek(endOffset);
}

std::string DebugPrint(Height::Version v)
{
  CHECK(v == Height::Version::V0, (base::Underlying(v)));
  return "V0";
}
}  // namespace feature
