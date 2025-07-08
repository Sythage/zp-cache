#pragma once


#include "ZPArcCacheNode.h"
#include <cstddef>
#include <memory>
#include <mutex>
#include <unordered_map>


namespace ZPCache {

template<typename Key, typename Value>
class ArcLruPart
{
public:
    using NodeType = ArcNode<Key, Value>;
    using NodePtr = std::shared_ptr<NodeType>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    explicit ArcLruPart(size_t capacity, size_t transformThreshold)
    : capacity_(capacity)
    , ghostCapacity_(capacity)
    , transformThreshold_(transformThreshold)
    {
        initializeLists();
    }

    bool put(Key key, Value value)
    {
        if(capacity_ == 0) return false;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if(it!=mainCache_.end())
        {
            return updateExistingNode(it->second, value);
        }
        return addNewNode(key, value);
    }

    bool get(Key key, Value& value, bool& shouldTransform)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = mainCache_.find(key);
        if(it!= mainCache_.end())
        {
            shouldTransform = updateNodeAccess(it->second);
            value = it->second->getValue();
            return true;
        }
        return false;
    }

    bool checkGhost(Key key){
        auto it = ghostCache_.find(key);
        if(it!=ghostCache_.end())
        {
            reMoveFromGhost(it->second);
            ghostCache_.erase(it);
            return true;
        }
        return false;
    }

    void increasCapacity() { ++capacity_; }

    bool decreaseCapacity()
    {
        if(capacity_ <= 0) return false;
        if(mainCache_.size() == capacity_) {
            evicitLeastRecent();
        }
        --capacity_;
        return true;
    }



private:
    void initializeLists()
    {
        mainHead_ = std::make_shared<NodeType>();
        mainTail_ = std::make_shared<NodeType>();
        mainHead_->next_ = mainTail_;
        mainTail_->prev_ = mainHead_;

        ghostHead_ = std::make_shared<NodeType>();
        ghostTail_ = std::make_shared<NodeType>();
        ghostHead_->next_ = ghostTail_;
        ghostTail_->prev_= ghostHead_;
    }

    bool updateExistingNode(NodePtr node, const Value& value)
    {
        node->setValue(value);
        moveToFront(node);
        return true;
    }

    bool addNewNode(const Key& key, const Value& value)
    {
        if(mainCache_.size() >= capacity_)
        {
            evicitLeastRecent(); // 驱逐最近最少访问
        }

        NodePtr newNode = std::make_shared<NodeType>(key,value);
        mainCache_[key] = newNode;
        addToFront(newNode);
        return true;
    }

    bool updateNodeAccess(NodePtr node)
    {
        moveToFront(node);
        node->incrementAccessCount();
        return node->getAccessCount() >= transformThreshold_;
    }

    void moveToFront(NodePtr node)
    {
        // remove from this site
        if(!node->prev_.expired() && node->next_) {
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            node->next_->prev_ = node->prev_;
            node->next_ = nullptr; // clear ptr, 防止悬垂引用
        }

        // add to head
        addToFront(node);
    }

    void addToFront(NodePtr node)
    {
        node->next_ = mainHead_->next_;
        node->prev_ = mainHead_;
        mainHead_->next_->prev_ = node;
        mainHead_->next_ = node;
    }

    void evicitLeastRecent()
    {
        NodePtr leastRecent = mainTail_->prev_.lock();
        if(!leastRecent || leastRecent == mainHead_)
            return;

        // delete from main list
        removeFromMain(leastRecent);

        // add to ghost(👻) cache
        if(ghostCache_.size() >= ghostCapacity_)
        {
            removeOldestGhost();
        }
        addToGhost(leastRecent);

        // 从主缓存映射中移除
        mainCache_.erase(leastRecent->getKey());
    }

    void removeFromMain(NodePtr node)
    {
        if(!node->prev_.expired() && node->next_)
        {
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            prev->next_->prev_ = node->prev_;
            node->next_ = nullptr;
        }
    }

    void reMoveFromGhost(NodePtr node){
        if(!node->prev_.expired() && node->next_)
        {
            auto prev = node->prev_.lock();
            prev->next_ = node->next_;
            prev->next_->prev_ = node->prev_;
            node->next_ = nullptr;
        }
    }

    void addToGhost(NodePtr node)
    {
        // reset access count of node
        node->accessCount_ = 1;

        // add to head of ghost cache
        node->next_ = ghostHead_->next_;
        node->prev_ = ghostHead_;
        ghostHead_->next_->prev_ = node;
        ghostHead_->next_ = node;

        // add to cache map of ghost
        ghostCache_[node->getKey()]=node;
    }

    void removeOldestGhost()
    {
        // 使用lock
        NodePtr oldestGhost = ghostTail_->prev_.lock();
        if(!oldestGhost || oldestGhost == ghostHead_)
            return;

        removeFromMain(oldestGhost);
        ghostCache_.erase(oldestGhost->getKey());
    }

private:
    size_t capacity_;
    size_t ghostCapacity_;
    size_t transformThreshold_; // 转换门槛阈值
    std::mutex mutex_;
    
    NodeMap mainCache_; // key-> arcNode
    NodeMap ghostCache_;

    // mian list
    NodePtr mainHead_;
    NodePtr mainTail_;
    // list out 
    NodePtr ghostHead_;
    NodePtr ghostTail_;
};

} // namespace ZPCache