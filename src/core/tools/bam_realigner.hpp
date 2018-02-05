// Copyright (c) 2017 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef bam_realigner_hpp
#define bam_realigner_hpp

#include <vector>
#include <cstddef>

#include <boost/optional.hpp>

#include "basics/aligned_read.hpp"
#include "core/types/haplotype.hpp"
#include "core/types/genotype.hpp"
#include "containers/mappable_flat_set.hpp"
#include "io/reference/reference_genome.hpp"
#include "io/read/read_reader.hpp"
#include "io/read/read_writer.hpp"
#include "io/variant/vcf_reader.hpp"
#include "utils/thread_pool.hpp"

namespace octopus {

class BAMRealigner
{
public:
    using ReadReader = io::ReadReader;
    using ReadWriter = io::ReadWriter;
    using SampleName = ReadReader::SampleName;
    using SampleList = std::vector<SampleName>;
    
    struct Config
    {
        bool copy_hom_ref_reads = false;
        bool simplify_cigars = false;
        boost::optional<unsigned> max_threads = 1;
    };
    
    struct Report
    {
        std::size_t n_total_reads;
        std::size_t n_read_assigned;
        std::size_t n_read_unassigned;
        std::size_t n_hom_ref_reads;
    };
    
    BAMRealigner() = default;
    BAMRealigner(Config config);
    
    BAMRealigner(const BAMRealigner&)            = default;
    BAMRealigner& operator=(const BAMRealigner&) = default;
    BAMRealigner(BAMRealigner&&)                 = default;
    BAMRealigner& operator=(BAMRealigner&&)      = default;
    
    ~BAMRealigner() = default;
    
    Report realign(ReadReader& src, VcfReader& variants, ReadWriter& dst,
                   const ReferenceGenome& reference, SampleList samples) const;
    Report realign(ReadReader& src, VcfReader& variants, ReadWriter& dst,
                   const ReferenceGenome& reference) const;
    
private:
    using VcfIterator = VcfReader::RecordIterator;
    using CallBlock   = std::vector<VcfRecord>;
    struct Batch
    {
        MappableFlatSet<Genotype<Haplotype>> genotypes;
        std::vector<AlignedRead> reads;
    };
    using BatchList = std::vector<Batch>;
    
    Config config_;
    mutable ThreadPool workers_;
    
    CallBlock read_next_block(VcfIterator& first, const VcfIterator& last, const SampleList& samples) const;
    BatchList read_next_batch(VcfIterator& first, const VcfIterator& last, ReadReader& src,
                              const ReferenceGenome& reference, const SampleList& samples) const;
};

BAMRealigner::Report realign(io::ReadReader::Path src, VcfReader::Path variants, io::ReadWriter::Path dst,
                             const ReferenceGenome& reference);

} // namespace octopus

#endif
