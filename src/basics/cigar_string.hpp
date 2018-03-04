// Copyright (c) 2015-2018 Daniel Cooke
// Use of this source code is governed by the MIT license that can be found in the LICENSE file.

#ifndef cigar_string_hpp
#define cigar_string_hpp

#include <string>
#include <cstdint>
#include <iterator>
#include <vector>
#include <numeric>
#include <iosfwd>
#include <functional>

#include <boost/functional/hash.hpp>

#include "concepts/comparable.hpp"

namespace octopus {

class CigarOperation : public Comparable<CigarOperation> // Comparable so can compare reads
{
public:
    enum class Flag : char
    {
        alignmentMatch = 'M',
        sequenceMatch  = '=',
        substitution   = 'X',
        insertion      = 'I',
        deletion       = 'D',
        softClipped    = 'S',
        hardClipped    = 'H',
        padding        = 'P',
        skipped        = 'N'
    };
    
    using Size = std::uint_fast32_t;
    
    CigarOperation() = default;
    
    explicit CigarOperation(Size size, Flag type) noexcept;
    
    CigarOperation(const CigarOperation&)            = default;
    CigarOperation& operator=(const CigarOperation&) = default;
    CigarOperation(CigarOperation&&)                 = default;
    CigarOperation& operator=(CigarOperation&&)      = default;
    
    ~CigarOperation() = default;
    
    Flag flag() const noexcept;
    
    Size size() const noexcept;
    
    bool advances_reference() const noexcept;
        
    bool advances_sequence() const noexcept;
    
private:
    Size size_;
    Flag flag_;
};

bool is_match(const CigarOperation& op) noexcept;
bool is_indel(const CigarOperation& op) noexcept;
bool is_clipping(const CigarOperation& op) noexcept;

// CigarString

using CigarString = std::vector<CigarOperation>;

CigarString parse_cigar(const std::string& cigar);

// Valid if non-empty and all operations are valid
bool is_valid(const CigarString& cigar) noexcept;

// Minimal if all adjacent operations are unique
bool is_minimal(const CigarString& cigar) noexcept;

bool is_front_soft_clipped(const CigarString& cigar) noexcept;

bool is_back_soft_clipped(const CigarString& cigar) noexcept;

bool is_soft_clipped(const CigarString& cigar) noexcept;

std::pair<CigarOperation::Size, CigarOperation::Size> get_soft_clipped_sizes(const CigarString& cigar) noexcept;

template <typename S = CigarOperation::Size>
S clipped_begin(const CigarString& cigar, S unclipped_begin) noexcept
{
    if (is_front_soft_clipped(cigar)) {
        unclipped_begin -= static_cast<S>(cigar.front().size());
    }
    return unclipped_begin;
}

template <typename S = CigarOperation::Size>
S sum_operation_sizes(const CigarString& cigar) noexcept
{
    return std::accumulate(std::cbegin(cigar), std::cend(cigar), S {0},
                           [] (const S curr, const CigarOperation& op) {
                               return curr + op.size();
                           });
}

template <typename S = CigarOperation::Size>
S reference_size(const CigarString& cigar) noexcept
{
    return std::accumulate(std::cbegin(cigar), std::cend(cigar), S {0},
                           [] (const S curr, const CigarOperation& op) {
                               return curr + ((op.advances_reference()) ? op.size() : 0);
                           });
}

template <typename S = CigarOperation::Size>
S sequence_size(const CigarString& cigar) noexcept
{
    return std::accumulate(std::cbegin(cigar), std::cend(cigar), S {0},
                           [] (const S curr, const CigarOperation& op) {
                               return curr + ((op.advances_sequence()) ? op.size() : 0);
                           });
}

template <typename S = CigarOperation::Size>
CigarOperation get_operation_at_sequence_position(const CigarString& cigar, S pos)
{
    auto first = std::cbegin(cigar);
    while (pos >= first->size()) {
        ++first;
        pos -= first->size();
    }
    return *first;
}

// Relative to both reference and sequence
CigarString copy(const CigarString& cigar, CigarOperation::Size offset, CigarOperation::Size size);
CigarString copy_reference(const CigarString& cigar, CigarOperation::Size offset, CigarOperation::Size size);
CigarString copy_sequence(const CigarString& cigar, CigarOperation::Size offset, CigarOperation::Size size);

std::vector<CigarOperation::Flag> decompose(const CigarString& cigar);
CigarString collapse_matches(const CigarString& cigar);

bool operator==(const CigarOperation& lhs, const CigarOperation& rhs) noexcept;
bool operator<(const CigarOperation& lhs, const CigarOperation& rhs) noexcept;

std::ostream& operator<<(std::ostream& os, const CigarOperation::Flag& flag);
std::ostream& operator<<(std::ostream& os, const CigarOperation& cigar_operation);
std::ostream& operator<<(std::ostream& os, const CigarString& cigar);

struct CigarHash
{
    std::size_t operator()(const CigarOperation& op) const noexcept;
    std::size_t operator()(const CigarString& cigar) const noexcept;
};

} // namespace octopus

namespace std {
    template <> struct hash<octopus::CigarOperation>
    {
        size_t operator()(const octopus::CigarOperation& op) const noexcept
        {
            return octopus::CigarHash()(op);
        }
    };
    
    template <> struct hash<octopus::CigarString>
    {
        size_t operator()(const octopus::CigarString& cigar) const noexcept
        {
            return octopus::CigarHash()(cigar);
        }
    };
} // namespace std

namespace boost {
    template <>
    struct hash<octopus::CigarOperation> : std::unary_function<octopus::CigarOperation, std::size_t>
    {
        std::size_t operator()(const octopus::CigarOperation& op) const noexcept
        {
            return std::hash<octopus::CigarOperation>()(op);
        }
    };
    
    template <>
    struct hash<octopus::CigarString> : std::unary_function<octopus::CigarString, std::size_t>
    {
        std::size_t operator()(const octopus::CigarString& cigar) const noexcept
        {
            return std::hash<octopus::CigarString>()(cigar);
        }
    };
} // namespace boost

#endif
