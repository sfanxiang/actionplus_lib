/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * This Source Code Form is "Incompatible With Secondary Licenses", as
 * defined by the Mozilla Public License, v. 2.0. */

#ifndef ACTIONPLUS_LIB__DETAIL__VIDEO_READER_HPP_
#define ACTIONPLUS_LIB__DETAIL__VIDEO_READER_HPP_

#include <algorithm>
#include <boost/multi_array.hpp>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libswscale/swscale.h>
}

namespace actionplus_lib
{
namespace detail
{

class VideoReader
{
public:
	const std::size_t read_frame_rate = 10;

	// Set scale_height and scale_width to 0 to disable scaling
	inline VideoReader(const std::string &video,
		std::size_t scale_height, std::size_t scale_width) :
	height(scale_height), width(scale_width)
	{
		format_ctx = avformat_alloc_context();

		if (avformat_open_input(&format_ctx, video.c_str(), 0, nullptr) != 0) {
			throw std::runtime_error("avformat_open_input failed");
		}

		if (avformat_find_stream_info(format_ctx, nullptr) < 0) {
			avformat_close_input(&format_ctx);
			throw std::runtime_error("avformat_find_stream_info failed");
		}

		auto frame_interval_ms = 1000 / read_frame_rate;
		uint64_t duration = format_ctx->duration;
		uint64_t duration_ms = duration * 1000 / static_cast<uint64_t>(AV_TIME_BASE);
		tot_frames = duration_ms / frame_interval_ms;

		tot_frames = std::min(tot_frames, static_cast<std::size_t>(30 * 60 * read_frame_rate));

		for (unsigned int i = 0; i < format_ctx->nb_streams; i++)
		{
			if (format_ctx->streams[i]->codecpar->codec_type ==
					AVMEDIA_TYPE_VIDEO) {
				stream_idx = i;
				break;
			}
		}

		if (stream_idx == -1) {
			// not found
			avformat_close_input(&format_ctx);
			throw std::runtime_error("no video stream was found");
		}

		if (format_ctx->streams[stream_idx]->metadata) {
			auto rotate_info = av_dict_get(format_ctx->streams[stream_idx]->metadata,
				"rotate", nullptr, 0);
			if (rotate_info && rotate_info->value) {
				if (std::string(rotate_info->value) == "270")
					rotation = 270;
				else if (std::string(rotate_info->value) == "90")
					rotation = 90;
				else if (std::string(rotate_info->value) == "180")
					rotation = 180;
				// Otherwise, use the default value
			}
		}

		AVCodecParameters *codec_param = format_ctx->streams[stream_idx]->codecpar;

		codec = avcodec_find_decoder(codec_param->codec_id);
		if (!codec) {
			avformat_close_input(&format_ctx);
			throw std::runtime_error("unsupported codec");
		}

		codec_ctx = avcodec_alloc_context3(codec);
		if (!codec_ctx) {
			avformat_close_input(&format_ctx);
			throw std::runtime_error("avcodec_alloc_context3 failed");
		}

		if (avcodec_parameters_to_context(codec_ctx, codec_param) < 0) {
			avcodec_free_context(&codec_ctx);
			avformat_close_input(&format_ctx);
			throw std::runtime_error("avcodec_parameters_to_context failed");
		}

		if (avcodec_open2(codec_ctx, codec, nullptr) < 0) {
			avcodec_free_context(&codec_ctx);
			avformat_close_input(&format_ctx);
			throw std::runtime_error("avcodec_open2 failed");
		}

		packet = av_packet_alloc();
		if (!packet) {
			avcodec_free_context(&codec_ctx);
			avformat_close_input(&format_ctx);
			throw std::runtime_error("av_packet_alloc failed");
		}

		frame = av_frame_alloc();
		if (!frame) {
			av_packet_free(&packet);
			avcodec_free_context(&codec_ctx);
			avformat_close_input(&format_ctx);
			throw std::runtime_error("av_frame_alloc failed");
		}
	}

	inline ~VideoReader()
	{
		if (sws_ctx) {
			sws_freeContext(sws_ctx);
		}
		av_frame_free(&frame);
		av_packet_free(&packet);
		avcodec_free_context(&codec_ctx);
		avformat_close_input(&format_ctx);
	}

	// thread-safe
	inline std::size_t frames() const
	{
		return tot_frames;
	}

	// thread-unsafe
	inline std::size_t next_index()
	{
		return next;
	}

	// thread-unsafe
	// next_index may go beyond index + 1
	inline std::shared_ptr<boost::multi_array<uint8_t, 3>> read(std::size_t index)
	{
		if (index >= tot_frames)
			throw std::runtime_error("index >= tot_frames");

		try {
			read_until(index);

			if (next <= index)
				throw std::runtime_error("");
		} catch (...) {
			for (; next < tot_frames; next++) {
				data[next] = std::shared_ptr<boost::multi_array<uint8_t, 3>>(
					new boost::multi_array<uint8_t, 3>(
						boost::extents[1][1][3]));
			}
		}

		auto it = data.find(index);
		if (it == data.end())
			throw std::runtime_error("data not found");
		return it->second;
	}

	// thread-unsafe
	inline void remove(std::size_t index)
	{
		data.erase(index);
	}

private:
	const std::size_t height;
	const std::size_t width;

	long long stream_idx{-1};
	AVFormatContext *format_ctx{};
	AVCodec *codec{};
	AVCodecContext *codec_ctx{};
	AVPacket *packet{};
	AVFrame *frame{};
	struct SwsContext *sws_ctx{};

