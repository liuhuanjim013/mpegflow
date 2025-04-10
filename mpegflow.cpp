#include <cstdio>
#include <cassert>
#include <cstdlib>
#define __STDC_WANT_LIB_EXT1__ 1  // Request C11 safe functions
extern "C" {
#include <time.h>  // Use only the C version
}
#include <stdint.h>

extern "C"
{
	#include <libavcodec/avcodec.h>
	#include <libavformat/avformat.h>
	#include <libswscale/swscale.h>
	#include <libavutil/motion_vector.h>
}

#include <string>
#include <algorithm>
#include <vector>
#include <stdexcept>

using namespace std;

AVFrame* ffmpeg_pFrame;
AVFormatContext* ffmpeg_pFormatCtx;
AVStream* ffmpeg_pVideoStream;
AVCodecContext* ffmpeg_pCodecCtx;
int ffmpeg_videoStreamIndex;
size_t ffmpeg_frameWidth, ffmpeg_frameHeight;

bool ARG_OUTPUT_RAW_MOTION_VECTORS, ARG_FORCE_GRID_8, ARG_FORCE_GRID_16, ARG_OUTPUT_OCCUPANCY, ARG_QUIET, ARG_HELP;
const char* ARG_VIDEO_PATH;
static int video_frame_count = 0;

static char* get_error_text(int error)
{
	static char error_buffer[255];
	av_strerror(error, error_buffer, sizeof(error_buffer));
	return error_buffer;
}

void ffmpeg_print_error(int err) // copied from cmdutils.c, originally called print_error
{
	char errbuf[128];
	const char *errbuf_ptr = errbuf;

	if (av_strerror(err, errbuf, sizeof(errbuf)) < 0)
		errbuf_ptr = strerror(AVUNERROR(err));
	av_log(NULL, AV_LOG_ERROR, "ffmpeg_print_error: %s\n", errbuf_ptr);
}

void ffmpeg_log_callback_null(void *ptr, int level, const char *fmt, va_list vl)
{
}

void ffmpeg_init()
{
	avformat_network_init();

	if(ARG_QUIET)
	{
		av_log_set_level(AV_LOG_ERROR);
		av_log_set_callback(ffmpeg_log_callback_null);
	} else {
		av_log_set_level(AV_LOG_DEBUG);
	}

	ffmpeg_pFrame = av_frame_alloc();
	ffmpeg_pFormatCtx = avformat_alloc_context();
	ffmpeg_videoStreamIndex = -1;

	int err = 0;

	if ((err = avformat_open_input(&ffmpeg_pFormatCtx, ARG_VIDEO_PATH, NULL, NULL)) != 0)
	{
		ffmpeg_print_error(err);
		throw std::runtime_error("Couldn't open file. Possibly it doesn't exist.");
	}

	if ((err = avformat_find_stream_info(ffmpeg_pFormatCtx, NULL)) < 0)
	{
		ffmpeg_print_error(err);
		throw std::runtime_error("Stream information not found.");
	}
	const AVCodec *dec = NULL;
	AVCodec* non_const_dec=const_cast<AVCodec*>(dec);
	enum AVMediaType type = AVMEDIA_TYPE_VIDEO;
	int ret = av_find_best_stream(ffmpeg_pFormatCtx, type, -1, -1, &non_const_dec, 0);
	if (ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(type), ARG_VIDEO_PATH);
		throw std::runtime_error("Could not find video stream in input file.");
	}

	int stream_idx = ret;
	AVStream *st;
	st = ffmpeg_pFormatCtx->streams[stream_idx];

	ffmpeg_pCodecCtx = avcodec_alloc_context3(dec);
	if (!ffmpeg_pCodecCtx) {
		av_log(NULL, AV_LOG_ERROR, "Failed to allocate codec context %d\n",AVERROR(EINVAL));
		throw std::runtime_error("Failed to allocate codec context.");
	}

	ret = avcodec_parameters_to_context(ffmpeg_pCodecCtx, st->codecpar);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Failed to copy codec parameters to codec context %d\n",ret);
		throw std::runtime_error("Failed to copy codec parameters to codec context.");
	}

	AVDictionary *opts = NULL;
	av_dict_set(&opts, "flags2", "+export_mvs", 0);
	ret = avcodec_open2(ffmpeg_pCodecCtx, dec, &opts);
	av_dict_free(&opts);
	if (ret < 0) {
		av_log(NULL, AV_LOG_ERROR, "Failed to open %s codec\n",
				av_get_media_type_string(type));
		throw std::runtime_error("Failed to open codec.");
	}

	ffmpeg_videoStreamIndex = stream_idx;
	ffmpeg_pVideoStream = ffmpeg_pFormatCtx->streams[ffmpeg_videoStreamIndex];
	ffmpeg_frameWidth = ffmpeg_pCodecCtx->width;
	ffmpeg_frameHeight = ffmpeg_pCodecCtx->height;
}

