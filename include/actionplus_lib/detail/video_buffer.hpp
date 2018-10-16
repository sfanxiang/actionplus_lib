/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__DETAIL__VIDEO_BUFFER_HPP_
#define ACTIONPLUS_LIB__DETAIL__VIDEO_BUFFER_HPP_

#include "video_reader.hpp"

#include <boost/multi_array.hpp>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>

namespace actionplus_lib
{
namespace detail
{

class VideoBuffer
{
public:
	inline VideoBuffer(const std::string &video,
		std::size_t scale_height, std::size_t scale_width,
		std::size_t buffer_frames) :
	buffer(buffer_frames),
	reader(video, scale_height, scale_width)
	{
		thread = std::thread(std::bind(&VideoBuffer::runner, this));
	}

	inline ~VideoBuffer()
	{
		{
			std::lock_guard<std::mutex> lk(data_mtx);
			stop = true;
		}
		cv.notify_all();
		thread.join();
	}

	inline std::size_t frames() const
	{
		return reader.frames();
	}

	inline std::shared_ptr<boost::multi_array<uint8_t, 3>> read(std::size_t index)
	{
		if (index >= reader.frames())
			throw std::runtime_error("index >= reader.frames()");

		std::unique_lock<std::mutex> lk(data_mtx);

		if (index >= target_next) {
			target_next = index + 1;
			cv.notify_all();
		}

		cv.wait(lk, [this, index] {return next > index;});

		auto it = data.find(index);
		if (it == data.end())
			throw std::runtime_error("data not found");
		return it->second;
	}

	inline void remove(std::size_t index)
	{
		std::lock_guard<std::mutex> lk(data_mtx);
		data.erase(index);
	}

private:
	const std::size_t buffer;

	std::mutex data_mtx{};
	std::condition_variable cv{};

	bool stop{false};
	std::size_t target_next{0};
	std::size_t next{0};
	std::unordered_map<std::size_t,
		std::shared_ptr<boost::multi_array<uint8_t, 3>>> data{};

	VideoReader reader;

	std::thread thread{};

	inline void runner()
	{
		std::unique_lock<std::mutex> lk(data_mtx);

		while (true) {
			cv.wait(lk, [this] {
				if (stop)
					return true;
				if (reader.next_index() >= reader.frames())
					return false;
				if (target_next + buffer > reader.next_index())
					return true;
				return false;
			});

			if (stop)
				return;

			auto prev = reader.next_index();
			lk.unlock();
			try {
				reader.read(prev);
			} catch (...) {}
			lk.lock();

			for (std::size_t i = prev; i < reader.next_index(); i++) {
				try {
					data[i] = reader.read(i);
					reader.remove(i);
				} catch (...) {}
			}
			next = reader.next_index();

			cv.notify_all();
		}
	}
};

}
}

#endif
