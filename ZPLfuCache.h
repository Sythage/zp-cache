#pragma once

#include "ZPCachePolicy.h"
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
namespace ZPCache {
// Rfu cache

template<typename Key, typename Value> class ZPLfuCache;

template<typename Key, typename Value>
class FreqList
{
private:
    struct Node
    {
        int freq;   // 访问频次
        Key key; 
        Value value;   
        std::weak_ptr<Node> pre; // 上一节点改为weak_ptr 打破循环引用
        std::shared_ptr<Node> next;
        
        Node()
        : freq(1), next(nullptr){}
        Node(Key key, Value value)
        : freq(1), key(key), value(value), next(nullptr) {}
    };

    using NodePtr = std::shared_ptr<Node>;
    int freq_; // 访问频率
    NodePtr head_;  // 假头节点
    NodePtr tail_;  // 假尾节点

public:
    explicit FreqList(int n) //explicit 意为明确的，
    : freq_(n)
    {
        head_ = std::make_shared<Node>();
        tail_ = std::make_shared<Node>();
        head_->next = tail_;
        tail_->pre = head_;
    }

    bool isEmpty() const{
        return head_->next == tail_;
    }

    void addNode(NodePtr node){
        if(!node || !head_ || !tail_)
            return;
    
        node->pre = tail_->pre;
        node->next = tail_;
        tail_->pre.lock()->next = node; // 使用lock()获取shaderd_ptr，因为weak_ptr 不能直接访问对象需要用lock升级为shared_ptr
                                        // 如果对象存在返回有效的shared_ptr ，如果不存在 返回空的shared_ptr
        tail_->pre = node;
    }

    void removeNode(NodePtr node){
        if(!node || !head_ || !tail_)
            return;
        if(node->pre.expired() || !node->next){
            return;
        }

        auto pre = node->pre.lock(); // 使用lock()获取shaderd_ptr
        pre->next = node->next;
        node->next->pre = pre;
        node->next = nullptr;       // 确保显示置空next 指针，彻底断开节点与链表的连接
    }

    NodePtr getFirstNode() const {return head_->next;}

    friend class ZPLfuCache<Key, Value>;

};

template<typename Key, typename  Value>
class ZPLfuCache : public ZPCachePolicy<Key, Value> 
{
public:
    using Node = typename FreqList<Key, Value>::Node;
    using NodePtr = std::shared_ptr<Node>;
    using NodeMap = std::unordered_map<Key, NodePtr>;

    ZPLfuCache(int capacity, int maxAverageNum =10)
    : capacity_(capacity), minFreq_(INT8_MAX), maxAverageNum_(maxAverageNum),
    curAverageNum_(0), curTotalNum_(0)
    {}

    ~ZPLfuCache() override = default;

    void put(Key key, Value value) override{
        if(capacity_ == 0)
            return;

        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if(it != nodeMap_.end())
        {
            // reset value
            it->second->value = value;
            // 找到了直接调整就好了，不用再去get中找一遍
            getInternal(it->second, value);
            return;
        }

        putInternal(key, value);
    }

    bool get(Key key, Value& value) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = nodeMap_.find(key);
        if(it != nodeMap_.end())
        {
            getInternal(it->second, value);
            return true;
        }
        return false;
    }

    Value get(Key key) override
    {
        Value value;
        get(key, value);
        return value;
    }

    // 清空缓存
    void purge()
    {
        nodeMap_.clear();
        freqToFreqList_.clear();
    }
   


private:
    void putInternal(Key key, Value value); // 添加缓存
    void getInternal(NodePtr node, Value& value); // 获取缓存

    void kickOut(); // 移除缓存中的过期数据

    void removeFromFreqList(NodePtr node); // 从频率列表中移除节点
    void addToFreqList(NodePtr node); // 添加到频率列表

    void addFreqNum(); // 增加平均访问等频率
    void decreaseFreqNum(int num); // 减少平均访问等频率
    void handleOverMaxAverageNum(); // 处理当前平均访问频率超过上限的情况
    void updateMinFreq();

private:
    int capacity_;
    int minFreq_;       // 最小访问频次
    int maxAverageNum_; // 最大平均访问频次
    int curAverageNum_; // 当前平均访问频次
    int curTotalNum_;   // 当前访问所有缓存次数总数
    std::mutex mutex_;  // 互斥锁
    NodeMap nodeMap_;   // key 到缓存节点的映射
    std::unordered_map<int , FreqList<Key, Value>*> freqToFreqList_; // 访问频次到该频次链表的映射
};

