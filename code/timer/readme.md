### TimerNode

## 成员
    # id        int
    编号

    # expires   TimeStamp
    时间辍变量，用于记录过期时间

    # cb        TimeoutCallBack
    过期回调函数，过期时调用的函数，
    一般为删除

    # operator<(const TimerNode& t)
    重载比较符号，用于比较过期时间点，
    是否超时



### 实现堆计时器

    //本定时器基于小根堆实现，可以提供
    o(log n)插入，o(1)查找，o(1)删除，
    相比于普通的队列性能更高。

## 类内部私有成员和函数

    # del_(size_t t)
    删除下标为i的heap_成员，若i非最后一个
    下标，则要对堆heap_进行调整：先交换i和最后
    下标的节点，再尝试下沉，若无法下沉则上浮。
    最后删除heap_末尾以及删除ref_对应成员

    # shiftup_(size_t i)
    上浮i下标的heap_成员，通过堆的性质，
    找父节点并判断是否小于自己，不断交换

    # shiftdown(size_t idx,size_t n)
    限制最大下沉下标<=n为前提，下沉i下标的heap_成员，
    通过堆性质，查看左右孩子节点中更大的并尝试与之交换，
    返回是否能下沉成功

    # SwapNode_(size_t i,size_t j)
    交换heap_中下标i和j的成员，并随之改变ref_

    # heap_     vector<TimerNode>
    基于vector动态数组实现堆，类似于STL中的heap

    # ref_      unordered_map<int,size_t>
    哈希表，通过id找到数组中的索引i


## 类内部公有函数

    # HeapTimer()
    初始化heap_容量为64

    # ~HeapTimer()
    调用clear()

    # adjust(int id,int newExpires)
    调整id对应时间节点的expires为newExpires，
    因为更新会增大，所以下沉即可

    # add(int id,int timeOut,const TimeoutCallBack& cb)
    往堆中添加节点。若无id，则添加该节点到末尾并上浮。
    否则更新时间节点，尝试上浮或者下沉。

    # doWork(int id)
    调用id的节点中的回调函数cb()，并删除该节点

    # clear()
    清空heap_和ref_

    # tick()
    将heap_中的过期时间节点全部删除

    # pop()
    删除最小节点，即expires最小的，可能超时的

    # GetNextTick()
    见tick()，在查看未过期的节点剩余存活时间