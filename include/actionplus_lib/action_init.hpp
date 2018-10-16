/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__ACTION_INIT_HPP_
#define ACTIONPLUS_LIB__ACTION_INIT_HPP_

#include <boost/filesystem.hpp>
#include <functional>
#include <thread>

namespace actionplus_lib
{
namespace action_init
{

inline void action_init(const std::string &dir, std::function<void()> callback)
{
	std::thread t([dir, callback] {
		try {
			try {
				boost::filesystem::remove_all(dir + "/tmp");
			} catch (boost::filesystem::filesystem_error) {}
			try {
				boost::filesystem::remove_all(dir + "/trash");
			} catch (boost::filesystem::filesystem_error) {}

			boost::filesystem::create_directories(dir + "/tmp");
			boost::filesystem::create_directories(dir + "/trash");
			boost::filesystem::create_directories(dir + "/storage");

			// TODO: maybe need to clean up storage dir for extra files/dirs
		} catch (...) {}

		try {
			callback();
		} catch (...) {}
	});
	t.detach();
}

}
}

#endif
