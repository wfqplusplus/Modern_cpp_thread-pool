#include "Thread_Pool.h"

#include <iostream>

//****************************** Thread_Pool实现

const int TASK_MAX_THRESHHOLD = INT32_MAX;  // 任务数量上限
const int THREAD_MAX_THRESHHOLD = 1024;     // 线程数量上限
const int THREAD_MAX_IDLE_TIME = 60; // 单位：秒

Thread_Pool::Thread_Pool()
	: initThreadSize_(0)
	, threadSizeThreshHold_(THREAD_MAX_THRESHHOLD)
	, curThreadSize_(0)
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
	curThreadSize_ = initThreadSize;    // 设置当前线程池里的线程数量

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

	// 以上是默认的fixed模式

	// cached模式 任务处理比较紧急 场景：小而快的任务 需要根据任务数量和空闲线程的数量，
	// 判断是否需要创建新的线程出来
	if (poolMode_ == PoolMode::MODE_CACHED
		&& taskSize_ > idleThreadSize_             // 任务数量大于空闲线程数量
		&& curThreadSize_ < threadSizeThreshHold_) // 目前池里的线程数量小于上限阈值
	{
		std::cout << ">>> create new thread..." << std::endl;

		// 创建新的线程对象
		std::unique_ptr<Thread> ptr =
			std::make_unique<Thread>(std::bind(&Thread_Pool::thread_hander,
				this, std::placeholders::_1));
		int threadId = ptr->getId();
		threads_.emplace(threadId, std::move(ptr));
		// 启动线程
		threads_[threadId]->start();
		curThreadSize_++;    // 总线程数量+1
		idleThreadSize_++;   // 空闲数量+1
	}

	return Result(sp);
}

// 定义线程函数   线程池的所有线程从任务队列里面消费任务
void Thread_Pool::thread_hander(int threadid) // 线程函数返回，相应的线程也就结束了
{
	// 每个线程进入线程函数，该语句执行一次。
	auto lastTime = std::chrono::high_resolution_clock().now();

	for (;;)  // 所有任务必须执行完成，线程池才可以回收所有线程资源
	{
		std::shared_ptr<Task> task;
		{
			// 先获取锁
			std::unique_lock<std::mutex> lock(taskQueMtx_);

			std::cout << "tid:" << std::this_thread::get_id()
				<< "尝试获取任务..." << std::endl;

			// cached模式下，有可能已经创建了很多的线程，但是空闲时间超过60s，
			// 应该把多余的线程结束回收掉（超过initThreadSize_数量的线程要进行回收）
			// 当前时间 - 上一次线程执行的时间 > 60s

			// 锁 + 双重判断
			while (taskQue_.size() == 0)   // 此时没有任务
			{
				if (!isPoolRunning_)  // 线程池要结束，回收线程资源
				{
					threads_.erase(threadid); // std::this_thread::getid()
					std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
						<< std::endl;
					exitCond_.notify_all();
					return;  // 线程函数结束，线程结束
				}

				if (poolMode_ == PoolMode::MODE_CACHED)
				{
					// 每一秒中返回一次   怎么区分：超时返回？还是有任务待执行返回
					//wait_for 函数的主要作用是使当前线程等待，直到以下两种情况之一发生：
					// 条件变量被通知（即其他线程调用了 notify_one 或 notify_all） no_timeout 没有超时 有任务
					// 等待超时（即达到指定的时间限制） timeout    超时返回

					// 等待1s没有任何提交任务的通知
					if (std::cv_status::timeout ==
						notEmpty_.wait_for(lock, std::chrono::seconds(1)))
					{
						auto now = std::chrono::high_resolution_clock().now();
						auto dur = std::chrono::duration_cast<std::chrono::seconds>(now - lastTime);
						if (dur.count() >= THREAD_MAX_IDLE_TIME    // 超过60s没有执行任务
							&& curThreadSize_ > initThreadSize_)   // 总线程数量大于初始线程数量
						{
							// 开始回收当前线程
							// 记录线程数量的相关变量的值修改
							// 把线程对象从线程列表容器中删除   没有办法 threadFunc《=》thread对象
							// threadid => thread对象 => 删除
							threads_.erase(threadid); // std::this_thread::getid()
							curThreadSize_--;
							idleThreadSize_--;

							std::cout << "threadid:" << std::this_thread::get_id() << " exit!"
								<< std::endl;
							return;
						}
					}
				}
				else
				{
					// 等待notEmpty条件
					notEmpty_.wait(lock);  // fixed模式
				}
			}

			// 线程池要结束，回收线程资源
			//if (!isPoolRunning_)
			//{
			//	break;  // 跳出任务循环 for(;;)
			//}


			
			idleThreadSize_--; //空闲线程数量-1

			std::cout << "tid: " << std::this_thread::get_id() << "获取任务成功..." << std::endl;

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

		if (task != nullptr)
		{
			task->exec(); // 执行任务；把任务的返回值setVal方法给到Result
		}

		idleThreadSize_++;    //空闲线程数量+1
		lastTime = std::chrono::high_resolution_clock().now(); //更新线程执行完任务的时间
	}
}

Thread_Pool::~Thread_Pool()
{
	//isPoolRunning_ = false; // 状态：停止运行
	//notEmpty_.notify_all(); 

	//// 等待线程池里面所有的线程返回  有两种状态：阻塞 & 正在执行任务中 & 谁先获取锁
	//std::unique_lock<std::mutex> lock(taskQueMtx_);
	//exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; }); // 阻塞自己 释放锁

	// 两行代码互换位置，解决线程池里线程先获取锁的情况。
	isPoolRunning_ = false; // 状态：停止运行
	// 等待线程池里面所有的线程返回  有两种状态：阻塞 & 正在执行任务中 & 谁先获取锁
	std::unique_lock<std::mutex> lock(taskQueMtx_);
	notEmpty_.notify_all();
	exitCond_.wait(lock, [&]()->bool {return threads_.size() == 0; }); // 阻塞自己 释放锁
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
	Any &&result = run();
	if (result_ != nullptr)
	{
		result_->setVal(std::move(result));  // 这里发生多态调用
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

Result::~Result()
{
	task_->setResult(nullptr);
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


