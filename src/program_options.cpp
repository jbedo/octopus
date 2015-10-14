//
//  program_options.cpp
//  Octopus
//
//  Created by Daniel Cooke on 27/02/2015.
//  Copyright (c) 2015 Oxford University. All rights reserved.
//

#include "program_options.hpp"

#include <iostream>
#include <fstream>
#include <stdexcept>
#include <iterator>
#include <algorithm>  // std::transform, std::min
#include <functional> // std::function
#include <unordered_map>
#include <memory>     // std::make_unique
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/operations.hpp>

#include "genomic_region.hpp"
#include "reference_genome.hpp"
#include "aligned_read.hpp"
#include "read_manager.hpp"

#include "read_filters.hpp"
#include "read_transform.hpp"
#include "read_transformations.hpp"
#include "candidate_generators.hpp"

#include "variant_caller_factory.hpp"

#include "vcf_reader.hpp"
#include "vcf_writer.hpp"

#include "mappable_algorithms.hpp"
#include "string_utils.hpp"

#include "maths.hpp"

namespace Octopus
{
    namespace Options
    {
    
    void conflicting_options(const po::variables_map& vm, const std::string& opt1, const std::string& opt2)
    {
        if (vm.count(opt1) && !vm[opt1].defaulted() && vm.count(opt2) && !vm[opt2].defaulted()) {
            throw std::logic_error(std::string("Conflicting options '") + opt1 + "' and '" + opt2 + "'.");
        }
    }
    
    void option_dependency(const po::variables_map& vm, const std::string& for_what,
                           const std::string& required_option)
    {
        if (vm.count(for_what) && !vm[for_what].defaulted())
            if (vm.count(required_option) == 0 || vm[required_option].defaulted()) {
                throw std::logic_error(std::string("Option '") + for_what
                                       + "' requires option '" + required_option + "'.");
            }
    }
    
