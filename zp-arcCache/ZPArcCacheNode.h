#pragma once

#include <cstddef>
#include <memory>

namespace ZPCache {

template<typename Key, typename Value>
class ArcNode
{
private:
    Key key_;
    Value value_;
    size_t accessCount_;
    std::weak_ptr<ArcNode> prev_;
    std::shared_ptr<ArcNode> next_;

public:
    ArcNode() : accessCount_(1), next_(nullptr){}

    ArcNode(Key key, Value value)
    : key_(key)
    , value_(value)
    , accessCount_(1)
    ,next_(nullptr)
    {}

    // Getters
    Key getKey() const { return key_;}
    Value getValue() const { return value_;}
    size_t getAccessCount() const { return accessCount_; }

    // Setters
    void setValue(const Value& value) { value_ = value; }
    void incrementAccessCount() {++accessCount_; }

    template<typename K, typename  V> friend class ArcLruPart;
    template<typename  K, typename  V> friend class ArcLfuPart;
};

} // namespace ZPCache