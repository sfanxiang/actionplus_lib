/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__DETAIL__ANALYZE_MANAGER_HPP_
#define ACTIONPLUS_LIB__DETAIL__ANALYZE_MANAGER_HPP_

#include "analyze_helper.hpp"
#include "worker.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <libaction/body_part.hpp>
#include <libaction/human.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace actionplus_lib
{
namespace detail
{

class AnalyzeManager
{
public:
	inline AnalyzeManager(const std::string &dir,
		std::unique_ptr<std::vector<std::uint8_t>> graph,
		std::size_t graph_height, std::size_t graph_width,
		std::function<void()> read_callback,
		std::function<void()> write_callback) :
	write_update_callback(write_callback),
	analyze_helper(dir, std::move(graph), graph_height, graph_width,
		std::move(read_callback), write_callback)
	{}

	// Analyze a video. An analyze write task will be immediately created.
	// It's better to check is_analyzed() and write_tasks() before adding a
	// task here.
	inline void analyze(const std::string &id)
	{
		analyze_helper.analyze(id, [this, id]
			(std::size_t length,
				std::unique_ptr<std::list<std::unique_ptr<libaction::Human>>>
					humans) {
				if (!humans || humans->size() == 0)
					return;

				std::unique_lock<std::mutex> lk(record_mtx);

				analyze_record.id = id;
				analyze_record.length = length;
				analyze_record.humans = std::move(humans);

				lk.unlock();

				write_update_callback();
			},
			[this] {
				std::lock_guard<std::mutex> lk(record_mtx);
				analyze_record = AnalyzeRecord();
			}
		);
	}

	// Get the metadata of the currently running analysis
	inline void current_analysis_meta(
		std::function<void(
			const std::string &id, std::size_t length, std::size_t pos)>
				callback)
	{
		try {
			std::unique_lock<std::mutex> lk(record_mtx);

			auto id = analyze_record.id;
			auto length = analyze_record.length;
			auto pos = 0;
			if (analyze_record.humans && analyze_record.humans->size() > 0)
				pos = analyze_record.humans->size() - 1;

			lk.unlock();

			analyze_helper.add_read_task([callback, id, length, pos] {
				callback(id, length, pos);
			});
		} catch (...) {
			try {
				analyze_helper.add_read_task([callback] {
					callback("", 0, 0);
				});
			} catch (...) {}
		}
	}

	// Get the currently running analysis
	inline void current_analysis(std::function<void(
		const std::string &id, std::size_t length,
		std::unique_ptr<std::list<std::unique_ptr<libaction::Human>>> humans)>
			callback)
	{
		try {
			std::unique_lock<std::mutex> lk(record_mtx);

			auto id = analyze_record.id;
			auto length = analyze_record.length;

			std::shared_ptr<std::list<std::unique_ptr<libaction::Human>>>
				humans_copy;
			if (analyze_record.humans) {
				humans_copy = std::shared_ptr<std::list<std::unique_ptr<
					libaction::Human>>>(
						new std::list<std::unique_ptr<libaction::Human>>());

				for (auto &human: *analyze_record.humans) {
					std::unique_ptr<libaction::Human> copy;
					if (human) {
						copy.reset(new libaction::Human(*human));
					}
					humans_copy->push_back(std::move(copy));
				}
			}

			lk.unlock();

			analyze_helper.add_read_task([callback, id, length, humans_copy] {
				try {
					std::unique_ptr<std::list<std::unique_ptr<libaction::Human>>>
						humans_unique;
					if (humans_copy) {
						humans_unique = std::unique_ptr<std::list<std::unique_ptr<
							libaction::Human>>>(
								new std::list<std::unique_ptr<libaction::Human>>(
									std::move(*humans_copy)));
					}

					try {
						callback(id, length, std::move(humans_unique));
					} catch (...) {}
				} catch (...) {
					callback("", 0, nullptr);
				}
			});
		} catch (...) {
			try {
				analyze_helper.add_read_task([callback] {
					callback("", 0, nullptr);
				});
			} catch (...) {}
		}
	}

private:
	struct AnalyzeRecord
	{
		std::string id{};
		std::size_t length{};
		std::unique_ptr<std::list<std::unique_ptr<libaction::Human>>> humans{};
	};

	std::function<void()> write_update_callback;

	std::mutex record_mtx{};
	AnalyzeRecord analyze_record{};

	AnalyzeHelper analyze_helper;

// Pass-through:
public:
	// Get existing (finished) analysis (or nullptr)
	inline void get_analysis(const std::string &id,
		std::function<void(
			std::unique_ptr<std::list<std::unique_ptr<libaction::Human>>>
				humans)> callback)
	{
		analyze_helper.get_analysis(id, std::move(callback));
	}

	// Score a video against a standard video. If one of the videos is not
	// analyzed, scored will be false.
	//
	// This is the shortened version of score().
	inline void quick_score(const std::string &sample_id,
		const std::string &standard_id,
		std::function<void(bool scored, std::uint8_t mean)> callback)
	{
		analyze_helper.quick_score(sample_id, standard_id, std::move(callback));
	}

	// Score a video against a standard video. If one of the videos is not
	// analyzed, scored will be false.
	inline void score(const std::string &sample_id,
		const std::string &standard_id,
		std::uint8_t missed_threshold,
		std::uint32_t missed_max_length,
		std::function<void(
			bool scored,
			std::unique_ptr<std::list<std::map<std::pair<libaction::BodyPart::PartIndex,
				libaction::BodyPart::PartIndex>, std::uint8_t>>> scores,
			std::unique_ptr<std::map<std::pair<libaction::BodyPart::PartIndex,
				libaction::BodyPart::PartIndex>, std::uint8_t>> part_means,
			std::uint8_t mean,
			std::unique_ptr<std::list<std::map<std::pair<
				libaction::BodyPart::PartIndex, libaction::BodyPart::PartIndex>,
					std::pair<std::uint32_t, std::uint8_t>>>> missed_moves
			)> callback)
	{
		analyze_helper.score(sample_id, standard_id, missed_threshold,
			missed_max_length, std::move(callback));
	}

	// Score a video during analysis. If the standard video is not analyzed,
	// scored will be false.
	inline void live_score(
		const std::string &sample_id,
		std::unique_ptr<std::list<std::unique_ptr<libaction::Human>>> sample,
		const std::string &standard_id,
		std::function<void(
			bool scored,
			std::unique_ptr<std::list<std::map<std::pair<libaction::BodyPart::PartIndex,
				libaction::BodyPart::PartIndex>, std::uint8_t>>> scores,
			std::unique_ptr<std::map<std::pair<libaction::BodyPart::PartIndex,
				libaction::BodyPart::PartIndex>, std::uint8_t>> part_means,
			std::uint8_t mean
			)> callback)
	{
		analyze_helper.live_score(sample_id, std::move(sample), standard_id,
			std::move(callback));
	}

	inline void cancel_one()
	{
		analyze_helper.cancel_one();
	}

	inline std::list<std::string> read_tasks()
	{
		return analyze_helper.read_tasks();
	}

	inline std::list<std::string> write_tasks()
	{
		return analyze_helper.write_tasks();
	}
};

}
}

#endif
