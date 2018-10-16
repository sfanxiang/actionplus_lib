/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__ACTION_METADATA_HPP_
#define ACTIONPLUS_LIB__ACTION_METADATA_HPP_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>

namespace actionplus_lib
{

struct ActionMetadata
{
	std::string title{};
	std::string score_against{};
};

std::string normalize_string_length(const std::string &str)
{
	return str.substr(0, 8192);
}

std::string normalize_string_line(const std::string &str)
{
	std::string result = normalize_string_length(str);
	for (auto &ch: result) {
		if (ch == '\n' || ch == '\0')
			ch = ' ';
	}
	return result;
}

std::string metadata_to_string(const ActionMetadata &metadata)
{
	std::string title = normalize_string_line(metadata.title);
	std::string score_against = normalize_string_line(metadata.score_against);

	std::ostringstream s;
	s << title << "\n" << score_against << "\n";
	return s.str();
}

ActionMetadata string_to_metadata(const std::string &string)
{
	ActionMetadata metadata{};

	std::istringstream s(string.substr(0, 8192));

	std::getline(s, metadata.title);
	normalize_string_line(metadata.title);

	std::getline(s, metadata.score_against);
	normalize_string_line(metadata.score_against);

	return metadata;
}

}

#endif
