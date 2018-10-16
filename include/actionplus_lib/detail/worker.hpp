/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__DETAIL__WORKER_HPP_
#define ACTIONPLUS_LIB__DETAIL__WORKER_HPP_

#include <condition_variable>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <list>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace actionplus_lib
{
namespace detail
{

class Worker
{
public:
	inline Worker(std::function<void()> callback) : update_callback(callback)
	{
		thread = std::thread(std::bind(&Worker::work, this));
	}

	inline ~Worker()
	{
		stop();
	}

	inline void add(std::function<void()> task, std::string desc = "")
	{
		{
			std::lock_guard<std::mutex> lk(mtx);
			task_list.push_back(std::make_pair([task] {
				task();
				return true;
			}, desc));
		}
		cv.notify_all();
	}

	inline std::list<std::string> tasks()
	{
		std::list<std::string> list;

		std::lock_guard<std::mutex> lk(mtx);
		for (auto &task: task_list)
			list.push_back(task.second);

		return list;
	}

private:
	std::thread thread{};
	std::mutex mtx{};
	std::condition_variable cv{};
	std::function<void()> update_callback;
	std::list<std::pair<std::function<bool()>, std::string>> task_list;

	inline void work()
	{
		std::unique_lock<std::mutex> lk(mtx);

		while (true) {
			try {
				cv.wait(lk, [this] { return !task_list.empty(); });

				{
					lk.unlock();
					try {
						update_callback();
					} catch (...) {}
					lk.lock();
				}

				while (!task_list.empty()) {
					try {
						auto task = task_list.front();

						lk.unlock();

						bool done = false;
						try {
							if (!task.first())
								done = true;
						} catch (...) {}

						lk.lock();
						task_list.pop_front();
						lk.unlock();

						try {
							update_callback();
						} catch (...) {}

						if (done)
							return;

						lk.lock();
					} catch (...) {}
				}
			} catch (...) {}
		}
	}

	inline void stop()
	{
		if (thread.joinable()) {
			{
				std::lock_guard<std::mutex> lk(mtx);
				task_list.push_back(std::make_pair([] {
					return false;
				}, ""));
			}
			cv.notify_all();

			// TODO: For now, if this happens, we just terminate the process to
			//       avoid waiting for the long-running threads
			std::_Exit(0);
			// thread.join();
		}
	}
};

}
}

#endif
