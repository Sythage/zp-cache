#pragma once

namespace ZPCache {
// Rfu cache

template<typename Key, typename Value> class ZPLfuCache;

template<typename Key, typename Value>
class FreqList
{
private:
    struct Node
    {
        int freq; // 访问频次
        Key key;
    }
}


}