template<typename Key, typename Value>
void ZPLfuCache<Key, Value>::getInternal(NodePtr node, Value& value)
{
    // 找到之后需要将其从低访问频次的链表中删除，并且添加到+1的访问频次链表中，
    // 访问频次+1, 然后把value值返回
    value = node->value;
    // 从原有访问频次的链表中删除节点
    removeFromFreqList(node);
    node->freq++;
    addToFreqList(node);
    // 如果当前node的访问频次如果等于minFreq+1，并且其前驱链表为空，则说明
    // freqToFreqList_[node->freq - 1]链表因node的迁移已经空了，需要更新最小访问频次
    if (node->freq - 1 == minFreq_ && freqToFreqList_[node->freq - 1]->isEmpty())
        minFreq_++;

    // 总访问频次和当前平均访问频次都随之增加
    addFreqNum();
}

template<typename Key, typename Value>
void ZPLfuCache<Key, Value>::putInternal(Key key, Value value)
{
    // 如果不在缓存中，则需要判断缓存是否已满
    if(nodeMap_.size() == capacity_)
    {
        // 缓存已满， 删除最不常访问的节点， 更新当前平均访问频次和总访问频次
        kickOut();
    }

    // 创建新结点，将新结点添加进入，更新最小访问频次
    NodePtr node = std::make_shared<Node>(key, value);
    nodeMap_[key] = node;
    addToFreqList(node);
    addFreqNum();
    minFreq_ = std::min(minFreq_, 1);
}

template<typename Key, typename Value>
void ZPLfuCache<Key, Value>::kickOut()
{
    NodePtr node = freqToFreqList_[minFreq_]->getFirstNode();
    removeFromFreqList(node);
    nodeMap_.erase(node->key);
    decreaseFreqNum(node->freq);
}

template<typename Key, typename Value>
void ZPLfuCache<Key, Value>::removeFromFreqList(NodePtr node)
{
    // 检查结点是否为空
    if (!node) 
        return;
    
    auto freq = node->freq;
    freqToFreqList_[freq]->removeNode(node);
}

template<typename Key, typename Value>
void ZPLfuCache<Key, Value>::addToFreqList(NodePtr node)
{
    // 检查结点是否为空
    if (!node) 
        return;

    // 添加进入相应的频次链表前需要判断该频次链表是否存在
    auto freq = node->freq;
    if (freqToFreqList_.find(node->freq) == freqToFreqList_.end())
    {
        // 不存在则创建
        freqToFreqList_[node->freq] = new FreqList<Key, Value>(node->freq);
    }

    freqToFreqList_[freq]->addNode(node);
}

template<typename Key, typename Value>
void ZPLfuCache<Key, Value>::addFreqNum()
{
    curTotalNum_++;
    if (nodeMap_.empty())
        curAverageNum_ = 0;
    else
        curAverageNum_ = curTotalNum_ / nodeMap_.size();

    if (curAverageNum_ > maxAverageNum_)
    {
       handleOverMaxAverageNum();
    }
}

template<typename Key, typename Value>
void ZPLfuCache<Key, Value>::decreaseFreqNum(int num)
{
    // 减少平均访问频次和总访问频次
    curTotalNum_ -= num;
    if (nodeMap_.empty())
        curAverageNum_ = 0;
    else
        curAverageNum_ = curTotalNum_ / nodeMap_.size();
}

template<typename Key, typename Value>
void ZPLfuCache<Key, Value>::handleOverMaxAverageNum()
{
    if (nodeMap_.empty())
        return;

    // 当前平均访问频次已经超过了最大平均访问频次，所有结点的访问频次- (maxAverageNum_ / 2)
    for (auto it = nodeMap_.begin(); it != nodeMap_.end(); ++it)
    {
        // 检查结点是否为空
        if (!it->second)
            continue;

        NodePtr node = it->second;

        // 先从当前频率列表中移除
        removeFromFreqList(node);

        // 减少频率
        node->freq -= maxAverageNum_ / 2;
        if (node->freq < 1) node->freq = 1;

        // 添加到新的频率列表
        addToFreqList(node);
    }

    // 更新最小频率
    updateMinFreq();
}

template<typename Key, typename Value>
void ZPLfuCache<Key, Value>::updateMinFreq() 
{
    minFreq_ = INT8_MAX;
    for (const auto& pair : freqToFreqList_) 
    {
        if (pair.second && !pair.second->isEmpty()) 
        {
            minFreq_ = std::min(minFreq_, pair.first);
        }
    }
    if (minFreq_ == INT8_MAX) 
        minFreq_ = 1;
}

}