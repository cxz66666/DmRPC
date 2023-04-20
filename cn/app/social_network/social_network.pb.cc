// Generated by the protocol buffer compiler.  DO NOT EDIT!
// source: social_network.proto

#include "social_network.pb.h"

#include <algorithm>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/extension_set.h>
#include <google/protobuf/wire_format_lite.h>
#include <google/protobuf/io/zero_copy_stream_impl_lite.h>
// @@protoc_insertion_point(includes)
#include <google/protobuf/port_def.inc>

PROTOBUF_PRAGMA_INIT_SEG

namespace _pb = ::PROTOBUF_NAMESPACE_ID;
namespace _pbi = _pb::internal;

namespace social_network {
PROTOBUF_CONSTEXPR Text::Text(
    ::_pbi::ConstantInitialized)
  : text_(&::_pbi::fixed_address_empty_string, ::_pbi::ConstantInitialized{}){}
struct TextDefaultTypeInternal {
  PROTOBUF_CONSTEXPR TextDefaultTypeInternal()
      : _instance(::_pbi::ConstantInitialized{}) {}
  ~TextDefaultTypeInternal() {}
  union {
    Text _instance;
  };
};
PROTOBUF_ATTRIBUTE_NO_DESTROY PROTOBUF_CONSTINIT PROTOBUF_ATTRIBUTE_INIT_PRIORITY1 TextDefaultTypeInternal _Text_default_instance_;
}  // namespace social_network
namespace social_network {

// ===================================================================

class Text::_Internal {
 public:
  using HasBits = decltype(std::declval<Text>()._has_bits_);
  static void set_has_text(HasBits* has_bits) {
    (*has_bits)[0] |= 1u;
  }
};

Text::Text(::PROTOBUF_NAMESPACE_ID::Arena* arena,
                         bool is_message_owned)
  : ::PROTOBUF_NAMESPACE_ID::MessageLite(arena, is_message_owned) {
  SharedCtor();
  // @@protoc_insertion_point(arena_constructor:social_network.Text)
}
Text::Text(const Text& from)
  : ::PROTOBUF_NAMESPACE_ID::MessageLite(),
      _has_bits_(from._has_bits_) {
  _internal_metadata_.MergeFrom<std::string>(from._internal_metadata_);
  text_.InitDefault();
  #ifdef PROTOBUF_FORCE_COPY_DEFAULT_STRING
    text_.Set("", GetArenaForAllocation());
  #endif // PROTOBUF_FORCE_COPY_DEFAULT_STRING
  if (from._internal_has_text()) {
    text_.Set(from._internal_text(), 
      GetArenaForAllocation());
  }
  // @@protoc_insertion_point(copy_constructor:social_network.Text)
}

inline void Text::SharedCtor() {
text_.InitDefault();
#ifdef PROTOBUF_FORCE_COPY_DEFAULT_STRING
  text_.Set("", GetArenaForAllocation());
#endif // PROTOBUF_FORCE_COPY_DEFAULT_STRING
}

Text::~Text() {
  // @@protoc_insertion_point(destructor:social_network.Text)
  if (auto *arena = _internal_metadata_.DeleteReturnArena<std::string>()) {
  (void)arena;
    return;
  }
  SharedDtor();
}

inline void Text::SharedDtor() {
  GOOGLE_DCHECK(GetArenaForAllocation() == nullptr);
  text_.Destroy();
}

void Text::SetCachedSize(int size) const {
  _cached_size_.Set(size);
}

void Text::Clear() {
// @@protoc_insertion_point(message_clear_start:social_network.Text)
  uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  cached_has_bits = _has_bits_[0];
  if (cached_has_bits & 0x00000001u) {
    text_.ClearNonDefaultToEmpty();
  }
  _has_bits_.Clear();
  _internal_metadata_.Clear<std::string>();
}

const char* Text::_InternalParse(const char* ptr, ::_pbi::ParseContext* ctx) {
#define CHK_(x) if (PROTOBUF_PREDICT_FALSE(!(x))) goto failure
  _Internal::HasBits has_bits{};
  while (!ctx->Done(&ptr)) {
    uint32_t tag;
    ptr = ::_pbi::ReadTag(ptr, &tag);
    switch (tag >> 3) {
      // optional string text = 1;
      case 1:
        if (PROTOBUF_PREDICT_TRUE(static_cast<uint8_t>(tag) == 10)) {
          auto str = _internal_mutable_text();
          ptr = ::_pbi::InlineGreedyStringParser(str, ptr, ctx);
          CHK_(ptr);
          CHK_(::_pbi::VerifyUTF8(str, nullptr));
        } else
          goto handle_unusual;
        continue;
      default:
        goto handle_unusual;
    }  // switch
  handle_unusual:
    if ((tag == 0) || ((tag & 7) == 4)) {
      CHK_(ptr);
      ctx->SetLastTag(tag);
      goto message_done;
    }
    ptr = UnknownFieldParse(
        tag,
        _internal_metadata_.mutable_unknown_fields<std::string>(),
        ptr, ctx);
    CHK_(ptr != nullptr);
  }  // while
message_done:
  _has_bits_.Or(has_bits);
  return ptr;
failure:
  ptr = nullptr;
  goto message_done;
#undef CHK_
}

uint8_t* Text::_InternalSerialize(
    uint8_t* target, ::PROTOBUF_NAMESPACE_ID::io::EpsCopyOutputStream* stream) const {
  // @@protoc_insertion_point(serialize_to_array_start:social_network.Text)
  uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  // optional string text = 1;
  if (_internal_has_text()) {
    ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::VerifyUtf8String(
      this->_internal_text().data(), static_cast<int>(this->_internal_text().length()),
      ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::SERIALIZE,
      "social_network.Text.text");
    target = stream->WriteStringMaybeAliased(
        1, this->_internal_text(), target);
  }

  if (PROTOBUF_PREDICT_FALSE(_internal_metadata_.have_unknown_fields())) {
    target = stream->WriteRaw(_internal_metadata_.unknown_fields<std::string>(::PROTOBUF_NAMESPACE_ID::internal::GetEmptyString).data(),
        static_cast<int>(_internal_metadata_.unknown_fields<std::string>(::PROTOBUF_NAMESPACE_ID::internal::GetEmptyString).size()), target);
  }
  // @@protoc_insertion_point(serialize_to_array_end:social_network.Text)
  return target;
}

size_t Text::ByteSizeLong() const {
// @@protoc_insertion_point(message_byte_size_start:social_network.Text)
  size_t total_size = 0;

  uint32_t cached_has_bits = 0;
  // Prevent compiler warnings about cached_has_bits being unused
  (void) cached_has_bits;

  // optional string text = 1;
  cached_has_bits = _has_bits_[0];
  if (cached_has_bits & 0x00000001u) {
    total_size += 1 +
      ::PROTOBUF_NAMESPACE_ID::internal::WireFormatLite::StringSize(
        this->_internal_text());
  }

  if (PROTOBUF_PREDICT_FALSE(_internal_metadata_.have_unknown_fields())) {
    total_size += _internal_metadata_.unknown_fields<std::string>(::PROTOBUF_NAMESPACE_ID::internal::GetEmptyString).size();
  }
  int cached_size = ::_pbi::ToCachedSize(total_size);
  SetCachedSize(cached_size);
  return total_size;
}

void Text::CheckTypeAndMergeFrom(
    const ::PROTOBUF_NAMESPACE_ID::MessageLite& from) {
  MergeFrom(*::_pbi::DownCast<const Text*>(
      &from));
}

void Text::MergeFrom(const Text& from) {
// @@protoc_insertion_point(class_specific_merge_from_start:social_network.Text)
  GOOGLE_DCHECK_NE(&from, this);
  uint32_t cached_has_bits = 0;
  (void) cached_has_bits;

  if (from._internal_has_text()) {
    _internal_set_text(from._internal_text());
  }
  _internal_metadata_.MergeFrom<std::string>(from._internal_metadata_);
}

void Text::CopyFrom(const Text& from) {
// @@protoc_insertion_point(class_specific_copy_from_start:social_network.Text)
  if (&from == this) return;
  Clear();
  MergeFrom(from);
}

bool Text::IsInitialized() const {
  return true;
}

void Text::InternalSwap(Text* other) {
  using std::swap;
  auto* lhs_arena = GetArenaForAllocation();
  auto* rhs_arena = other->GetArenaForAllocation();
  _internal_metadata_.InternalSwap(&other->_internal_metadata_);
  swap(_has_bits_[0], other->_has_bits_[0]);
  ::PROTOBUF_NAMESPACE_ID::internal::ArenaStringPtr::InternalSwap(
      &text_, lhs_arena,
      &other->text_, rhs_arena
  );
}

std::string Text::GetTypeName() const {
  return "social_network.Text";
}


// @@protoc_insertion_point(namespace_scope)
}  // namespace social_network
PROTOBUF_NAMESPACE_OPEN
template<> PROTOBUF_NOINLINE ::social_network::Text*
Arena::CreateMaybeMessage< ::social_network::Text >(Arena* arena) {
  return Arena::CreateMessageInternal< ::social_network::Text >(arena);
}
PROTOBUF_NAMESPACE_CLOSE

// @@protoc_insertion_point(global_scope)
#include <google/protobuf/port_undef.inc>
