


#include "../ZPCachePolicy.h"
#include <cstddef>

// Ensure ZPCachePolicy template is defined or included before use
// If ZPCachePolicy is not a template, adjust the base class accordingly.

namespace ZPCache{

template<typename Key, typename Value> 
class ZPArcCache : public ZPCachePolicy<Key, Value>
{

private:
    size_t capacity_;
    size_t transformThreshold_;
    std::unique_ptr<ArcLruPart<Key,Value>> lruPart_;
    std::unique_ptr<ArcLfuPart<Key,Value>> lfuPart_;

}



} // namespace ZPCache