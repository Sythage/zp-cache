#pragma once    

#include <cstddef>
#include <memory>

#include "../ZPCachePolicy.h"


// Ensure ZPCachePolicy template is defined or included before use
// If ZPCachePolicy is not a template, adjust the base class accordingly.

namespace ZPCache{

template<typename Key, typename Value> 
class ZPArcCache : public ZPCachePolicy<Key, Value>
{

public:
    explicit ZPArcCache(size_t capacity = 10, size_t transformThreshold = 2)
    : capacity_(capacity)
    , transformThreshold_(transformThreshold)
    , lruPart_(std::make_unique<ArcLruPart<Key, Value>>(capacity, transformThreshold))
    , lfuPart_(std::make_unique<ArcLfuPart<Key, Value>>(capacity, transformThreshold))
    {}

    ~ZPArcCache() override = default;

    void put(Key key, Value value) override
    {
        checkGhostCaches(key);

        // 检查 LFU 部分是否存在该键
        bool inLfu = lfuPart_->contain(key);
        // 更新 LRu 部分缓存
        lruPart_->put(key,value);
        // 如果LFU 部分存在该键， 则更新LFU部分
        if(inLfu)
        {
            lfuPart_->put(key, value);
        }
    }

    bool get(Key key, Value& vlaue)
    {
        checkGhostCaches(key);

        bool shouldTransform = false;
        if(lruPart_->get(key, vlaue, shouldTransform))
        {
            if(shouldTransform)
            {
                lfuPart_->put(key, vlaue);
            }
            return lfuPart_->get(key, vlaue);
        }
    }

    Value get(Key key) override 
    {
        Value value{};
        get(key, value);
        return value;
    }

private:
    bool checkGhostCaches(Key key)
    {
        bool inGhost = false;
        if(lruPart_->checkGhost(key))
        {
            if(lfuPart_->decreaseCapacity())
            {
                lruPart_->increaseCapacity();
            }
            inGhost = true;
        }else if(lfuPart_->checkGhost(key))
        {
            if(lruPart_->decreaseCapacity())
            {
                lfuPart_>increaseCapacity();
            }
            return inGhost;
        }
    }


private:
    size_t capacity_;
    size_t transformThreshold_;
    std::unique_ptr<ArcLruPart<Key,Value>> lruPart_;
    std::unique_ptr<ArcLfuPart<Key,Value>> lfuPart_;

};

} // namespace ZPCache