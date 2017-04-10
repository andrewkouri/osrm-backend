#ifndef OSRM_EXTRACTOR_IO_HPP
#define OSRM_EXTRACTOR_IO_HPP

#include "extractor/datasources.hpp"
#include "extractor/nbg_to_ebg.hpp"
#include "extractor/segment_data_container.hpp"
#include "extractor/restriction.hpp"

#include "storage/io.hpp"

#include <boost/assert.hpp>

namespace osrm
{
namespace extractor
{
namespace io
{

inline void read(const boost::filesystem::path &path, std::vector<NBGToEBG> &mapping)
{
    const auto fingerprint = storage::io::FileReader::VerifyFingerprint;
    storage::io::FileReader reader{path, fingerprint};

    reader.DeserializeVector(mapping);
}

inline void write(const boost::filesystem::path &path, const std::vector<NBGToEBG> &mapping)
{
    const auto fingerprint = storage::io::FileWriter::GenerateFingerprint;
    storage::io::FileWriter writer{path, fingerprint};

    writer.SerializeVector(mapping);
}

// read/write for datasources file
inline void read(const boost::filesystem::path &path, Datasources &sources)
{
    const auto fingerprint = storage::io::FileReader::HasNoFingerprint;
    storage::io::FileReader reader{path, fingerprint};

    reader.ReadInto(sources);
}

inline void write(const boost::filesystem::path &path, Datasources &sources)
{
    const auto fingerprint = storage::io::FileWriter::HasNoFingerprint;
    storage::io::FileWriter writer{path, fingerprint};

    writer.WriteFrom(sources);
}

// read/write for segment data file
template <>
inline void read(const boost::filesystem::path &path, SegmentDataContainer &segment_data)
{
    const auto fingerprint = storage::io::FileReader::HasNoFingerprint;
    storage::io::FileReader reader{path, fingerprint};

    auto num_indices = reader.ReadElementCount32();
    segment_data.index.resize(num_indices);
    reader.ReadInto(segment_data.index.data(), num_indices);

    auto num_entries = reader.ReadElementCount32();
    segment_data.nodes.resize(num_entries);
    segment_data.fwd_weights.resize(num_entries);
    segment_data.rev_weights.resize(num_entries);
    segment_data.fwd_durations.resize(num_entries);
    segment_data.rev_durations.resize(num_entries);
    segment_data.datasources.resize(num_entries);

    reader.ReadInto(segment_data.nodes);
    reader.ReadInto(segment_data.fwd_weights);
    reader.ReadInto(segment_data.rev_weights);
    reader.ReadInto(segment_data.fwd_durations);
    reader.ReadInto(segment_data.rev_durations);
    reader.ReadInto(segment_data.datasources);
}

template <>
inline void write(const boost::filesystem::path &path, const SegmentDataContainer &segment_data)
{
    const auto fingerprint = storage::io::FileWriter::HasNoFingerprint;
    storage::io::FileWriter writer{path, fingerprint};

    writer.WriteElementCount32(segment_data.index.size());
    writer.WriteFrom(segment_data.index);

    writer.WriteElementCount32(segment_data.nodes.size());
    BOOST_ASSERT(segment_data.fwd_weights.size() == segment_data.nodes.size());
    BOOST_ASSERT(segment_data.rev_weights.size() == segment_data.nodes.size());
    BOOST_ASSERT(segment_data.fwd_durations.size() == segment_data.nodes.size());
    BOOST_ASSERT(segment_data.rev_durations.size() == segment_data.nodes.size());
    BOOST_ASSERT(segment_data.datasources.size() == segment_data.nodes.size());
    writer.WriteFrom(segment_data.nodes);
    writer.WriteFrom(segment_data.fwd_weights);
    writer.WriteFrom(segment_data.rev_weights);
    writer.WriteFrom(segment_data.fwd_durations);
    writer.WriteFrom(segment_data.rev_durations);
    writer.WriteFrom(segment_data.datasources);
}

// read/write for conditional turn restrictions file
inline void read(const boost::filesystem::path &path, std::vector<InputRestrictionContainer> &restrictions)
{
    const auto fingerprint = storage::io::FileReader::VerifyFingerprint;
    storage::io::FileReader reader{path, fingerprint};

    auto num_indices = reader.ReadElementCount64();
    restrictions.reserve(num_indices);
    InputRestrictionContainer res;
    while (num_indices > 0)
    {
        bool is_only;
        reader.ReadInto(res.restriction.via);
        reader.ReadInto(res.restriction.from);
        reader.ReadInto(res.restriction.to);
        reader.ReadInto(is_only);

        auto num_conditions = reader.ReadElementCount64();
        res.restriction.condition.resize(num_conditions);
        for (uint64_t i = 0; i < num_conditions; i++)
        {
            reader.ReadInto(res.restriction.condition[i].modifier);
            reader.DeserializeVector(res.restriction.condition[i].times);
            reader.DeserializeVector(res.restriction.condition[i].weekdays);
            reader.DeserializeVector(res.restriction.condition[i].monthdays);
        }
        res.restriction.flags.is_only = is_only;
        restrictions.push_back(std::move(res));
        num_indices--;
    }
}

inline void write(storage::io::FileWriter &writer, const InputRestrictionContainer &container)
{
    writer.WriteOne(container.restriction.via);
    writer.WriteOne(container.restriction.from);
    writer.WriteOne(container.restriction.to);
    writer.WriteOne(container.restriction.flags.is_only);
    writer.WriteElementCount64(container.restriction.condition.size());
    for (auto &c : container.restriction.condition)
    {
        writer.WriteOne(c.modifier);
        writer.SerializeVector(c.times);
        writer.SerializeVector(c.weekdays);
        writer.SerializeVector(c.monthdays);
    }
}

}
}
}

#endif
