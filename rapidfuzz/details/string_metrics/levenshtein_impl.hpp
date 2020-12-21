/* SPDX-License-Identifier: MIT */
/* Copyright © 2020 Max Bachmann */

#include "rapidfuzz/utils.hpp"
#include <numeric>
#include <algorithm>
#include <array>
#include <limits>


namespace rapidfuzz {
namespace string_metric {
namespace detail {

static constexpr std::array<uint64_t, 64> levenshtein_hyrroe2003_masks = {
    0x0000000000000001, 0x0000000000000003, 0x0000000000000007, 0x000000000000000f,
    0x000000000000001f, 0x000000000000003f, 0x000000000000007f, 0x00000000000000ff,
    0x00000000000001ff, 0x00000000000003ff, 0x00000000000007ff, 0x0000000000000fff,
    0x0000000000001fff, 0x0000000000003fff, 0x0000000000007fff, 0x000000000000ffff,
    0x000000000001ffff, 0x000000000003ffff, 0x000000000007ffff, 0x00000000000fffff,
    0x00000000001fffff, 0x00000000003fffff, 0x00000000007fffff, 0x0000000000ffffff,
    0x0000000001ffffff, 0x0000000003ffffff, 0x0000000007ffffff, 0x000000000fffffff,
    0x000000001fffffff, 0x000000003fffffff, 0x000000007fffffff, 0x00000000ffffffff,
    0x00000001ffffffff, 0x00000003ffffffff, 0x00000007ffffffff, 0x0000000fffffffff,
    0x0000001fffffffff, 0x0000003fffffffff, 0x0000007fffffffff, 0x000000ffffffffff,
    0x000001ffffffffff, 0x000003ffffffffff, 0x000007ffffffffff, 0x00000fffffffffff,
    0x00001fffffffffff, 0x00003fffffffffff, 0x00007fffffffffff, 0x0000ffffffffffff,
    0x0001ffffffffffff, 0x0003ffffffffffff, 0x0007ffffffffffff, 0x000fffffffffffff,
    0x001fffffffffffff, 0x003fffffffffffff, 0x007fffffffffffff, 0x00ffffffffffffff,
    0x01ffffffffffffff, 0x03ffffffffffffff, 0x07ffffffffffffff, 0x0fffffffffffffff,
    0x1fffffffffffffff, 0x3fffffffffffffff, 0x7fffffffffffffff, 0xffffffffffffffff,
};

template <typename CharT1, typename CharT2>
std::size_t levenshtein_hyrroe2003(basic_string_view<CharT1> s1, basic_string_view<CharT2> s2)
{
  std::array<uint64_t, 256> posbits;

  for (std::size_t i = 0; i < s2.size(); i++){
    posbits[(unsigned char)s2[i]] |= (uint64_t)1 << i;
  }

  std::size_t dist = s2.size();
  uint64_t mask = (uint64_t)1 << (s2.size() - 1);
  uint64_t VP   = levenshtein_hyrroe2003_masks[s2.size() - 1];
  uint64_t VN   = 0;

  for (const auto& ch : s1) {
    uint64_t y = posbits[(unsigned char)ch];
    uint64_t X  = y | VN;
    uint64_t D0 = ((VP + (X & VP)) ^ VP) | X;
    uint64_t HN = VP & D0;
    uint64_t HP = VN | ~(VP|D0);
    X  = (HP << 1) | 1;
    VN =  X & D0;
    VP = (HN << 1) | ~(X | D0);
    if (HP & mask) { dist++; }
    if (HN & mask) { dist--; }
  }

  return dist;
}

template <typename CharT1, typename CharT2>
std::size_t levenshtein_wagner_fischer(basic_string_view<CharT1> s1, basic_string_view<CharT2> s2, std::size_t max)
{
  if (max > s1.size()) {
    max = s1.size();
  }

  const std::size_t len_diff = s1.size() - s2.size();
  std::vector<std::size_t> cache(s1.size());
  std::iota(cache.begin(), cache.begin() + max, 1);
  std::fill(cache.begin() + max, cache.end(), max + 1);

  const std::size_t offset = max - len_diff;
  const bool haveMax = max < s1.size();

  std::size_t jStart = 0;
  std::size_t jEnd = max;

  std::size_t current = 0;
  std::size_t left;
  std::size_t above;
  std::size_t s2_pos = 0;

  for (const auto& char2 : s2) {
    left = s2_pos;
    above = s2_pos + 1;

    jStart += (s2_pos > offset) ? 1 : 0;
    jEnd += (jEnd < s1.size()) ? 1 : 0;

    for (std::size_t j = jStart; j < jEnd; j++) {
      above = current;
      current = left;
      left = cache[j];

      if (char2 != s1[j]) {

        // Insertion
        if (left < current) current = left;

        // Deletion
        if (above < current) current = above;

        ++current;
      }

      cache[j] = current;
    }

    if (haveMax && cache[s2_pos + len_diff] > max) {
      return std::numeric_limits<std::size_t>::max();
    }
    ++s2_pos;
  }

  return (cache.back() <= max) ? cache.back() : std::numeric_limits<std::size_t>::max();
}

template <typename CharT1, typename CharT2>
std::size_t levenshtein(basic_string_view<CharT1> s1, basic_string_view<CharT2> s2, std::size_t max)
{
  // Swapping the strings so the second string is shorter
  if (s1.size() < s2.size()) {
    return levenshtein(s2, s1, max);
  }

  // when no differences are allowed a direct comparision is sufficient
  if (max == 0) {
    if (s1.size() != s2.size()) {
      return -1;
    }
    return std::equal(s1.begin(), s1.end(), s2.begin()) ? 0 : -1;
  }

  // at least length difference insertions/deletions required
  if (s1.size() - s2.size() > max) {
    return -1;
  }

  // The Levenshtein distance between <prefix><string1><suffix> and <prefix><string2><suffix>
  // is similar to the distance between <string1> and <string2>, so they can be removed in linear time
  utils::remove_common_affix(s1, s2);

  if (s2.empty()) {
    return (s1.size() > max) ? -1 : s1.size();
  }

  if (s1.size() - s2.size() > max) {
    return -1;
  }

  // when both strings only hold characters < 256 and the short strings has less
  // then 65 elements Hyyrös' algorithm can be used
  if (sizeof(CharT1) == 1 && sizeof(CharT2) == 1) {
    if (s2.size() < 65) {
      std::size_t dist = levenshtein_hyrroe2003(s1, s2);
      return (dist > max) ? -1 : dist;
    }
  }

  // replace this with myers algorithm in the future
  return levenshtein_wagner_fischer(s1, s2, max);
}


template <typename CharT1, typename CharT2>
double normalized_levenshtein(basic_string_view<CharT1> s1, basic_string_view<CharT2> s2, const double score_cutoff)
{
  if (s1.empty() || s2.empty()) {
    return s1.empty() && s2.empty();
  }

  std::size_t max_len = std::max(s1.size(), s2.size());

  auto cutoff_distance = utils::score_cutoff_to_distance(score_cutoff, max_len);

  std::size_t dist = levenshtein(s1, s2, cutoff_distance);
  return (dist != (std::size_t)-1)
    ? utils::norm_distance(dist, max_len, score_cutoff)
    : 0.0;
}

} // namespace detail
} // namespace levenshtein
} // namespace rapidfuzz
