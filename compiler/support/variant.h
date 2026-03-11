#ifndef DAO_SUPPORT_VARIANT_H
#define DAO_SUPPORT_VARIANT_H

namespace dao {

// ---------------------------------------------------------------------------
// Visitor helper — standard C++17 overloaded pattern for std::visit.
// ---------------------------------------------------------------------------

template <class... Ts>
struct overloaded : Ts... { using Ts::operator()...; };
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

} // namespace dao

#endif // DAO_SUPPORT_VARIANT_H
