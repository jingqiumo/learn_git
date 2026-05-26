#ifndef THREADPOOL_H
#define THREADPOOL_H

#include<iostream>
#include<vector>
#include<mutex>
#include<atomic>
#include<thread>
#include<queue>
#include<condition_variable>
#include<functional>
#include<chrono>
#include<unordered_map>
#include<future>


#define TASK_MAX_SIZE 5
#define THREAD_MAX_SIZE 100
#define MAX_TIME 10


enum class POOLMODE
{
	FIXED,
	CACHE
};


class Thread
{
public:
	Thread(std::function<void(int)> func):
		func_{ func },
		threadId_(upThreadId_++)
	{

	}
	~Thread()
	{

	}

	void start() {
		std::thread t(func_, threadId_);
		t.detach();
	}
	int getThreadId()
	{
		return threadId_;
	}

private:
	std::function<void(int)> func_;
	inline static int upThreadId_=0;
	int threadId_;
};



//线程池实现
class ThreadPool
{
public:
	ThreadPool() :
		intThreadSize_{ 4 },
		taskSize_{ 0 },
		taskMaxSize_{ TASK_MAX_SIZE },
		poolMode_{ POOLMODE::FIXED },
		isStart_{ false },
		freeThreadSize_{ 0 },
		nowThreadSize_{ 0 },
		maxThreadSize_{ THREAD_MAX_SIZE }
	{

	}

    

	~ThreadPool() {
		isStart_ = false;

		std::unique_lock<std::mutex> lock(taskQueMtx_);

		notEmpty_.notify_all();

		over_.wait(lock, [this]()->bool {
			return threads_.size() == 0;
			});
	}
	//设置线程池模式
	void setMode(POOLMODE mode) {
		if (isStart_)
		{
			return;
		}
		poolMode_ = mode;
	}

	//设置线程数量
	void setintThreadSize(int size) {
		if (isStart_)
		{
			return;
		}
		intThreadSize_ = size;
	}
	//设置最大任务数量
	void settaskMaxSize(int size) {
		if (isStart_)
		{
			return;
		}
		taskMaxSize_ = size;
	}


	//提交任务
	template<typename Func,typename... Args>
	auto submitTask(Func&& func, Args&&... args) -> std::future<std::invoke_result_t<Func, Args...>>
	{
		using Rtype = std::invoke_result_t<Func, Args...>;
		auto task = std::make_shared<std::packaged_task<Rtype()>>(std::bind(std::forward<Func>(func), std::forward<Args>(args)...));

		std::unique_lock<std::mutex> lock(taskQueMtx_);

		if (!(notFull_.wait_for(lock, std::chrono::seconds(1), [this]()->bool {
			return taskSize_ < taskMaxSize_;
			})))
		{
			std::cerr << "目前任务过多，请稍后再试\n";
			task = std::make_shared<std::packaged_task<Rtype()>>([]()->Rtype {
				return Rtype();
				});
			(*task)();
			return task->get_future();
		}

		taskQue_.emplace([task]() {(*task)(); });
		taskSize_++;
		std::cout << "添加任务成功\n";

		notEmpty_.notify_one();


		//需要创建线程
		if (poolMode_ == POOLMODE::CACHE
			&& taskQue_.size() > freeThreadSize_
			&& nowThreadSize_ < maxThreadSize_)
		{
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadfunc, this, std::placeholders::_1));
			int id = ptr->getThreadId();
			threads_.emplace(id, std::move(ptr));
			threads_[id]->start();
			nowThreadSize_++;
			freeThreadSize_++;
			std::cout << "创建新线程\n";
		}

		return task->get_future();

	}

	//开启线程池
	void start(int intThreadSize = 4) {
		isStart_ = true;
		intThreadSize_ = intThreadSize;
		nowThreadSize_ = intThreadSize;

		for (int i = 0; i < intThreadSize_; i++)
		{
			auto ptr = std::make_unique<Thread>(std::bind(&ThreadPool::threadfunc, this, std::placeholders::_1));
			int id = ptr->getThreadId();
			threads_.emplace(id, std::move(ptr));
		}

		for (int i = 0; i < intThreadSize_; i++)
		{

			threads_[i]->start();
			freeThreadSize_++;

		}
	}
	void setmaxThreadSize(int size) {
		if (isStart_)
		{
			return;
		}
		if (poolMode_ == POOLMODE::CACHE)
		{
			maxThreadSize_ = size;
		}
	}

	ThreadPool(const ThreadPool& tp) = delete;
	ThreadPool&& operator=(const ThreadPool& tp) = delete;

private:
	void threadfunc(int threadID) {
		auto lasttime = std::chrono::high_resolution_clock().now();
		while (true)
		{
			Task t;
			{
				std::unique_lock<std::mutex> lock(taskQueMtx_);

				while (taskSize_ == 0)
				{
					if (!isStart_)
					{
						threads_.erase(threadID);
						over_.notify_all();
						std::cout << std::this_thread::get_id() << "exit\n";
						return;
					}


					if (poolMode_ == POOLMODE::CACHE)
					{

						if (std::cv_status::timeout == notEmpty_.wait_for(lock, std::chrono::seconds(1)))
						{
							auto now = std::chrono::high_resolution_clock().now();
							auto cur = std::chrono::duration_cast<std::chrono::seconds>(now - lasttime);
							if (cur.count() > MAX_TIME
								&& nowThreadSize_ > intThreadSize_)
							{
								//销毁该线程
								threads_.erase(threadID);
								nowThreadSize_--;
								freeThreadSize_--;
								std::cout << std::this_thread::get_id() << "exit\n";
								return;

							}
						}

					}
					else
					{
						notEmpty_.wait(lock);
					}
				}



				freeThreadSize_--;

				t = std::move(taskQue_.front());

				taskQue_.pop();


				taskSize_--;

				if (taskSize_ > 0)
				{
					notEmpty_.notify_one();
				}

				notFull_.notify_one();
			}

			if (t)
			{
				t();
			}
			freeThreadSize_++;
			lasttime = std::chrono::high_resolution_clock().now();
		}



	}

private:
	//std::vector < std::unique_ptr<Thread>> threads_;//线程列表
	std::unordered_map<int, std::unique_ptr<Thread>>threads_;//线程列表
	int intThreadSize_;								//初始线程数量
	std::atomic<int> freeThreadSize_;				//空闲线程数量
	std::atomic<int> nowThreadSize_;				//当前线程数量
	int maxThreadSize_;

	using Task = std::function<void()>;
	std::queue <Task> taskQue_;	//任务队列
	std::atomic<int> taskSize_;						//任务数量
	int taskMaxSize_;								//任务最大数量

	std::mutex taskQueMtx_;
	std::condition_variable notFull_;				//表示队列不满
	std::condition_variable notEmpty_;				//表示队列不空
	std::condition_variable over_;					//判断线程数组是否为空

	POOLMODE poolMode_;									//线程池模式
	std::atomic_bool isStart_;
};



#endif