    po::variables_map parse_options(int argc, const char** argv)
    {
        po::positional_options_description p;
        p.add("command", -1);
        
        po::options_description general("General options");
        general.add_options()
        ("help,h", "produce help message")
        ("version", "output the version number")
        ("verbosity", po::value<unsigned>()->default_value(0), "level of logging. Verbosity 0 switches off logging")
        ;
        
        po::options_description backend("Backend options");
        backend.add_options()
        ("max-threads,t", po::value<unsigned>()->default_value(1), "maximum number of threads")
        ("memory", po::value<size_t>()->default_value(8000), "target memory usage in MB")
        ("reference-cache-size", po::value<size_t>()->default_value(0), "the maximum number of bytes that can be used to cache reference sequence")
        ("compress-reads", po::bool_switch()->default_value(false), "compress the reads (slower)")
        ("max-open-files", po::value<unsigned>()->default_value(200), "the maximum number of files that can be open at one time")
        ;
        
        po::options_description input("Input/output options");
        input.add_options()
        ("reference,R", po::value<std::string>()->required(), "the reference genome file")
        ("reads,I", po::value<std::vector<std::string>>()->multitoken(), "space-seperated list of read file paths")
        ("reads-file", po::value<std::string>(), "list of read file paths, one per line")
        ("regions", po::value<std::vector<std::string>>()->multitoken(), "space-seperated list of one-indexed variant search regions (chrom:begin-end)")
        ("regions-file", po::value<std::string>(), "list of one-indexed variant search regions (chrom:begin-end), one per line")
        ("skip-regions", po::value<std::vector<std::string>>()->multitoken(), "space-seperated list of one-indexed regions (chrom:begin-end) to skip")
        ("skip-regions-file", po::value<std::string>(), "list of one-indexed regions (chrom:begin-end) to skip, one per line")
        ("samples,S", po::value<std::vector<std::string>>()->multitoken(), "space-seperated list of sample names to consider")
        ("samples-file", po::value<std::string>(), "list of sample names to consider, one per line")
        ("output,o", po::value<std::string>()->default_value("octopus_variants.vcf"), "write output to file")
        //("log-file", po::value<std::string>(), "path of the output log file")
        ;
        
        po::options_description filters("Read filter options");
        filters.add_options()
        ("no-unmapped", po::bool_switch()->default_value(false), "filter reads marked as unmapped")
        ("min-mapping-quality", po::value<unsigned>()->default_value(20), "reads with smaller mapping quality are ignored")
        ("good-base-quality", po::value<unsigned>()->default_value(20), "base quality threshold used by min-good-bases filter")
        ("min-good-base-fraction", po::value<double>(), "base quality threshold used by min-good-bases filter")
        ("min-good-bases", po::value<AlignedRead::SizeType>()->default_value(0), "minimum number of bases with quality min-base-quality before read is considered")
        ("no-qc-fails", po::bool_switch()->default_value(false), "filter reads marked as QC failed")
        ("min-read-length", po::value<AlignedRead::SizeType>(), "filter reads shorter than this")
        ("max-read-length", po::value<AlignedRead::SizeType>(), "filter reads longer than this")
        ("remove-duplicate-reads", po::bool_switch()->default_value(false), "filters duplicate reads")
        ("no-secondary-alignmenets", po::bool_switch()->default_value(false), "filters reads marked as secondary alignments")
        ("no-supplementary-alignmenets", po::bool_switch()->default_value(false), "filters reads marked as supplementary alignments")
        ("no-unmapped-mates", po::bool_switch()->default_value(false), "filters reads with unmapped mates")
        ("downsample-above", po::value<unsigned>()->default_value(10000), "downsample reads in regions where coverage is over this")
        ("downsample-target", po::value<unsigned>()->default_value(10000), "the target coverage for the downsampler")
        ;
        
        po::options_description transforms("Read transform options");
        transforms.add_options()
        ("trim-soft-clipped", po::bool_switch()->default_value(false), "trims soft clipped parts of the read")
        ("tail-trim-size", po::value<AlignedRead::SizeType>()->default_value(0), "trims this number of bases off the tail of all reads")
        ("trim-adapters", po::bool_switch()->default_value(true), "trims any overlapping regions that pass the fragment size")
        ;
        
        po::options_description candidates("Candidate generation options");
        candidates.add_options()
        ("candidates-from-alignments", po::bool_switch()->default_value(true), "generate candidate variants from the aligned reads")
        ("candidates-from-assembler", po::bool_switch()->default_value(false), "generate candidate variants with the assembler")
        ("candidates-from-source", po::value<std::string>(), "variant file path containing known variants. These variants will automatically become candidates")
        ("min-snp-base-quality", po::value<unsigned>()->default_value(20), "only base changes with quality above this value are considered for snp generation")
        ("min-supporting-reads", po::value<unsigned>()->default_value(1), "minimum number of reads that must support a variant if it is to be considered a candidate")
        ("max-variant-size", po::value<AlignedRead::SizeType>()->default_value(100), "maximum candidate varaint size from alignmenet CIGAR")
        ("kmer-size", po::value<unsigned>()->default_value(15), "k-mer size to use for assembly")
        ("no-cycles", po::bool_switch()->default_value(false), "dissalow cycles in assembly graph")
        ;
        
        po::options_description model("Model options");
        model.add_options()
        ("model", po::value<std::string>()->default_value("population"), "the calling model used")
        ("ploidy", po::value<unsigned>()->default_value(2), "the organism ploidy, all contigs with unspecified ploidy are assumed this ploidy")
        ("contig-ploidies", po::value<std::vector<std::string>>()->multitoken(), "the ploidy of individual contigs")
        ("contig-ploidies-file", po::value<std::string>(), "list of contig=ploidy pairs, one per line")
        ("normal-sample", po::value<std::string>(), "the normal sample used in cancer calling model")
        ("transition-prior", po::value<double>()->default_value(0.003), "the prior probability of a transition snp from the reference")
        ("transversion-prior", po::value<double>()->default_value(0.003), "the prior probability of a transversion snp from the reference")
        ("insertion-prior", po::value<double>()->default_value(0.003), "the prior probability of an insertion into the reference")
        ("deletion-prior", po::value<double>()->default_value(0.003), "the prior probability of a deletion from the reference")
        ("prior-precision", po::value<double>()->default_value(0.003), "the precision (inverse variance) of the given variant priors")
        ;
        
        po::options_description calling("Caller options");
        calling.add_options()
        ("min-variant-posterior", po::value<unsigned>()->default_value(20), "the minimum variant call posterior probability (phred scale)")
        ("min-refcall-posterior", po::value<unsigned>()->default_value(10), "the minimum homozygous reference call posterior probability (phred scale)")
        ("min-somatic-posterior", po::value<unsigned>()->default_value(10), "the minimum somaitc mutation call posterior probability (phred scale)")
        ("make-positional-refcalls", po::bool_switch()->default_value(false), "caller will output positional REFCALLs")
        ("make-blocked-refcalls", po::bool_switch()->default_value(false), "caller will output blocked REFCALLs")
        ;
        
        po::options_description all("Allowed options");
        all.add(general).add(backend).add(input).add(filters).add(transforms).add(candidates).add(model).add(calling);
        
        po::variables_map vm;
        po::store(po::command_line_parser(argc, argv).options(all).positional(p).run(), vm);
        
        if (vm.count("help")) {
            std::cout << "Usage: octopus <command> [options]" << std::endl;
            std::cout << all << std::endl;
            return vm;
        }
        
        // boost::option cannot handle option dependencies so we must do our own checks
        
        if (vm.count("reads") == 0 && vm.count("reads-file") == 0) {
            throw boost::program_options::required_option {"--reads | --reads-file"};
        }
        
        if (vm.at("model").as<std::string>() == "cancer" && vm.count("normal-sample") == 0) {
            throw std::logic_error {"Option model requires option normal-sample when model=cancer"};
        }
        
        conflicting_options(vm, "make-positional-refcalls", "make-blocked-refcalls");
        
        po::notify(vm);
        
        return vm;
    }
    
