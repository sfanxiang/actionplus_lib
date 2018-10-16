/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__DETAIL__STORAGE_MANAGER_HPP_
#define ACTIONPLUS_LIB__DETAIL__STORAGE_MANAGER_HPP_

#include "../action_metadata.hpp"
#include "sync_file.hpp"
#include "worker.hpp"

#include <algorithm>
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <iomanip>
#include <list>
#include <sstream>
#include <string>

namespace actionplus_lib
{
namespace detail
{

class StorageManager
{
public:
	inline StorageManager(const std::string &dir,
		std::function<void()> read_callback,
		std::function<void()> write_callback) :
	root_dir(dir), storage_dir(dir + "/storage"), tmp_dir(dir + "/tmp"),
	read_worker(read_callback),
	write_worker(write_callback)
	{}

	// List all items
	inline void list(std::function<void(const std::list<std::string> &list)>
		callback)
	{
		read_worker.add([this, callback] {
			std::list<std::string> list;
			try {
				for (auto &ent:
					boost::filesystem::directory_iterator(storage_dir))
				{
					list.push_back(ent.path().filename().generic_string());
				}
			} catch (...) {}
			list.sort([] (const std::string &a, const std::string &b) {
				return a > b;
			});

			callback(list);
		});
	}

	// Get metadata
	inline void info(const std::string &id,
		std::function<void(const ActionMetadata &metadata)> callback)
	{
		read_worker.add([this, id, callback] {
			FILE *file = std::fopen((storage_dir + "/" + id + "/info.txt")
				.c_str(), "r");
			if (!file) {
				callback(ActionMetadata());
				return;
			}

			const std::size_t buf_size = 8192;
			auto buf = std::unique_ptr<char[]>(new char[buf_size]);
			auto size = std::fread(buf.get(), 1, buf_size, file);
			if (std::ferror(file)) {
				std::fclose(file);
				callback(ActionMetadata());
				return;
			} else {
				std::fclose(file);

				try {
					auto metadata = string_to_metadata(
						std::string(buf.get(), std::min(buf_size, size)));

					try {
						callback(metadata);
					} catch (...) {}
				} catch (...) {
					callback(ActionMetadata());
				}
			}
		});
	}

	// Get video file name (including path)
	inline void video(const std::string &id,
		std::function<void(const std::string &video_file)> callback)
	{
		read_worker.add([this, id, callback] {
			try {
				for (auto &ent: boost::filesystem::directory_iterator(
						storage_dir + "/" + id)) {
					if (ent.path().stem() == "video") {
						try {
							callback(storage_dir + "/" + id + "/" +
								ent.path().filename().generic_string());
						} catch (...) {}
						return;
					}
				}
			} catch (...) {}

			callback(storage_dir + "/" + id + "/video");
		});
	}

	// Get thumbnail file name (including path)
	inline void thumbnail(const std::string &id,
		std::function<void(const std::string &thumbnail_file)> callback)
	{
		read_worker.add([this, id, callback] {
			callback(storage_dir + "/" + id + "/thumbnail.jpg");
		});
	}

	// Check if a video is analyzed and can be used to score
	inline void is_analyzed(const std::string &id,
		std::function<void(bool analyzed)> callback)
	{
		read_worker.add([this, id, callback] {
			try {
				if (boost::filesystem::exists(storage_dir + "/" + id +
						"/action.act")) {
					try {
						callback(true);
					} catch (...) {}
					return;
				}
			} catch (...) {}

			callback(false);
		});
	}

	// Import a new video from a temporary directory
	inline void import_from_temp(const std::string &dir)
	{
		write_worker.add([this, dir] {
			std::string uuid{boost::uuids::to_string(uuid_gen())};
			std::uint64_t timestamp_count = std::chrono::duration_cast<
				std::chrono::milliseconds>(
					std::chrono::system_clock::now().time_since_epoch())
						.count();
			std::ostringstream timestamp;
			timestamp << std::setfill('0') << std::setw(20) << timestamp_count;

			std::string id = timestamp.str() + "_" + uuid;
			boost::filesystem::rename(dir, storage_dir + "/" + id);
		});
	}

	// Update metadata
	inline void update(const std::string &id, const ActionMetadata &metadata)
	{
		write_worker.add([this, id, metadata] {
			std::string uuid = boost::uuids::to_string(uuid_gen());

			FILE *file = std::fopen((tmp_dir + "/" + uuid).c_str(), "w");
			if (file) {
				std::fputs(metadata_to_string(metadata).c_str(), file);
				std::fclose(file);

				sync_file(tmp_dir + "/" + uuid);

				boost::filesystem::rename(tmp_dir + "/" + uuid,
					storage_dir + "/" + id + "/info.txt");
			}
		});
	}

	// Remove an item
	inline void remove(const std::string &id)
	{
		write_worker.add([this, id] {
			std::string uuid = boost::uuids::to_string(uuid_gen());
			boost::filesystem::rename(storage_dir + "/" + id,
				root_dir + "/trash/" + uuid);
		});
	}

	inline std::list<std::string> read_tasks()
	{
		return read_worker.tasks();
	}

	inline std::list<std::string> write_tasks()
	{
		return write_worker.tasks();
	}

private:
	const std::string root_dir;
	const std::string storage_dir;
	const std::string tmp_dir;
	boost::uuids::random_generator uuid_gen{};

	Worker read_worker;
	Worker write_worker;
};

}
}

#endif