struct FrameInfo
{
	const static size_t MAX_GRID_SIZE = 512;

	size_t GridStep;
	pair<size_t, size_t> Shape;

	int dx[MAX_GRID_SIZE][MAX_GRID_SIZE];
	int dy[MAX_GRID_SIZE][MAX_GRID_SIZE];
	uint8_t occupancy[MAX_GRID_SIZE][MAX_GRID_SIZE];
	int64_t Pts;
	int FrameIndex;
	char PictType;
	const char* Origin;
	bool Empty;
	bool Printed;

	FrameInfo()
	{
		memset(dx, 0, sizeof(dx));
		memset(dy, 0, sizeof(dy));
		memset(occupancy, 0, sizeof(occupancy));
		Empty = true;
		Printed = false;
		PictType = '?';
		FrameIndex = -1;
		Pts = -1;
		Origin = "";
	}

	void InterpolateFlow(FrameInfo& prev, FrameInfo& next)
	{
		for(int i = 0; i < Shape.first; i++)
		{
			for(int j = 0; j < Shape.second; j++)
			{
				dx[i][j] = (prev.dx[i][j] + next.dx[i][j]) / 2;
				dy[i][j] = (prev.dy[i][j] + next.dy[i][j]) / 2;
			}
		}
		Empty = false;
		Origin = "interpolated";
	}

	void FillInSomeMissingVectorsInGrid8()
	{
		for(int k = 0; k < 2; k++)
		{
			for(int i = 1; i < Shape.first - 1; i++)
			{
				for(int j = 1; j < Shape.second - 1; j++)
				{
					if(occupancy[i][j] == 0)
					{
						if(occupancy[i][j - 1] != 0 && occupancy[i][j + 1] != 0)
						{
							dx[i][j] = (dx[i][j -1] + dx[i][j + 1]) / 2;
							dy[i][j] = (dy[i][j -1] + dy[i][j + 1]) / 2;
							occupancy[i][j] = 2;
						}
						else if(occupancy[i - 1][j] != 0 && occupancy[i + 1][j] != 0)
						{
							dx[i][j] = (dx[i - 1][j] + dx[i + 1][j]) / 2;
							dy[i][j] = (dy[i - 1][j] + dy[i + 1][j]) / 2;
							occupancy[i][j] = 2;
						}
					}
				}
			}
		}
	}

	void PrintIfNotPrinted()
	{
		static int64_t FirstPts = -1;

		if(Printed)
			return;

		if(FirstPts == -1)
			FirstPts = Pts;
		
		printf("# pts=%lld frame_index=%d pict_type=%c output_type=arranged shape=%zux%zu origin=%s\n", (long long) Pts - FirstPts, FrameIndex, PictType, (ARG_OUTPUT_OCCUPANCY ? 3 : 2) * Shape.first, Shape.second, Origin);
		for(int i = 0; i < Shape.first; i++)
		{
			for(int j = 0; j < Shape.second; j++)
			{
				printf("%d\t", dx[i][j]);
			}
			printf("\n");
		}
		for(int i = 0; i < Shape.first; i++)
		{
			for(int j = 0; j < Shape.second; j++)
			{
				printf("%d\t", dy[i][j]);
			}
			printf("\n");
		}

		if(ARG_OUTPUT_OCCUPANCY)
		{
			for(int i = 0; i < Shape.first; i++)
			{
				for(int j = 0; j < Shape.second; j++)
				{
					printf("%d\t", occupancy[i][j]);
				}
				printf("\n");
			}
		}

		Printed = true;
	}
};

const size_t FrameInfo::MAX_GRID_SIZE;

