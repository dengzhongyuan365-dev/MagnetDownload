#pragma once
#include <iostream>
#include <thread>
#include <mutex>
#include <iomanip>
#include <chrono>
#include <asio.hpp>
class MultiThreadDemo {
private:

	asio::io_context& io_context_;
	asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
	std::atomic<int> counter_ { 0 };
	std::atomic<int> completed_tasks_{0};
	mutable std::mutex print_mutex_;
	const int total_tasks_ {10};

public:

	MultiThreadDemo(asio::io_context& io_context)
		: io_context_(io_context)
		, work_guard_(asio::make_work_guard(io_context))
	{}

	void start_work() {
		safe_print("开始投递任务到io_context...");


		for (int i = 0; i<total_tasks_;++i) {
			asio::post(io_context_, [this, i] {
				this->worker_task(i);
				});
		}

		safe_print("所有任务已投递，等待执行...");


		auto timer = std::make_shared<asio::steady_timer>(io_context_, std::chrono::seconds(5));

		timer->async_wait([this, timer](const asio::error_code& ec) {
			if (!ec) {
				safe_print("定时器到期，停止工作...");
				work_guard_.reset();
			}
			});

	}



private:
	void worker_task(int task_id) {
		auto thread_id = std::this_thread::get_id();

		safe_print("任务" + std::to_string(task_id) + "开始执行，线程ID： " +
			std::to_string(std::hash<std::thread::id>{}(thread_id) % 10000));

		std::this_thread::sleep_for(std::chrono::milliseconds(100 + (task_id % 5) * 100));

		int current_value = counter_.fetch_add(1);

		int completed = completed_tasks_.fetch_add(1);

		safe_print("任务 " + std::to_string(task_id) + " 完成！当前计数器: " +
			std::to_string(current_value + 1) + ", 已完成: " + std::to_string(completed + 1));

		if (completed + 1 >= total_tasks_) {
			safe_print("🎉 所有任务完成！正在停止...");
			asio::post(io_context_, [this]() {
				work_guard_.reset();
				});
		}



	}

	void safe_print(const std::string& message) const {
		std::lock_guard<std::mutex> lock(print_mutex_);
		auto now = std::chrono::steady_clock::now();
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());

		auto thread_id = std::this_thread::get_id();


		std::cout << "[ " << std::setfill('0') << std::setw(6) << (ms.count() % 100000) << " ms]"
			<< "[线程: " << std::setw(4) << (std::hash<std::thread::id>{}(thread_id) % 10000)
			<< "]"
			<< message << std::endl;
	}

};
