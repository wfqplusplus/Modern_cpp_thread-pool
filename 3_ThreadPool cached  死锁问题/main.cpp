#include <iostream>
#include <chrono>

using namespace std;

#include "Thread_Pool.h"

/*
有些场景，是希望能够获取线程执行任务得返回值得
举例：
1 + 。。。 + 30000的和
thread1  1 + ... + 10000
thread2  10001 + ... + 20000
.....

main thread：给每一个线程分配计算的区间，并等待他们算完返回结果，合并最终的结果即可
*/

using uLong = unsigned long long;

class MyTask : public Task
{
public:
    MyTask(int begin, int end)
        : begin_(begin)
        , end_(end)
    {}
    // 问题一：怎么设计run函数的返回值，可以表示任意的类型
    // Java Python   Object 是所有其它类类型的基类
    // C++17 Any类型
    Any run()  // run方法最终就在线程池分配的线程中去做执行了!
    {
        std::cout << "tid:" << std::this_thread::get_id()
            << "begin!" << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(3));
        uLong sum = 0;
        for (uLong i = begin_; i <= end_; i++)
            sum += i;
        std::cout << "tid:" << std::this_thread::get_id()
            << "end!" << std::endl;

        return sum;
    }

private:
    int begin_;
    int end_;
};

int main()
{
    {
        Thread_Pool pool;
        pool.start();
        Result res = pool.submitTask(std::make_shared<MyTask>(100000001, 200000000));
        uLong sum1 = res.get().cast_<uLong>();
        cout << sum1 << endl;
    }
    cout << "main over!" << endl;
    getchar();
    return 0;
}