void output_vectors_raw(int frameIndex, int64_t pts, char pictType, vector<AVMotionVector>& motionVectors)
{
	printf("# pts=%lld frame_index=%d pict_type=%c output_type=raw shape=%zux4\n", (long long) pts, frameIndex, pictType, motionVectors.size());
	for(int i = 0; i < motionVectors.size(); i++)
	{
		AVMotionVector& mv = motionVectors[i];
		int mvdx = mv.dst_x - mv.src_x;
		int mvdy = mv.dst_y - mv.src_y;

		printf("%d\t%d\t%d\t%d\n", mv.dst_x, mv.dst_y, mvdx, mvdy);
	}
}

void output_vectors_std(int frameIndex, int64_t pts, char pictType, vector<AVMotionVector>& motionVectors)
{
	static vector<FrameInfo> prev;

	size_t gridStep = ARG_FORCE_GRID_8 ? 8 : 16;
	pair<size_t, size_t> shape = make_pair(min(ffmpeg_frameHeight / gridStep, FrameInfo::MAX_GRID_SIZE), min(ffmpeg_frameWidth / gridStep, FrameInfo::MAX_GRID_SIZE));

	if(!prev.empty() && pts < prev.back().Pts + 1)
	{
		av_log(NULL, AV_LOG_ERROR, "Missing frame(s) between prev.back().Pts=%lld and pts=%lld\n", (long long) prev.back().Pts, (long long) pts);
		for(int64_t dummy_pts = prev.back().Pts + 1; dummy_pts < pts; dummy_pts++)
		{
			FrameInfo dummy;
			dummy.FrameIndex = -1;
			dummy.Pts = dummy_pts;
			dummy.Origin = "dummy";
			dummy.PictType = '?';
			dummy.GridStep = gridStep;
			dummy.Shape = shape;
			prev.push_back(dummy);
		}
	} else if (!prev.empty()) {
		av_log(NULL, AV_LOG_DEBUG, "prev.empty()=%d pts=%lld prev.back().Pts=%lld\n", prev.empty(), (long long) pts, (long long) prev.back().Pts);
	}

	FrameInfo cur;
	cur.FrameIndex = frameIndex;
	cur.Pts = pts;
	cur.Origin = "video";
	cur.PictType = pictType;
	cur.GridStep = gridStep;
	cur.Shape = shape;

	for(int i = 0; i < motionVectors.size(); i++)
	{
		AVMotionVector& mv = motionVectors[i];
		int mvdx = mv.dst_x - mv.src_x;
		int mvdy = mv.dst_y - mv.src_y;

		size_t i_clipped = max(size_t(0), min(mv.dst_y / cur.GridStep, cur.Shape.first - 1)); 
		size_t j_clipped = max(size_t(0), min(mv.dst_x / cur.GridStep, cur.Shape.second - 1));

		cur.Empty = false;
		cur.dx[i_clipped][j_clipped] = mvdx;
		cur.dy[i_clipped][j_clipped] = mvdy;
		cur.occupancy[i_clipped][j_clipped] = true;
	}

	if(cur.GridStep == 8)
		cur.FillInSomeMissingVectorsInGrid8();
	
	if(frameIndex == -1)
	{
		for(int i = 0; i < prev.size(); i++)
			prev[i].PrintIfNotPrinted();
	}
	else if(!motionVectors.empty())
	{
		if(prev.size() == 2 && prev.front().Empty == false)
		{
			prev.back().InterpolateFlow(prev.front(), cur);
			prev.back().PrintIfNotPrinted();
		}
		else
		{
			for(int i = 0; i < prev.size(); i++)
				prev[i].PrintIfNotPrinted();
		}
		prev.clear();
		cur.PrintIfNotPrinted();
	}

	prev.push_back(cur);
}

void parse_options(int argc, const char* argv[])
{
	for(int i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0)
			ARG_HELP = true;
		else if(strcmp(argv[i], "--raw") == 0)
			ARG_OUTPUT_RAW_MOTION_VECTORS = true;
		else if(strcmp(argv[i], "--grid8x8") == 0)
			ARG_FORCE_GRID_8 = true;
		else if(strcmp(argv[i], "--occupancy") == 0)
			ARG_OUTPUT_OCCUPANCY = true;
		else if(strcmp(argv[i], "-q") == 0 || strcmp(argv[i], "--quiet") == 0)
			ARG_QUIET = true;
		else
			ARG_VIDEO_PATH = argv[i];
	}
	if(ARG_HELP || ARG_VIDEO_PATH == NULL)
	{
		fprintf(stderr, "Usage: mpegflow [--raw | [[--grid8x8] [--occupancy]]] videoPath\n  --help and -h will output this help message.\n  --raw will prevent motion vectors from being arranged in matrices.\n  --grid8x8 will force fine 8x8 grid.\n  --occupancy will append occupancy matrix after motion vector matrices.\n  --quiet will suppress debug output.\n");
		exit(1);
	}
}

