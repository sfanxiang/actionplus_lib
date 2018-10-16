/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__DETAIL__IMPORT_TEMP_MANAGER_HPP_
#define ACTIONPLUS_LIB__DETAIL__IMPORT_TEMP_MANAGER_HPP_

#include "../action_metadata.hpp"
#include "sync_file.hpp"
#include "video_thumbnail.hpp"
#include "worker.hpp"

#include <atomic>
#include <boost/filesystem.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <cstddef>
#include <fstream>
#include <functional>
#include <list>
#include <stdexcept>
#include <string>

namespace actionplus_lib
{
namespace detail
{

class ImportTempManager
{
public:
	inline ImportTempManager(const std::string &dir,
		std::function<void()> callback) :
	tmp_dir(dir + "/tmp"), worker(callback)
	{}

	// Import a new video to a temporary directory
	inline void import_to_temp(const std::string &path,
		const ActionMetadata &metadata,
		bool move,
		std::function<void(const std::string &dir)> callback)
	{
		worker.add([this, path, metadata, move, callback] {
			try {
				canceled = false;

				std::string uuid{boost::uuids::to_string(uuid_gen())};
				try {
					boost::filesystem::create_directory(tmp_dir + "/" + uuid);
				} catch (...) {}

				try {
					// currently, even if move == true, the file is still
					// copied and deleted

					boost::filesystem::path fs_path = path;
					FILE *in = std::fopen(path.c_str(), "rb");
					if (!in) {
						throw std::runtime_error("");
					}
					FILE *out = std::fopen((tmp_dir + "/" + uuid + "/video" +
						fs_path.extension().generic_string()).c_str(), "wb");
					if (!out) {
						std::fclose(in);
						throw std::runtime_error("");
					}

					try {
						while (true) {
							if (canceled)
								throw std::runtime_error("");

							const std::size_t bufsize = 1024 * 64;
							std::unique_ptr<unsigned char[]> buffer(
								new unsigned char[bufsize]);
							auto size = std::fread(buffer.get(), 1, bufsize, in);
							if (std::ferror(in))
								throw std::runtime_error("");
							if (size == 0)
								break;

							auto wsize = std::fwrite(buffer.get(), 1, size, out);
							if (wsize < size)
								throw std::runtime_error("");
						}

						std::fclose(in);
						in = nullptr;
						std::fclose(out);
						out = nullptr;

						sync_file(tmp_dir + "/" + uuid + "/video" +
							fs_path.extension().generic_string());

						if (move) {
							try {
								boost::filesystem::remove(path);
							} catch (...) {}
						}

						video_thumbnail::generate(tmp_dir + "/" + uuid +
							"/video" + fs_path.extension().generic_string(),
							tmp_dir + "/" + uuid + "/thumbnail.jpg");

						out = std::fopen((tmp_dir + "/" + uuid + "/info.txt")
							.c_str(), "w");
						if (!out)
							throw std::runtime_error("");
						if (std::fputs(metadata_to_string(metadata).c_str(), out) < 0)
							throw std::runtime_error("");
						std::fclose(out);
						out = nullptr;

						sync_file(tmp_dir + "/" + uuid + "/info.txt");
					} catch (...) {
						if (in)
							std::fclose(in);
						if (out)
							std::fclose(out);
						throw;
					}
				} catch (...) {
					try {
						boost::filesystem::remove_all(tmp_dir + "/" + uuid);
					} catch (...) {}
				}

				canceled = false;
				try {
					callback(tmp_dir + "/" + uuid);
				} catch (...) {}
			} catch (...) {
				canceled = false;
				try {
					callback("");
				} catch (...) {}
			}
		}, path);
	}

	inline void cancel_one()
	{
		canceled = true;
	}

	inline std::list<std::string> tasks()
	{
		return worker.tasks();
	}

private:
	std::string tmp_dir;
	boost::uuids::random_generator uuid_gen{};
	std::atomic_bool canceled{false};

	Worker worker;
};

}
}

#endif