    namespace detail
    {
        bool is_region_file_path(const std::string& region_option)
        {
            return fs::native(region_option);
        }
        
        struct Line
        {
            std::string line_data;
            
            operator std::string() const
            {
                return line_data;
            }
        };
        
        std::istream& operator>>(std::istream& str, Line& data)
        {
            std::getline(str, data.line_data);
            return str;
        }
        
        std::string to_region_format(const std::string& bed_line)
        {
            auto tokens = split(bed_line, '\t');
            
            switch (tokens.size()) {
                case 0:
                    throw std::runtime_error {"Empty line in input region bed file"};
                case 1:
                    return std::string {tokens[0]};
                case 2:
                    // Assume this represents a half range rather than a point
                    return std::string {tokens[0] + ':' + tokens[1] + '-'};
                default:
                    return std::string {tokens[0] + ':' + tokens[1] + '-' + tokens[2]};
            }
        }
        
        std::function<GenomicRegion(std::string)> get_line_parser(const fs::path& the_region_path,
                                                                  const ReferenceGenome& the_reference)
        {
            if (the_region_path.extension().string() == ".bed") {
                return [&the_reference] (const std::string& line) {
                    return parse_region(detail::to_region_format(line), the_reference);
                };
            } else {
                return [&the_reference] (const std::string& line) {
                    return parse_region(line, the_reference);
                };
            }
        }
        
        std::vector<GenomicRegion> get_regions_from_file(const std::string& file_path, const ReferenceGenome& the_reference)
        {
            std::vector<GenomicRegion> result {};
            
            fs::path the_path {file_path};
            
            if (!fs::exists(the_path)) {
                throw std::runtime_error {"cannot find given region file " + the_path.string()};
            }
            
            std::ifstream the_file {the_path.string()};
            
            std::transform(std::istream_iterator<Line>(the_file), std::istream_iterator<Line>(),
                           std::back_inserter(result), get_line_parser(the_path, the_reference));
            
            return result;
        }
        
        SearchRegions make_search_regions(const std::vector<GenomicRegion>& regions)
        {
            SearchRegions contig_mapped_regions {};
            
            for (const auto& region : regions) {
                contig_mapped_regions[region.get_contig_name()].insert(region);
            }
            
            SearchRegions result {};
            
            for (auto& contig_regions : contig_mapped_regions) {
                auto covered_contig_regions = get_covered_regions(std::cbegin(contig_regions.second),
                                                                  std::cend(contig_regions.second));
                result[contig_regions.first].insert(std::make_move_iterator(std::begin(covered_contig_regions)),
                                                    std::make_move_iterator(std::end(covered_contig_regions)));
            }
            
            return result;
        }
        
        SearchRegions get_all_regions_not_skipped(const ReferenceGenome& the_reference, std::vector<GenomicRegion>& skip_regions)
        {
            if (skip_regions.empty()) {
                return make_search_regions(get_all_contig_regions(the_reference));
            } else {
                auto skipped = make_search_regions(skip_regions);
                
                SearchRegions result {};
                
                return result;
            }
        }
        
