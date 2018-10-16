/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__ACTION_MANAGER_HPP_
#define ACTIONPLUS_LIB__ACTION_MANAGER_HPP_

#include "action_metadata.hpp"
#include "detail/analyze_manager.hpp"
#include "detail/export_manager.hpp"
#include "detail/import_temp_manager.hpp"
#include "detail/storage_manager.hpp"
#include "detail/worker.hpp"

#include <boost/filesystem.hpp>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <libaction/body_part.hpp>
#include <libaction/human.hpp>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace actionplus_lib
{

// action_init() must be called before using this class
class ActionManager
{
public:
	inline ActionManager(const std::string &dir,
		std::unique_ptr<std::vector<std::uint8_t>> graph,
		std::size_t graph_height,
		std::size_t graph_width,
		std::function<void()> analyze_read_callback,
		std::function<void()> analyze_write_callback,
		std::function<void()> import_callback,
		std::function<void()> export_callback,
		std::function<void()> storage_read_callback,
		std::function<void()> storage_write_callback):
	root_dir(dir),
	storage_manager(dir, storage_read_callback, storage_write_callback),
	import_temp_manager(dir, import_callback),
	export_manager(dir, export_callback),
	analyze_manager(dir, std::move(graph), graph_height, graph_width,
		analyze_read_callback, analyze_write_callback)
	{
		trash_worker.add(std::bind(&ActionManager::trash_task, this));
	}

	// List all items
	inline void list(std::function<void(const std::list<std::string> &list)>
		callback)
	{
		storage_manager.list(callback);
	}

	// Get metadata
	inline void info(const std::string &id,
		std::function<void(const ActionMetadata &metadata)> callback)
	{
		storage_manager.info(id, callback);
	}

	// Get video file name (including path)
	inline void video(const std::string &id,
		std::function<void(const std::string &video_file)> callback)
	{
		storage_manager.video(id, callback);
	}

	// Get thumbnail file name (including path)
	inline void thumbnail(const std::string &id,
		std::function<void(const std::string &thumbnail_file)> callback)
	{
		storage_manager.thumbnail(id, callback);
	}

	// Check if a video is analyzed and can be used to score
	inline void is_analyzed(const std::string &id,
		std::function<void(bool analyzed)> callback)
	{
		storage_manager.is_analyzed(id, callback);
	}

	// Get existing (finished) analysis (or nullptr)
	inline void get_analysis(const std::string &id,
		std::function<void(
			std::unique_ptr<std::list<std::unique_ptr<libaction::Human>>>
				humans)> callback)
	{
		analyze_manager.get_analysis(id, callback);
	}

	// Get the metadata of the currently running analysis (or empty id)
	inline void current_analysis_meta(
		std::function<void(
			const std::string &id, std::size_t length, std::size_t pos)>
				callback)
	{
		analyze_manager.current_analysis_meta(callback);
	}

	// Wait for a scheduled analysis to reach pos.
	// If the analysis is not scheduled, callback will be as soon as possible
	// with running == false and others empty.
	// If the analysis is running, it will be waited on and callback will
	// contain the analysis information.
	inline void wait_for_analysis(const std::string &id, std::size_t pos,
		std::function<void(bool running, std::size_t length,
			std::unique_ptr<std::list<std::unique_ptr<libaction::Human>>>
				humans)> callback)
	{
		analyze_manager.wait_for_analysis(id, pos, callback);
	}

	// Score a video against a standard video. If one of the videos is not
	// analyzed, scored will be false.
	//
	// This is the shortened version of score().
	inline void quick_score(const std::string &sample_id,
		const std::string &standard_id,
		std::function<void(bool scored, std::uint8_t mean)> callback)
	{
		analyze_manager.quick_score(sample_id, standard_id, callback);
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
		analyze_manager.score(sample_id, standard_id, missed_threshold,
			missed_max_length, callback);
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
		analyze_manager.live_score(sample_id, std::move(sample), standard_id,
			callback);
	}

	// Import a new video
	inline void import(const std::string &path, const ActionMetadata &metadata,
		bool move = false)
	{
		import_temp_manager.import_to_temp(path, metadata, move,
				[this] (const std::string &dir) {
			if (!dir.empty()) {
				storage_manager.import_from_temp(dir);
			}
		});
	}

	// Export a video
	inline void export_video(const std::string &id, const std::string &path)
	{
		export_manager.export_video(id, path);
	}

	// Update metadata
	inline void update(const std::string &id, const ActionMetadata &metadata)
	{
		storage_manager.update(id, metadata);
	}

	// Remove an item
	inline void remove(const std::string &id)
	{
		storage_manager.remove(id);
	}

	// Analyze a video. An analyze write task will be immediately created.
	// It's better to check is_analyzed() and analyze_write_tasks() before
	// adding a task here.
	inline void analyze(const std::string &id)
	{
		analyze_manager.analyze(id);
	}

	// Cancel one import task
	inline void cancel_one_import()
	{
		import_temp_manager.cancel_one();
	}

	// Cancel one export task
	inline void cancel_one_export()
	{
		export_manager.cancel_one();
	}

	// Cancel one analyze task
	inline void cancel_one_analyze()
	{
		analyze_manager.cancel_one();
	}

	// Description of analyze read tasks (strings can be empty)
	inline std::list<std::string> analyze_read_tasks()
	{
		return analyze_manager.read_tasks();
	}

	// Description of analyze write tasks (strings can be empty)
	inline std::list<std::string> analyze_write_tasks()
	{
		return analyze_manager.write_tasks();
	}

	// Description of import tasks (strings can be empty)
	inline std::list<std::string> import_tasks()
	{
		return import_temp_manager.tasks();
	}

	// Description of export tasks (strings can be empty)
	inline std::list<std::string> export_tasks()
	{
		return export_manager.tasks();
	}

	// Description of storage read tasks (strings can be empty)
	inline std::list<std::string> storage_read_tasks()
	{
		return storage_manager.read_tasks();
	}

	// Description of storage write tasks (strings can be empty)
	inline std::list<std::string> storage_write_tasks()
	{
		return storage_manager.write_tasks();
	}

private:
	std::string root_dir;

	detail::StorageManager storage_manager;
	detail::ImportTempManager import_temp_manager;
	detail::ExportManager export_manager;
	detail::AnalyzeManager analyze_manager;

	int trash_count{16};
	detail::Worker trash_worker{[] {}};

	inline void trash_task()
	{
		if (trash_count < 16) {
			trash_count++;
			std::this_thread::sleep_for(std::chrono::seconds(1));
		} else {
			trash_count = 0;
			try {
				for (auto &ent: boost::filesystem::directory_iterator(
						root_dir + "/trash")) {
					try {
						boost::filesystem::remove_all(ent.path());
					} catch (...) {}
				}
			} catch (...) {}
		}

		trash_worker.add(std::bind(&ActionManager::trash_task, this));
	}
};

}

#endif
