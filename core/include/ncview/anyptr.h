#ifndef SEEN_NCVIEW_ANYPTR_H
#define SEEN_NCVIEW_ANYPTR_H

#include <cstddef>

/*
 * Upstream's linked-list nodes (Stringlist, NCVar, FDBlist, Cmaplist) type
 * their next/prev fields as `void *`, relying on C's implicit void*->T*
 * conversion at every traversal site -- legal in C, ill-formed in C++, and
 * used at hundreds of call sites across core/. AnyPtr is a minimal wrapper
 * with a templated implicit conversion operator that restores exactly that
 * "converts to any pointer type" behavior, so only the four struct
 * declarations need to change, not every call site (and not a compiler-
 * specific flag: GCC's -fpermissive downgrades this to a warning, but
 * Clang -- macOS's default compiler -- has no equivalent, making it a hard
 * portability blocker there without this).
 */
class AnyPtr {
public:
	AnyPtr() = default;
	AnyPtr( void *p ) : p_( p ) {}

	template<class T> operator T*() const { return static_cast<T*>( p_ ); }

	AnyPtr &operator=( void *p ) { p_ = p; return *this; }

private:
	void *p_ = nullptr;
};

inline bool operator==( const AnyPtr &a, std::nullptr_t ) { return static_cast<void*>(a) == nullptr; }
inline bool operator==( std::nullptr_t, const AnyPtr &a ) { return static_cast<void*>(a) == nullptr; }
inline bool operator!=( const AnyPtr &a, std::nullptr_t ) { return static_cast<void*>(a) != nullptr; }
inline bool operator!=( std::nullptr_t, const AnyPtr &a ) { return static_cast<void*>(a) != nullptr; }

#endif