        std::vector<std::string> get_read_paths_file(const std::string& file_path)
        {
            std::vector<std::string> result {};
            
            fs::path the_path {file_path};
            
            if (!fs::exists(the_path)) {
                throw std::runtime_error {"cannot find given read path file " + the_path.string()};
            }
            
            std::ifstream the_file {the_path.string()};
            
            std::transform(std::istream_iterator<Line>(the_file), std::istream_iterator<Line>(),
                           std::back_inserter(result), [] (const Line& line) { return line.line_data; });
            
            return result;
        }
    } // namespace detail
    
    unsigned get_max_threads(const po::variables_map& options)
    {
        return options.at("max-threads").as<unsigned>();
    }
    
    size_t get_memory_quota(const po::variables_map& options)
    {
        return options.at("memory").as<size_t>();
    }
    
    ReferenceGenome get_reference(const po::variables_map& options)
    {
        auto cache_size = options.at("reference-cache-size").as<size_t>();
        return make_reference(options.at("reference").as<std::string>(),
                              static_cast<ReferenceGenome::SizeType>(cache_size));
    }
    
    SearchRegions get_search_regions(const po::variables_map& options, const ReferenceGenome& the_reference)
    {
        std::vector<GenomicRegion> input_regions {};
        
        if (options.count("regions") == 0 && options.count("regions-file") == 0) {
            std::vector<GenomicRegion> skip_regions {};
            
            if (options.count("skip-regions") == 1) {
                const auto& regions = options.at("skip-regions").as<std::vector<std::string>>();
                skip_regions.reserve(skip_regions.size());
                std::transform(std::cbegin(regions), std::cend(regions), std::back_inserter(skip_regions),
                               [&the_reference] (const auto& region) {
                                   return parse_region(region, the_reference);
                               });
            }
            
            if (options.count("skip-regions-file") == 1) {
                const auto& skip_path = options.at("skip-regions-file").as<std::string>();
                auto skip_regions_from_file = detail::get_regions_from_file(skip_path, the_reference);
                skip_regions.insert(skip_regions.end(), std::make_move_iterator(std::begin(skip_regions_from_file)),
                                    std::make_move_iterator(std::end(skip_regions_from_file)));
            }
            
            return detail::get_all_regions_not_skipped(the_reference, skip_regions);
        } else {
            if (options.count("regions") == 1) {
                const auto& regions = options.at("regions").as<std::vector<std::string>>();
                input_regions.reserve(regions.size());
                std::transform(std::cbegin(regions), std::cend(regions), std::back_inserter(input_regions),
                               [&the_reference] (const auto& region) {
                                   return parse_region(region, the_reference);
                               });
            }
            
            if (options.count("regions-file") == 1) {
                const auto& regions_path = options.at("regions-file").as<std::string>();
                auto regions_from_file = detail::get_regions_from_file(regions_path, the_reference);
                input_regions.insert(input_regions.end(), std::make_move_iterator(std::begin(regions_from_file)),
                                    std::make_move_iterator(std::end(regions_from_file)));
            }
        }
        
        return detail::make_search_regions(input_regions);
    }
    
    std::vector<SampleIdType> get_samples(const po::variables_map& options)
    {
        std::vector<SampleIdType> result {};
        
        if (options.count("samples") == 1) {
            auto samples = options.at("samples").as<std::vector<std::string>>();
            result.reserve(samples.size());
            std::copy(std::cbegin(samples), std::cend(samples), std::back_inserter(result));
        }
        
        return result;
    }
    
    std::vector<fs::path> get_read_paths(const po::variables_map& options)
    {
        std::vector<fs::path> result {};
        
        if (options.count("reads") == 1) {
            const auto& read_paths = options.at("reads").as<std::vector<std::string>>();
            result.insert(result.end(), std::cbegin(read_paths), std::cend(read_paths));
        }
        
        if (options.count("reads-file") == 1) {
            const auto& read_file_path = options.at("reads-file").as<std::string>();
            auto regions_from_file = detail::get_read_paths_file(read_file_path);
            result.insert(result.end(), std::make_move_iterator(std::begin(regions_from_file)),
                          std::make_move_iterator(std::end(regions_from_file)));
        }
        
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        
        return result;
    }
    
    ReadManager get_read_manager(const po::variables_map& options)
    {
        return ReadManager {get_read_paths(options), options.at("max-open-files").as<unsigned>()};
    }
    
