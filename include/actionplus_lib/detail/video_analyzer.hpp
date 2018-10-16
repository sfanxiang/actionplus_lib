/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__DETAIL__VIDEO_ANALYZER_HPP_
#define ACTIONPLUS_LIB__DETAIL__VIDEO_ANALYZER_HPP_

#include "video_buffer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <libaction/human.hpp>
#include <libaction/motion/single/estimator.hpp>
#include <libaction/still/single/estimator.hpp>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace actionplus_lib
{
namespace detail
{

class VideoAnalyzer
{
public:
	inline VideoAnalyzer(const std::string &video,
		const std::vector<uint8_t> &graph,
		// graph must be kept throughout lifetime
		std::size_t graph_height, std::size_t graph_width)
	{
		unsigned int estimators = std::thread::hardware_concurrency();

		// Leave one out for UI. Some platforms already do this.
		if (estimators > 0 && estimators % 2 == 0)
			estimators -= 1;

		if (estimators < 4)
			estimators = 4;

		if (estimators > 128)
			estimators = 128;

		// One thread for video buffering
		estimators--;

		// TODO: validate that buffering `estimators` number of frames is optimal
		video_buffer = std::unique_ptr<VideoBuffer>(new VideoBuffer(video,
			graph_height, graph_width, estimators));

		for (unsigned int i = 0; i < estimators; i++) {
			using type = libaction::still::single::Estimator<float>;
			still_estimators.push_back(std::unique_ptr<type>(new type(
				graph.data(), graph.size(), 1, graph_height, graph_width, 3)));
		}
	}

	inline std::size_t frames() const
	{
		return video_buffer->frames();
	}

	inline std::unique_ptr<std::unordered_map<std::size_t, libaction::Human>>
	analyze(std::size_t frame)
	{
		const std::size_t fuzz_range = 7;

		std::vector<libaction::still::single::Estimator<float> *>
			still_estimator_ptrs;
		for (auto &est: still_estimators)
			still_estimator_ptrs.push_back(est.get());

		std::function<std::shared_ptr<boost::multi_array<uint8_t, 3>>
			(std::size_t pos, bool last_image_access)> cb{
				std::bind(&VideoAnalyzer::estimator_callback, this,
					std::placeholders::_1, std::placeholders::_2)};

		auto result = motion_estimator.estimate(frame, frames(), fuzz_range,
			{}, true, false, 0, 1, still_estimator_ptrs, still_estimator_ptrs,
			cb);

		return result;
	}

private:
	std::unique_ptr<VideoBuffer> video_buffer{};
	std::vector<std::unique_ptr<libaction::still::single::Estimator<float>>>
		still_estimators{};
	libaction::motion::single::Estimator motion_estimator{};

	inline std::shared_ptr<boost::multi_array<uint8_t, 3>> estimator_callback(
		std::size_t pos, bool last_image_access)
	{
		auto result = video_buffer->read(pos);
		if (last_image_access)
			video_buffer->remove(pos);
		return result;
	}
};

}
}

#endif
