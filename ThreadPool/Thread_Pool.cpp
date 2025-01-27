#include "Thread_Pool.h"

#include <iostream>

//****************************** Thread_Pool实现

const int TASK_MAX_THRESHHOLD = INT32_MAX;  // 任务数量上限
const int THREAD_MAX_THRESHHOLD = 1024;     // 线程数量上鞋
const int THREAD_MAX_IDLE_TIME = 60; // 单位：秒

Thread_Pool::Thread_Pool()
	: initThreadSize_(0)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, curThteadSize_(0)
	, idleThreadSize_(0)
	, taskSize_(0)
	, taskQueMaxThreshHold_(TASK_MAX_THRESHHOLD)
	, poolMode_(PoolMode::MODE_FIXED)
	, isPoolRunning_(false)
{ }

// 设置线程池的工作模式
void Thread_Pool::setMode(PoolMode mode)
{
	if (checkRunningState())
		return;
	poolMode_ = mode;
}

// 检查线程池是否运行
bool Thread_Pool::checkRunningState() const
{
	return isPoolRunning_;
}

// 设置task任务队列上线阈值
void Thread_Pool::setTaskQueMaxThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	taskQueMaxThreshHold_ = threshhold;
}

// 设置线程池cached模式下线程阈值
void Thread_Pool::setThreadSizeThreshHold(int threshhold)
{
	if (checkRunningState())
		return;
	if (poolMode_ == PoolMode::MODE_CACHED)
	{
		threadSizeThreshHold_ = threshhold;
	}
}

void Thread_Pool::start(int initThreadSize)
{
	isPoolRunning_ = true;              // 设置线程池的运行状态
	initThreadSize_ = initThreadSize;   // 初始化线程池的初始线程个数
	curThteadSize_ = initThreadSize;    // 设置当前线程池里的线程数量

	// 创建线程对象 但不马上执行
	for (size_t i = 0; i < initThreadSize_; i++)
	{
		std::unique_ptr<Thread> ptr = 
			std::make_unique<Thread>(std::bind(&Thread_Pool::thread_hander, 
				this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
	}

	// 开始启动所有线程
	for (size_t i = 0; i < initThreadSize_; i++)
	{
		threads_[i]->start();   // 需要去执行一个线程函数
		idleThreadSize_++;		// 纪录初始空闲线程数量 此时激活线程去执行线程函数
		                        // 但该线程不一定会获取任务 故此时还处于空闲状态
	}
}

// 给线程池提交任务    用户调用该接口，传入任务对象，生产任务
Result Thread_Pool::submitTask(std::shared_ptr<Task> sp)
{
	// 获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);

	// 线程的通信  等待任务队列有空余   wait   wait_for   wait_until
	// 用户提交任务，最长不能阻塞超过1s，否则判断提交任务失败，返回
	//while (taskQue_.size() == taskQueMaxThreshHold_)
	//{
	//	notFull_.wait(lock); // 该线程等待（挂起，非阻塞），释放锁
	//}

	// 1s之内，如果添加满足，马上返回true或false;
	if (!notFull_.wait_for(lock, std::chrono::seconds(1),
		[&]()->bool { return taskQue_.size() < taskQueMaxThreshHold_; }))
	{
		// 返回false, 表示notFull_等待1s种，条件依然没有满足
		std::cerr << "task queue is full, submit task fail." << std::endl;
		// return task->getResult();  // Task  Result   线程执行完task，task对象就被析构掉了
		return Result(sp, false);
	}

	// 如果有空余，把任务放入任务队列中
	taskQue_.emplace(sp);
	taskSize_++;

	// 因为新放了任务，任务队列肯定不空了，在notEmpty_上进行通知，赶快分配线程执行任务
	notEmpty_.notify_all();

	return Result(sp);
}

// 定义线程函数   线程池的所有线程从任务队列里面消费任务
void Thread_Pool::thread_hander(int threadid) // 线程函数返回，相应的线程也就结束了
{
	for (;;)
	{
		std::shared_ptr<Task> task;
		{
			// 先获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			// 等待notEmpty条件
			notEmpty_.wait(lock, [&]()->bool { return taskQue_.size() > 0; });

			// 从任务队列中取一个任务出来
			task = taskQue_.front();
			taskQue_.pop();
			taskSize_--;

			// 如果依然有剩余任务，继续通知其它线程执行任务
			if (taskQue_.size() > 0)
			{
				notEmpty_.notify_all();
			}

			// 取出一个任务，进行通知，通知可以继续提交生产任务
			notFull_.notify_all();

			// 当前线程负责执行这个任务
		}

		/*if (task != nullptr)
		{
			task->run();
		}*/
	}
}

Thread_Pool::~Thread_Pool()
{

}

// ****************************   Thread 实现
int Thread::generateId_ = 0;

Thread::Thread(ThreadFunc func)
	: func_(func)
	, threadId_(generateId_++)
{ }

Thread::~Thread() { }

void Thread::start()
{
	std::thread t(func_, threadId_);
	t.detach();  // 设置分离线程
}

int Thread::getId()const
{
	return threadId_;
}

// ********************* Task 实现
Task::Task() : result_(nullptr)
{ }

void Task::exec()
{
	if (result_ != nullptr)
	{
		result_->setVal(run());  // 这里发生多态调用
	}
}

void Task::setResult(Result * res)
{
	result_ = res;
}

//***************** Result 实现
Result::Result(std::shared_ptr<Task> task, bool isValid)
	: task_(task)
	, isValid_(isValid)
{ 
	task_->setResult(this);
}

void Result::setVal(Any any)    // Task 调用
{
	// 存储task的返回值
	this->any_ = std::move(any);
	sem_.post();           // 已经获取到任务的返回值，增加信号量资源
}

Any Result::get()     // 用户调用
{
	if (!isValid_)
	{
		return "";
	}
	sem_.wait();            // task任务如果没有执行完，这里会阻塞用户的线程
	return std::move(any_);
}