	unsigned rotation{0};
	int64_t pts_start{};

	std::size_t tot_frames{0};
	std::size_t next{0};

	std::unordered_map<std::size_t,
		std::shared_ptr<boost::multi_array<uint8_t, 3>>> data{};

	inline void read_until(std::size_t index)
	{
		if (tot_frames == 0)
			return;
		if (index >= tot_frames)
			index = tot_frames - 1;

		while (next <= index) {
			if (av_read_frame(format_ctx, packet) != 0) {
				// EOF
				return;
			}

			if (packet->stream_index == stream_idx) {
				if (avcodec_send_packet(codec_ctx, packet) < 0) {
					av_packet_unref(packet);
					continue;
				}

				while (true) {
					int ret = avcodec_receive_frame(codec_ctx, frame);
					if (ret < 0)
						break;

					if (next == 0)
						pts_start = frame->pts;

					if (time_base_to_ms(frame->pts - pts_start) >=
							next * 1000 / read_frame_rate) {
						av_frame_apply_cropping(frame, 0);

						if (frame->height == 0 || frame->width == 0) {
							data[next++] = std::shared_ptr<boost::multi_array<uint8_t, 3>>(
								new boost::multi_array<uint8_t, 3>(
									boost::extents[1][1][3]));
							continue;
						}

						std::size_t dst_height = height;
						std::size_t dst_width = width;
						if (dst_height == 0 || dst_width == 0) {
							dst_height = frame->height;
							dst_width = frame->width;
						}
						dst_height = std::min(dst_height, static_cast<std::size_t>(
							std::numeric_limits<int>::max() - 1));
						dst_width = std::min(dst_width, static_cast<std::size_t>(
							std::numeric_limits<int>::max() - 1));

						// convert to RGB
						sws_ctx = sws_getCachedContext(
							sws_ctx, frame->width, frame->height,
							static_cast<AVPixelFormat>(frame->format),
							dst_width, dst_height, AV_PIX_FMT_RGB24,
							0, nullptr, nullptr, nullptr);
						if (!sws_ctx)
							break;

						auto image = std::shared_ptr<boost::multi_array<uint8_t, 3>>(
							new boost::multi_array<uint8_t, 3>(
								boost::extents[dst_height][dst_width][3]));

						uint8_t * const dst[1]{image->data()};
						const int stride[1]{3 * static_cast<int>(dst_width)};

						sws_scale(sws_ctx, frame->data, frame->linesize, 0,
							frame->height, dst, stride);

						image = rotate(image, rotation);

						data[next++] = std::move(image);
					}
				}
			}

			av_packet_unref(packet);
		}
	}

	inline int64_t time_base_to_ms(int64_t time)
	{
		auto &time_base = format_ctx->streams[stream_idx]->time_base;
		if (time_base.num == 0)
			return time;
		return time * 1000 * time_base.num / time_base.den;
	}

	inline std::shared_ptr<boost::multi_array<uint8_t, 3>>
		rotate(const std::shared_ptr<boost::multi_array<uint8_t, 3>> &image,
			unsigned degrees)
	{
		if (degrees == 270) {
			return rotate_270(*image);
		} else if (degrees == 90) {
			return rotate_90(*image);
		} else if (degrees == 180) {
			return rotate_180(*image);
		} else if (degrees == 0) {
			return image;
		} else {
			return image;
		}
	}

	inline std::shared_ptr<boost::multi_array<uint8_t, 3>>
		rotate_90(const boost::multi_array<uint8_t, 3> &image)
	{
		auto rotated = std::shared_ptr<boost::multi_array<uint8_t, 3>>(
			new boost::multi_array<uint8_t, 3>(boost::extents[image.shape()[1]]
				[image.shape()[0]][image.shape()[2]]));

		for (std::size_t i = 0; i < image.shape()[1]; i++) {
			for (std::size_t j = 0; j < image.shape()[0]; j++) {
				(*rotated)[i][j] = image[image.shape()[0] - j - 1][i];
			}
		}

		return rotated;
	}

	inline std::shared_ptr<boost::multi_array<uint8_t, 3>>
		rotate_180(const boost::multi_array<uint8_t, 3> &image)
	{
		auto rotated = std::shared_ptr<boost::multi_array<uint8_t, 3>>(
			new boost::multi_array<uint8_t, 3>(boost::extents[image.shape()[0]]
				[image.shape()[1]][image.shape()[2]]));

		for (std::size_t i = 0; i < image.shape()[0]; i++) {
			for (std::size_t j = 0; j < image.shape()[1]; j++) {
				(*rotated)[i][j] =
					image[image.shape()[0] - i - 1][image.shape()[1] - j - 1];
			}
		}

		return rotated;
	}

	inline std::shared_ptr<boost::multi_array<uint8_t, 3>>
		rotate_270(const boost::multi_array<uint8_t, 3> &image)
	{
		auto rotated = std::shared_ptr<boost::multi_array<uint8_t, 3>>(
			new boost::multi_array<uint8_t, 3>(boost::extents[image.shape()[1]]
				[image.shape()[0]][image.shape()[2]]));

		for (std::size_t i = 0; i < image.shape()[1]; i++) {
			for (std::size_t j = 0; j < image.shape()[0]; j++) {
				(*rotated)[i][j] = image[j][image.shape()[1] - i - 1];
			}
		}

		return rotated;
	}
};

}
}

#endif
