/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__DETAIL__SYNC_FILE_HPP_
#define ACTIONPLUS_LIB__DETAIL__SYNC_FILE_HPP_

#include <fcntl.h>
#include <string>
#include <unistd.h>

#ifdef _WIN32
#include <io.h>
#endif

namespace actionplus_lib
{
namespace detail
{

inline void sync_file(const std::string &file)
{
	int fd = open(file.c_str(), O_WRONLY);
	if (fd != -1) {
#ifndef _WIN32
		fsync(fd);
#else
		_commit(fd);
#endif
		close(fd);
	}
}

}
}

#endif
