/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__DETAIL__ANALYZE_HELPER_HPP_
#define ACTIONPLUS_LIB__DETAIL__ANALYZE_HELPER_HPP_

#include "sync_file.hpp"
#include "video_analyzer.hpp"
#include "worker.hpp"

#include <algorithm>
#include <atomic>
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <libaction/body_part.hpp>
#include <libaction/human.hpp>
#include <libaction/motion/multi/deserialize.hpp>
#include <libaction/motion/multi/serialize.hpp>
#include <libaction/motion/single/missed_moves.hpp>
#include <libaction/still/single/score.hpp>
#include <list>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace actionplus_lib
{
namespace detail
{

class AnalyzeHelper
{
public:
	inline AnalyzeHelper(const std::string &dir,
		std::unique_ptr<std::vector<std::uint8_t>> graph,
		std::size_t graph_height, std::size_t graph_width,
		std::function<void()> read_callback,
		std::function<void()> write_callback) :
	storage_dir(dir + "/storage"), tmp_dir(dir + "/tmp"),
	graph_data(std::move(graph)), height(graph_height), width(graph_width),
	write_worker(write_callback),
	read_worker(read_callback)
	{}

	// Analyze a video. An analyze write task will be immediately created.
	// It's better to check is_analyzed() and write_tasks() before adding a
	// task here.
	inline void analyze(const std::string &id,
		std::function<void(std::size_t length,
			std::unique_ptr<std::list<std::unique_ptr<libaction::Human>>>
				humans)> progress,
		std::function<void()> done)
	{
		write_worker.add([this, id, progress, done] {
			try {
				canceled = false;

				if (boost::filesystem::exists(storage_dir + "/" + id +
						"/action.act")) {
					// Already analyzed
					try {
						done();
					} catch (...) {}
					return;
				}

				std::string video = get_video_file(id);
				std::string output = storage_dir + "/" + id + "/action.act";

				VideoAnalyzer analyzer(video, *graph_data, height, width);

				std::list<std::unordered_map<std::size_t, libaction::Human>>
					action;

				for (std::size_t i = 0; i < analyzer.frames(); i++) {
					if (canceled)
						throw std::runtime_error("");

					auto res = analyzer.analyze(i);
					action.push_back(std::move(*res));

					try {
						progress(analyzer.frames(),
							simplify_for_result(action));
					} catch (...) {}
				}

				std::string tmp_file = tmp_dir + "/" + boost::uuids::to_string(uuid_gen());

				auto serialized = libaction::motion::multi::serialize::serialize(action);
				write_file(tmp_file, *serialized);

				sync_file(tmp_file);

				boost::filesystem::rename(tmp_file, output);
				try {
					boost::filesystem::remove(tmp_file);
				} catch (...) {}

				try {
					done();
				} catch (...) {}
			} catch (...) {
				try {
					done();
				} catch (...) {}
			}

			canceled = false;
		}, id);
	}

	// Get existing (finished) analysis (or nullptr)
	inline void get_analysis(const std::string &id,
		std::function<void(
			std::unique_ptr<std::list<std::unique_ptr<libaction::Human>>>
				humans)> callback)
	{
		read_worker.add([this, id, callback] {
			try {
				auto read = read_file(storage_dir + "/" + id +
					"/action.act");
				auto action = libaction::motion::multi::deserialize::
					deserialize(*read);

				try {
					callback(simplify_for_result(*action));
				} catch (...) {}
				return;
			} catch (...) {}

			callback(nullptr);
		});
	}

	// Score a video against a standard video. If one of the videos is not
	// analyzed, scored will be false.
	//
	// This is the shortened version of score().
	inline void quick_score(const std::string &sample_id,
		const std::string &standard_id,
		std::function<void(bool scored, std::uint8_t mean)> callback)
	{
		read_worker.add([this, sample_id, standard_id, callback] {
			try {
				auto sample_read = read_file(storage_dir + "/" + sample_id +
					"/action.act");
				auto sample = libaction::motion::multi::deserialize::
					deserialize(*sample_read);
				auto standard_read = read_file(storage_dir + "/" + standard_id +
					"/action.act");
				auto standard = libaction::motion::multi::deserialize::
					deserialize(*standard_read);

				do_score(*sample, *standard, false, 0, 0,
					std::bind(callback, std::placeholders::_1,
						std::placeholders::_4));
			} catch (...) {
				callback(false, 0);
			}
		}, sample_id);
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
		read_worker.add([this, sample_id, standard_id, missed_threshold,
				missed_max_length, callback] {
			try {
				auto sample_read = read_file(storage_dir + "/" + sample_id +
					"/action.act");
				auto sample = libaction::motion::multi::deserialize::
					deserialize(*sample_read);
				auto standard_read = read_file(storage_dir + "/" + standard_id +
					"/action.act");
				auto standard = libaction::motion::multi::deserialize::
					deserialize(*standard_read);

				do_score(*sample, *standard, true, missed_threshold,
					missed_max_length, callback);
			} catch (...) {
				callback(false, nullptr, nullptr, 0, nullptr);
			}
		}, sample_id);
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
		std::shared_ptr<std::list<std::unique_ptr<libaction::Human>>> sample2
		(std::move(sample));

		read_worker.add([this, sample2, standard_id, callback] {
			try {
				if (!sample2)
					throw std::runtime_error("");

				std::list<std::unordered_map<std::size_t, libaction::Human>>
					sample_map;
				for (auto &item: *sample2) {
					if (item) {
						sample_map.push_back({std::make_pair(0, std::move(*item))});
					} else {
						sample_map.push_back(std::unordered_map<std::size_t, libaction::Human>());
					}
				}

				auto standard_read = read_file(storage_dir + "/" + standard_id +
					"/action.act");
				auto standard = libaction::motion::multi::deserialize::
					deserialize(*standard_read);

				do_score(sample_map, *standard, false, 0, 0,
					std::bind(callback,
						std::placeholders::_1, std::placeholders::_2,
						std::placeholders::_3, std::placeholders::_4));
			} catch (...) {
				callback(false, nullptr, nullptr, 0);
			}
		}, sample_id);
	}

	inline void cancel_one()
	{
		canceled = true;
	}

	inline std::list<std::string> read_tasks()
	{
		return read_worker.tasks();
	}

	inline std::list<std::string> write_tasks()
	{
		return write_worker.tasks();
	}

	inline void add_read_task(std::function<void()> task)
	{
		read_worker.add(task);
	}

private:
	std::string storage_dir;
	std::string tmp_dir;

	std::unique_ptr<std::vector<std::uint8_t>> graph_data;
	std::size_t height;
	std::size_t width;

	std::atomic_bool canceled{false};

	boost::uuids::random_generator uuid_gen{};

	Worker write_worker;
	Worker read_worker;

	static inline std::unique_ptr<std::vector<std::uint8_t>> read_file(
		const std::string &file)
	{
		constexpr std::size_t max = 0x20000000;

		FILE *f = std::fopen(file.c_str(), "rb");
		if (!f)
			throw std::runtime_error("failed to open file");

		auto data = std::unique_ptr<std::vector<std::uint8_t>>(
			new std::vector<std::uint8_t>());

		int c;
		while (data->size() < max && (c = std::fgetc(f)) != EOF) {
			if (std::ferror(f)) {
				std::fclose(f);
				throw std::runtime_error("failed to read file");
			}
			data->push_back(static_cast<std::uint8_t>(c));
		}

		std::fclose(f);

		return data;
	}

	static inline void write_file(const std::string &file,
		const std::vector<std::uint8_t> &data)
	{
		FILE *f = std::fopen(file.c_str(), "wb");
		if (!f)
			throw std::runtime_error("failed to open file");

		if (std::fwrite(data.data(), 1, data.size(), f) < data.size()) {
			std::fclose(f);
			try {
				boost::filesystem::remove(file);
			} catch (...) {}
			throw std::runtime_error("failed to write file");
		}

		std::fclose(f);
	}


	inline std::string get_video_file(const std::string &id)
	{
		for (auto &ent: boost::filesystem::directory_iterator(
				storage_dir + "/" + id)) {
			if (ent.path().stem() == "video") {
				return storage_dir + "/" + id + "/video" +
					ent.path().extension().generic_string();
			}
		}

		throw std::runtime_error("video file not found");
	}

	template<typename Action>
	std::unique_ptr<std::list<std::unique_ptr<libaction::Human>>>
		simplify_for_result(const Action &action)
	{
		auto humans = std::unique_ptr<std::list<std::unique_ptr<libaction::Human>>>(
			new std::list<std::unique_ptr<libaction::Human>>());
		for (const auto &human_map: action) {
			auto it = human_map.find(0);
			if (it != human_map.end()) {
				humans->push_back(std::unique_ptr<libaction::Human>(
					new libaction::Human(it->second)));
			} else {
				humans->push_back(std::unique_ptr<libaction::Human>());
			}
		}

		return humans;
	}

	template<typename Sample, typename Standard>
	void do_score(const Sample &sample, const Standard &standard,
		bool calculate_missed_moves,
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
			)> callback) noexcept
	{
		// callback is always called
		try {
			auto scores = std::unique_ptr<std::list<std::map<std::pair<libaction::BodyPart::PartIndex,
				libaction::BodyPart::PartIndex>, std::uint8_t>>>(
					new std::list<std::map<std::pair<libaction::BodyPart::PartIndex,
						libaction::BodyPart::PartIndex>, std::uint8_t>>());

			auto part_means = std::unique_ptr<std::map<std::pair<libaction::BodyPart::PartIndex,
				libaction::BodyPart::PartIndex>, std::uint8_t>>(
					new std::map<std::pair<libaction::BodyPart::PartIndex,
						libaction::BodyPart::PartIndex>, std::uint8_t>());

			std::map<std::pair<libaction::BodyPart::PartIndex,
				libaction::BodyPart::PartIndex>, std::uint64_t> part_sums;
			std::map<std::pair<libaction::BodyPart::PartIndex,
				libaction::BodyPart::PartIndex>, std::uint32_t> part_counts;
			std::uint64_t frame_sum = 0;
			std::uint32_t frame_count = 0;

			auto sample_it = sample.begin();
			auto standard_it = standard.begin();

			for (std::size_t i = 0; i < sample.size() && i < standard.size();
				i++, sample_it++, standard_it++)
			{
				auto &sample_frame = *sample_it;
				auto &standard_frame = *standard_it;

				auto human1_it = sample_frame.find(0);
				if (human1_it == sample_frame.end()) {
					scores->push_back({});
					continue;
				}

				auto human2_it = standard_frame.find(0);
				if (human2_it == standard_frame.end()) {
					scores->push_back({});
					continue;
				}

				auto &human1 = human1_it->second;
				auto &human2 = human2_it->second;

				auto frame_scores = libaction::still::single::score::score(
					human1, human2);

				std::uint32_t sum = 0;
				for (auto &score: *frame_scores) {
					sum += static_cast<std::uint32_t>(score.second);
					part_sums[score.first] += score.second;
					part_counts[score.first]++;
				}
				if (!frame_scores->empty()) {
					frame_sum += sum / frame_scores->size();
					frame_count++;
				}

				scores->push_back(std::move(*frame_scores));
			}

			for (auto &score: part_sums) {
				std::uint64_t current = score.second / part_counts[score.first];
				(*part_means)[score.first] = current;
			}

			std::uint8_t mean = (frame_count != 0 ? frame_sum / frame_count : 0);

			std::unique_ptr<std::list<std::map<std::pair<
				libaction::BodyPart::PartIndex, libaction::BodyPart::PartIndex>,
					std::pair<std::uint32_t, std::uint8_t>>>> missed_moves;

			if (calculate_missed_moves) {
				missed_moves = libaction::motion::single::missed_moves::
					missed_moves(*scores, missed_threshold, missed_max_length);
			}

			try {
				callback(true, std::move(scores), std::move(part_means),
					mean, std::move(missed_moves));
			} catch (...) {}
		} catch (...) {
			try {
				callback(false, nullptr, nullptr, 0, nullptr);
			} catch (...) {}
		}
	}
};

}
}

#endif