    ReadFilter<ReadContainer::const_iterator> get_read_filter(const po::variables_map& options)
    {
        using QualityType = AlignedRead::QualityType;
        using SizeType    = AlignedRead::SizeType;
        
        ReadFilter<ReadContainer::const_iterator> result {};
        
        if (options.at("no-unmapped").as<bool>()) {
            result.register_filter(ReadFilters::is_mapped());
        }
        
        auto min_mapping_quality = options.at("min-mapping-quality").as<unsigned>();
        
        if (min_mapping_quality > 0) {
            result.register_filter(ReadFilters::is_good_mapping_quality(min_mapping_quality));
        }
        
        auto min_base_quality = options.at("good-base-quality").as<unsigned>();
        auto min_good_bases = options.at("min-good-bases").as<unsigned>();
        
        if (min_good_bases > 0) {
            result.register_filter(ReadFilters::has_sufficient_good_quality_bases(min_base_quality, min_good_bases));
        }
        
        if (options.count("min-good-base-fraction") == 1) {
            auto min_good_base_fraction =  options.at("min-good-base-fraction").as<double>();
            result.register_filter(ReadFilters::has_good_base_fraction(min_base_quality, min_good_base_fraction));
        }
        
        if (options.count("min-read-length") == 1) {
            result.register_filter(ReadFilters::is_short(options.at("min-read-length").as<SizeType>()));
        }
        
        if (options.count("max-read-length") == 1) {
            result.register_filter(ReadFilters::is_long(options.at("max-read-length").as<SizeType>()));
        }
        
        if (options.at("remove-duplicate-reads").as<bool>()) {
            result.register_filter(ReadFilters::is_not_duplicate());
        }
        
        if (options.at("no-qc-fails").as<bool>()) {
            result.register_filter(ReadFilters::is_not_marked_qc_fail());
        }
        
        if (options.at("no-secondary-alignmenets").as<bool>()) {
            result.register_filter(ReadFilters::is_not_secondary_alignment());
        }
        
        if (options.at("no-supplementary-alignmenets").as<bool>()) {
            result.register_filter(ReadFilters::is_not_supplementary_alignment());
        }
        
        if (options.at("no-unmapped-mates").as<bool>()) {
            result.register_filter(ReadFilters::mate_is_mapped());
        }
        
        return result;
    }
    
    Downsampler<SampleIdType> get_downsampler(const po::variables_map& options)
    {
        auto max_coverage    = options.at("downsample-above").as<unsigned>();
        auto target_coverage = options.at("downsample-target").as<unsigned>();
        
        return Downsampler<SampleIdType>(max_coverage, target_coverage);
    }
    
    ReadTransform get_read_transformer(const po::variables_map& options)
    {
        using SizeType = AlignedRead::SizeType;
        
        ReadTransform result {};
        
        bool trim_soft_clipped = options.at("trim-soft-clipped").as<bool>();
        
        auto tail_trim_size = options.at("tail-trim-size").as<SizeType>();
        
        if (trim_soft_clipped && tail_trim_size > 0) {
            result.register_transform(ReadTransforms::trim_soft_clipped_tails(tail_trim_size));
        } else if (tail_trim_size > 0) {
            result.register_transform(ReadTransforms::trim_tail(tail_trim_size));
        } else if (trim_soft_clipped) {
            result.register_transform(ReadTransforms::trim_soft_clipped());
        }
        
        if (options.at("trim-adapters").as<bool>()) {
            result.register_transform(ReadTransforms::trim_adapters());
        }
        
        return result;
    }
    
    CandidateVariantGenerator get_candidate_generator(const po::variables_map& options, ReferenceGenome& reference)
    {
        CandidateVariantGenerator result {};
        
        auto max_variant_size = options.at("max-variant-size").as<AlignmentCandidateVariantGenerator::SizeType>();
        
        if (options.at("candidates-from-alignments").as<bool>()) {
            auto min_snp_base_quality = options.at("min-snp-base-quality").as<unsigned>();
            auto min_supporting_reads = options.at("min-supporting-reads").as<unsigned>();
            
            if (min_supporting_reads == 0) ++min_supporting_reads; // probably input error; 0 is meaningless
            
            result.register_generator(std::make_unique<AlignmentCandidateVariantGenerator>(reference, min_snp_base_quality,
                                                                                           min_supporting_reads, max_variant_size));
        }
        
        if (options.at("candidates-from-assembler").as<bool>()) {
            auto kmer_size    = options.at("kmer-size").as<unsigned>();
            //auto allow_cycles = !options.at("no-cycles").as<bool>();
            result.register_generator(std::make_unique<AssemblerCandidateVariantGenerator>(reference, kmer_size, max_variant_size));
        }
        
        if (options.count("candidates-from-source") == 1) {
            auto variant_file_path = options.at("candidates-from-source").as<std::string>();
            result.register_generator(std::make_unique<ExternalCandidateVariantGenerator>(VcfReader {variant_file_path}));
        }
        
        return result;
    }
    
