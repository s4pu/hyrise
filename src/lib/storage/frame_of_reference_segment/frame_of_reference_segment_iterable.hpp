#pragma once

#include <type_traits>

#include "storage/base_segment.hpp"
#include "storage/frame_of_reference_segment.hpp"
#include "storage/segment_iterables.hpp"
#include "storage/vector_compression/resolve_compressed_vector_type.hpp"

namespace opossum {

template <typename T>
class FrameOfReferenceSegmentIterable : public PointAccessibleSegmentIterable<FrameOfReferenceSegmentIterable<T>> {
 public:
  using ValueType = T;

  explicit FrameOfReferenceSegmentIterable(const FrameOfReferenceSegment<T>& segment) : _segment{segment} {}

  template <typename Functor>
  void _on_with_iterators(const Functor& functor) const {
    _segment.access_counter[SegmentAccessCounter::AccessType::Sequential] += _segment.size();
    resolve_compressed_vector_type(_segment.offset_values(), [&](const auto& offset_values) {
      using OffsetValueIteratorT = decltype(offset_values.cbegin());

      auto begin = Iterator<OffsetValueIteratorT>{_segment.block_minima().cbegin(), offset_values.cbegin(),
                                                  _segment.null_values().cbegin(), ChunkOffset{0}};

      auto end =
          Iterator<OffsetValueIteratorT>{_segment.block_minima().cend(), offset_values.cend(),
                                         _segment.null_values().cend(), static_cast<ChunkOffset>(_segment.size())};

      functor(begin, end);
    });
  }

  template <typename Functor, typename PosListType>
  void _on_with_iterators(const std::shared_ptr<PosListType>& position_filter, const Functor& functor) const {
    _segment.access_counter[SegmentAccessCounter::access_type(*position_filter)] += position_filter->size();
    resolve_compressed_vector_type(_segment.offset_values(), [&](const auto& vector) {
      auto decompressor = vector.create_decompressor();
      using OffsetValueDecompressorT = std::decay_t<decltype(decompressor)>;

      using PosListIteratorType = std::decay_t<decltype(position_filter->cbegin())>;
      auto begin = PointAccessIterator<OffsetValueDecompressorT, PosListIteratorType>{
          &_segment.block_minima(), &_segment.null_values(), std::move(decompressor), position_filter->cbegin(),
          position_filter->cbegin()};
      auto end = PointAccessIterator<OffsetValueDecompressorT, PosListIteratorType>{position_filter->cbegin(),
                                                                                    position_filter->cend()};
      functor(begin, end);
    });
  }

  size_t _on_size() const { return _segment.size(); }

 private:
  const FrameOfReferenceSegment<T>& _segment;

 private:
  template <typename OffsetValueIteratorT>
  class Iterator : public BaseSegmentIterator<Iterator<OffsetValueIteratorT>, SegmentPosition<T>> {
   public:
    using ValueType = T;
    using IterableType = FrameOfReferenceSegmentIterable<T>;
    using ReferenceFrameIterator = typename pmr_vector<T>::const_iterator;
    using NullValueIterator = typename pmr_vector<bool>::const_iterator;

   public:
    explicit Iterator(ReferenceFrameIterator block_minimum_it, OffsetValueIteratorT offset_value_it,
                      NullValueIterator null_value_it, ChunkOffset chunk_offset)
        : _block_minimum_it{std::move(block_minimum_it)},
          _offset_value_it{std::move(offset_value_it)},
          _null_value_it{std::move(null_value_it)},
          _index_within_frame{0u},
          _chunk_offset{chunk_offset} {}

   private:
    friend class boost::iterator_core_access;  // grants the boost::iterator_facade access to the private interface

    void increment() {
      ++_offset_value_it;
      ++_null_value_it;
      ++_index_within_frame;
      ++_chunk_offset;

      if (_index_within_frame >= FrameOfReferenceSegment<T>::block_size) {
        _index_within_frame = 0u;
        ++_block_minimum_it;
      }
    }

    void decrement() {
      --_offset_value_it;
      --_null_value_it;
      --_chunk_offset;

      if (_index_within_frame > 0) {
        --_index_within_frame;
      } else {
        _index_within_frame = FrameOfReferenceSegment<T>::block_size - 1;
        --_block_minimum_it;
      }
    }

    void advance(std::ptrdiff_t n) {
      // For now, the lazy approach
      PerformanceWarning("Using repeated increment/decrement for random access");
      if (n < 0) {
        for (std::ptrdiff_t i = n; i < 0; ++i) {
          decrement();
        }
      } else {
        for (std::ptrdiff_t i = 0; i < n; ++i) {
          increment();
        }
      }
    }

    bool equal(const Iterator& other) const { return _offset_value_it == other._offset_value_it; }

    std::ptrdiff_t distance_to(const Iterator& other) const { return other._offset_value_it - _offset_value_it; }

    SegmentPosition<T> dereference() const {
      const auto value = static_cast<T>(*_offset_value_it) + *_block_minimum_it;
      return SegmentPosition<T>{value, *_null_value_it, _chunk_offset};
    }

   private:
    ReferenceFrameIterator _block_minimum_it;
    OffsetValueIteratorT _offset_value_it;
    NullValueIterator _null_value_it;
    size_t _index_within_frame;
    ChunkOffset _chunk_offset;
  };

  template <typename OffsetValueDecompressorT, typename PosListIteratorType>
  class PointAccessIterator
      : public BasePointAccessSegmentIterator<PointAccessIterator<OffsetValueDecompressorT, PosListIteratorType>,
                                              SegmentPosition<T>, PosListIteratorType> {
   public:
    using ValueType = T;
    using IterableType = FrameOfReferenceSegmentIterable<T>;

    // Begin Iterator
    PointAccessIterator(const pmr_vector<T>* block_minima, const pmr_vector<bool>* null_values,
                        std::optional<OffsetValueDecompressorT> attribute_decompressor,
                        PosListIteratorType position_filter_begin, PosListIteratorType position_filter_it)
        : BasePointAccessSegmentIterator<PointAccessIterator<OffsetValueDecompressorT, PosListIteratorType>,
                                         SegmentPosition<T>, PosListIteratorType>{std::move(position_filter_begin),
                                                                                  std::move(position_filter_it)},
          _block_minima{block_minima},
          _null_values{null_values},
          _offset_value_decompressor{std::move(attribute_decompressor)} {}

    // End Iterator
    explicit PointAccessIterator(const PosListIteratorType position_filter_begin,
                                 PosListIteratorType position_filter_it)
        : PointAccessIterator{nullptr, nullptr, std::nullopt, std::move(position_filter_begin),
                              std::move(position_filter_it)} {}

   private:
    friend class boost::iterator_core_access;  // grants the boost::iterator_facade access to the private interface

    SegmentPosition<T> dereference() const {
      const auto& chunk_offsets = this->chunk_offsets();

      static constexpr auto block_size = FrameOfReferenceSegment<T>::block_size;

      const auto is_null = (*_null_values)[chunk_offsets.offset_in_referenced_chunk];
      const auto block_minimum = (*_block_minima)[chunk_offsets.offset_in_referenced_chunk / block_size];
      const auto offset_value = _offset_value_decompressor->get(chunk_offsets.offset_in_referenced_chunk);
      const auto value = static_cast<T>(offset_value) + block_minimum;

      return SegmentPosition<T>{value, is_null, chunk_offsets.offset_in_poslist};
    }

   private:
    const pmr_vector<T>* _block_minima;
    const pmr_vector<bool>* _null_values;
    mutable std::optional<OffsetValueDecompressorT> _offset_value_decompressor;
  };
};

}  // namespace opossum