int main(int argc, const char* argv[])
{
	parse_options(argc, argv);
	ffmpeg_init();
	av_log(NULL, AV_LOG_DEBUG, "1 Reading frame\n");
	AVPacket pkt;
	int64_t pts, prev_pts = -1;
	char pictType;
	vector<AVMotionVector> motionVectors;
	while (av_read_frame(ffmpeg_pFormatCtx, &pkt) >= 0) {
		av_log(NULL, AV_LOG_DEBUG, "2 got packet\n");
        if (pkt.stream_index == ffmpeg_videoStreamIndex) {
			int ret = avcodec_send_packet(ffmpeg_pCodecCtx, &pkt);
			if (ret < 0) {
				av_log(NULL, AV_LOG_ERROR, "Error while sending a packet to the decoder: %s\n", get_error_text(ret));
				return -1;
			}

			while (ret >= 0)  {
				ret = avcodec_receive_frame(ffmpeg_pCodecCtx, ffmpeg_pFrame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					break;
				} else if (ret < 0) {
					av_log(NULL, AV_LOG_ERROR, "Error while receiving a frame from the decoder: %s\n", get_error_text(ret));
					return -1;
				}

				if (ret >= 0) {
					video_frame_count++;
					pictType = av_get_picture_type_char(ffmpeg_pFrame->pict_type);
					// fragile, consult fresh f_select.c and ffprobe.c when updating ffmpeg
					av_log(NULL, AV_LOG_DEBUG, "ffmpeg_pFrame->pts=%lld ffmpeg_pFrame->pkt_dts=%lld pts=%lld\n", (long long) ffmpeg_pFrame->pts, (long long) ffmpeg_pFrame->pkt_dts, (long long) pts);
					if (ffmpeg_pFrame->pts != AV_NOPTS_VALUE) {
						pts = ffmpeg_pFrame->pts; // Use PTS if it is valid
					} else if (ffmpeg_pFrame->pkt_dts != AV_NOPTS_VALUE) {
							if (ffmpeg_pFrame->pkt_dts != AV_NOPTS_VALUE) { // Handle the case where PTS is not defined
								pts = ffmpeg_pFrame->pkt_dts; // Use DTS if PTS is not available
							} else { // Fallback to incrementing the last known PTS
								pts = pts + 1;
							}
					} else {
						pts = pts + 1; // Increment the last known PTS if both are invalid
					}

					av_log(NULL, AV_LOG_DEBUG, "Frame pts=%lld pict_type=%c\n", (long long) pts, pictType);
					AVFrameSideData *sd;
					sd = av_frame_get_side_data(ffmpeg_pFrame, AV_FRAME_DATA_MOTION_VECTORS);
					if (sd) {
						const AVMotionVector *mvs = (const AVMotionVector *)sd->data;
						int mvcount = sd->size / sizeof(AVMotionVector);
						motionVectors = vector<AVMotionVector>(mvs, mvs + mvcount);
						av_log(NULL, AV_LOG_DEBUG, "Frame has motion %d vectors\n", mvcount);
					} else {
						av_log(NULL, AV_LOG_DEBUG, "Frame has no motion vectors\n");
					}
					if(pts <= prev_pts && prev_pts != -1)
					{
						if(!ARG_QUIET)
							av_log(NULL, AV_LOG_ERROR, "Skipping frame %d (frame with pts %d already processed).\n", int(video_frame_count), int(pts));
						continue;
					}

					av_log(NULL, AV_LOG_DEBUG, "3 outputting vectors video_frame_count=%d pts=%lld pict_type=%c\n", video_frame_count, (long long) pts, pictType);
					if(ARG_OUTPUT_RAW_MOTION_VECTORS)
						output_vectors_raw(video_frame_count, pts, pictType, motionVectors);
					else
						output_vectors_std(video_frame_count, pts, pictType, motionVectors);

					prev_pts = pts;

				}
				av_frame_unref(ffmpeg_pFrame);
			}
		}
    }
	av_packet_unref(&pkt);
if(ARG_OUTPUT_RAW_MOTION_VECTORS == false)
		output_vectors_std(-1, pts, pictType, motionVectors);
		
}
