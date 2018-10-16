/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__DETAIL__EXPORT_MANAGER_HPP_
#define ACTIONPLUS_LIB__DETAIL__EXPORT_MANAGER_HPP_

#include "worker.hpp"

#include <atomic>
#include <boost/filesystem.hpp>
#include <cstddef>
#include <functional>
#include <list>
#include <string>

namespace actionplus_lib
{
namespace detail
{

class ExportManager
{
public:
	inline ExportManager(const std::string &dir,
		std::function<void()> callback) :
	storage_dir(dir + "/storage"), worker(callback)
	{}

	// Export a video
	inline void export_video(const std::string &id, const std::string &path)
	{
		worker.add([this, id, path] {
			try {
				canceled = false;

				std::string ext{};

				for (auto &ent: boost::filesystem::directory_iterator(
						storage_dir + "/" + id)) {
					if (ent.path().stem() == "video") {
						ext = ent.path().extension().generic_string();
					}
				}

				FILE *in = std::fopen((storage_dir + "/" + id + "/video" + ext)
					.c_str(), "rb");
				if (!in) {
					throw std::runtime_error("");
				}
				FILE *out = std::fopen(path.c_str(), "wb");
				if (!out) {
					std::fclose(in);
					throw std::runtime_error("");
				}

				while (true) {
					if (canceled) {
						std::fclose(in);
						std::fclose(out);
						throw std::runtime_error("");
					}

					const std::size_t bufsize = 1024 * 64;
					std::unique_ptr<unsigned char[]> buffer(
						new unsigned char[bufsize]);
					auto size = std::fread(buffer.get(), 1, bufsize, in);
					if (std::ferror(in)) {
						std::fclose(in);
						std::fclose(out);
						throw std::runtime_error("");
					}
					if (size == 0)
						break;

					auto wsize = std::fwrite(buffer.get(), 1, size, out);
					if (wsize < size) {
						std::fclose(in);
						std::fclose(out);
						throw std::runtime_error("");
					}
				}

				std::fclose(in);
				std::fclose(out);
			} catch (...) {
				try {
					boost::filesystem::remove(path);
				} catch (...) {}
			}

			canceled = false;
		}, id);
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
	std::string storage_dir;
	std::atomic_bool canceled{false};

	Worker worker;
};

}
}

#endif
