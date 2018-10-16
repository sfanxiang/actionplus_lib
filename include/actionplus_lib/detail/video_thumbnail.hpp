/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__DETAIL__VIDEO_THUMBNAIL_HPP_
#define ACTIONPLUS_LIB__DETAIL__VIDEO_THUMBNAIL_HPP_

#include "video_reader.hpp"

#include <boost/gil.hpp>
#include <boost/gil/extension/io/jpeg.hpp>
#include <string>

namespace actionplus_lib
{
namespace detail
{
namespace video_thumbnail
{

void generate(const std::string &video_file, const std::string &jpeg_file)
{
	std::shared_ptr<boost::multi_array<uint8_t, 3>> image;

	{
		VideoReader reader(video_file, 0, 0);

		try {
			image = reader.read(0);
			if (image->shape()[0] < 1 || image->shape()[1] < 1 ||
					image->shape()[2] != 3)
				throw std::runtime_error("wrong image dimensions");
		} catch (...) {
			image = std::shared_ptr<boost::multi_array<uint8_t, 3>>(
				new boost::multi_array<uint8_t, 3>(
					boost::extents[1][1][3]));
		}
	}

	// TODO: normalize size

	auto image_view = boost::gil::interleaved_view(
		image->shape()[1], image->shape()[0],
		static_cast<const boost::gil::rgb8_pixel_t *>(static_cast<const void *>(
			image->data())),
		image->shape()[1] * image->shape()[2]);

	boost::gil::write_view(jpeg_file, image_view,
		boost::gil::image_write_info<boost::gil::jpeg_tag>(95));
}

}
}
}

#endif