    std::unique_ptr<VariantCaller> get_variant_caller(const po::variables_map& options, ReferenceGenome& reference,
                                                      CandidateVariantGenerator& candidate_generator,
                                                      const GenomicRegion::StringType& contig)
    {
        const auto& model = options.at("model").as<std::string>();
        
        auto refcall_type = VariantCaller::RefCallType::None;
        
        if (options.at("make-positional-refcalls").as<bool>()) {
            refcall_type = VariantCaller::RefCallType::Positional;
        } else if (options.at("make-blocked-refcalls").as<bool>()) {
            refcall_type = VariantCaller::RefCallType::Blocked;
        }
        
        auto ploidy = options.at("ploidy").as<unsigned>();
        
        if (options.count("contig-ploidies") == 1) {
            auto contig_ploidies = options.at("contig-ploidies").as<std::vector<std::string>>();
            
            for (const auto& contig_ploidy : contig_ploidies) {
                if (contig_ploidy.find(contig) == 0) {
                    if (contig_ploidy[contig.size()] != '=') {
                        throw std::runtime_error {"Could not pass contig-plodies option"};
                    }
                    ploidy = static_cast<unsigned>(std::stoul(contig_ploidy.substr(contig.size() + 1)));
                }
            }
        } else if (options.count("contig-ploidies-file") == 1) {
            // TODO: fetch from file
        }
        
        auto min_variant_posterior_phred = options.at("min-variant-posterior").as<unsigned>();
        auto min_variant_posterior        = Maths::phred_to_probability(min_variant_posterior_phred);
        
        auto min_refcall_posterior_phred = options.at("min-refcall-posterior").as<unsigned>();
        auto min_refcall_posterior       = Maths::phred_to_probability(min_refcall_posterior_phred);
        
        SampleIdType normal_sample {};
        double min_somatic_posterior {};
        if (model == "cancer") {
            normal_sample = options.at("normal-sample").as<std::string>();
            auto min_somatic_posterior_phred = options.at("min-somatic-posterior").as<unsigned>();
            min_somatic_posterior = Maths::phred_to_probability(min_somatic_posterior_phred);
        }
        
        return make_variant_caller(model, reference, candidate_generator, refcall_type,
                                   min_variant_posterior, min_refcall_posterior,
                                   ploidy, normal_sample, min_somatic_posterior);
    }
    
    std::unique_ptr<VariantCaller> get_variant_caller(const po::variables_map& options, ReferenceGenome& reference,
                                                      CandidateVariantGenerator& candidate_generator)
    {
        const auto& model = options.at("model").as<std::string>();
        
        auto refcall_type = VariantCaller::RefCallType::None;
        
        if (options.at("make-positional-refcalls").as<bool>()) {
            refcall_type = VariantCaller::RefCallType::Positional;
        } else if (options.at("make-blocked-refcalls").as<bool>()) {
            refcall_type = VariantCaller::RefCallType::Blocked;
        }
        
        auto ploidy = options.at("ploidy").as<unsigned>();
        
        auto min_variant_posterior_phred = options.at("min-variant-posterior").as<unsigned>();
        auto min_variant_posterior        = Maths::phred_to_probability(min_variant_posterior_phred);
        
        auto min_refcall_posterior_phred = options.at("min-refcall-posterior").as<unsigned>();
        auto min_refcall_posterior       = Maths::phred_to_probability(min_refcall_posterior_phred);
        
        SampleIdType normal_sample {};
        double min_somatic_posterior {};
        if (model == "cancer") {
            normal_sample = options.at("normal-sample").as<std::string>();
            auto min_somatic_posterior_phred = options.at("min-somatic-posterior").as<unsigned>();
            min_somatic_posterior = Maths::phred_to_probability(min_somatic_posterior_phred);
        }
        
        return make_variant_caller(model, reference, candidate_generator, refcall_type,
                                   min_variant_posterior, min_refcall_posterior,
                                   ploidy, normal_sample, min_somatic_posterior);
    }
    
    VcfWriter get_output_vcf(const po::variables_map& options)
    {
        return VcfWriter {options.at("output").as<std::string>()};
    }
    
    } // namespace Options
} // namespace Octopus
