#include <emscripten/bind.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

struct FormatContextDeleter {
    void operator()(AVFormatContext* context) const { avformat_close_input(&context); }
};
struct CodecContextDeleter {
    void operator()(AVCodecContext* context) const { avcodec_free_context(&context); }
};
struct FrameDeleter {
    void operator()(AVFrame* frame) const { av_frame_free(&frame); }
};
struct PacketDeleter {
    void operator()(AVPacket* packet) const { av_packet_free(&packet); }
};
struct SwsDeleter {
    void operator()(SwsContext* context) const { sws_freeContext(context); }
};
struct IoDeleter {
    void operator()(AVIOContext* io) const {
        if (io) {
            av_freep(&io->buffer);
            avio_context_free(&io);
        }
    }
};

std::string ffmpegError(int code) {
    char buffer[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(code, buffer, sizeof(buffer));
    return buffer;
}

[[noreturn]] void throwError(const std::string& message) {
    emscripten::val::global("Error").new_(message).throw_();
    __builtin_unreachable();
}

std::vector<unsigned char> copyBytes(const emscripten::val& input) {
    const std::size_t length = input["length"].as<std::size_t>();
    std::vector<unsigned char> bytes(length);
    if (length > 0) {
        emscripten::val(emscripten::typed_memory_view(bytes.size(), bytes.data())).call<void>("set", input);
    }
    return bytes;
}

struct MemoryReader {
    const unsigned char* data = nullptr;
    std::size_t size = 0;
    std::size_t position = 0;
};

int readMemory(void* opaque, std::uint8_t* buffer, int size) {
    auto* reader = static_cast<MemoryReader*>(opaque);
    const std::size_t remaining = reader->size - reader->position;
    if (remaining == 0) {
        return AVERROR_EOF;
    }
    const std::size_t count = std::min(remaining, static_cast<std::size_t>(size));
    std::memcpy(buffer, reader->data + reader->position, count);
    reader->position += count;
    return static_cast<int>(count);
}

std::int64_t seekMemory(void* opaque, std::int64_t offset, int whence) {
    auto* reader = static_cast<MemoryReader*>(opaque);
    if (whence == AVSEEK_SIZE) {
        return static_cast<std::int64_t>(reader->size);
    }
    std::int64_t target;
    switch (whence & ~AVSEEK_FORCE) {
        case SEEK_SET: target = offset; break;
        case SEEK_CUR: target = static_cast<std::int64_t>(reader->position) + offset; break;
        case SEEK_END: target = static_cast<std::int64_t>(reader->size) + offset; break;
        default: return -1;
    }
    if (target < 0 || target > static_cast<std::int64_t>(reader->size)) {
        return -1;
    }
    reader->position = static_cast<std::size_t>(target);
    return target;
}

// A video file held in memory and opened with FFmpeg. Must not be moved or copied
// because the AVIO context points at `reader`.
struct Input {
    MemoryReader reader;
    std::unique_ptr<AVIOContext, IoDeleter> io;
    std::unique_ptr<AVFormatContext, FormatContextDeleter> format;
    int videoStream = -1;

    Input() = default;
    Input(const Input&) = delete;
    Input& operator=(const Input&) = delete;

    AVStream* stream() const { return format->streams[videoStream]; }
};

// Returns an empty string on success, otherwise the failure reason.
std::string openInput(const std::vector<unsigned char>& data, Input& input) {
    if (data.empty()) {
        return "Video data is empty";
    }

    input.reader = MemoryReader{data.data(), data.size(), 0};
    constexpr int kIoBufferSize = 64 * 1024;
    auto* ioBuffer = static_cast<unsigned char*>(av_malloc(kIoBufferSize));
    if (!ioBuffer) {
        return "Out of memory";
    }
    input.io.reset(avio_alloc_context(ioBuffer, kIoBufferSize, 0, &input.reader, readMemory, nullptr, seekMemory));
    if (!input.io) {
        av_free(ioBuffer);
        return "Out of memory";
    }

    AVFormatContext* rawFormat = avformat_alloc_context();
    if (!rawFormat) {
        return "Out of memory";
    }
    rawFormat->pb = input.io.get();
    rawFormat->flags |= AVFMT_FLAG_CUSTOM_IO;
    int result = avformat_open_input(&rawFormat, "", nullptr, nullptr);
    if (result < 0) {
        return "Unable to open the video: " + ffmpegError(result);
    }
    input.format.reset(rawFormat);

    result = avformat_find_stream_info(input.format.get(), nullptr);
    if (result < 0) {
        return "Unable to read the video streams: " + ffmpegError(result);
    }
    input.videoStream = av_find_best_stream(input.format.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (input.videoStream < 0) {
        return "No video stream found";
    }
    return "";
}

struct VideoMetadata {
    std::string container;
    std::uint64_t fileSize = 0;
    double duration = 0;
    std::string videoCodec;
    unsigned int width = 0;
    unsigned int height = 0;
    double frameRate = 0;
};

std::string readMetadata(const std::vector<unsigned char>& data, VideoMetadata& out) {
    Input input;
    const std::string error = openInput(data, input);
    if (!error.empty()) {
        return error;
    }

    const AVStream* stream = input.stream();
    const AVInputFormat* inputFormat = input.format->iformat;
    out.container = inputFormat->long_name ? inputFormat->long_name : inputFormat->name;
    out.fileSize = data.size();
    if (input.format->duration != AV_NOPTS_VALUE) {
        out.duration = static_cast<double>(input.format->duration) / AV_TIME_BASE;
    } else if (stream->duration != AV_NOPTS_VALUE) {
        out.duration = stream->duration * av_q2d(stream->time_base);
    }
    out.videoCodec = avcodec_get_name(stream->codecpar->codec_id);
    out.width = stream->codecpar->width;
    out.height = stream->codecpar->height;
    const AVRational rate = av_guess_frame_rate(input.format.get(), input.stream(), nullptr);
    if (rate.num > 0 && rate.den > 0) {
        out.frameRate = av_q2d(rate);
    }
    return "";
}

VideoMetadata loadOrThrow(const std::vector<unsigned char>& data) {
    VideoMetadata metadata;
    const std::string error = readMetadata(data, metadata);
    if (!error.empty()) {
        throwError(error);
    }
    return metadata;
}

// Encodes a decoded frame as a JPEG image.
std::string encodeJpeg(const AVFrame* source, std::vector<unsigned char>& jpeg) {
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
    if (!encoder) {
        return "JPEG encoder is not available";
    }

    std::unique_ptr<AVCodecContext, CodecContextDeleter> context(avcodec_alloc_context3(encoder));
    if (!context) {
        return "Unable to allocate the JPEG encoder";
    }
    context->width = source->width;
    context->height = source->height;
    context->pix_fmt = AV_PIX_FMT_YUV420P;
    context->strict_std_compliance = FF_COMPLIANCE_UNOFFICIAL;
    context->time_base = AVRational{1, 25};
    context->color_range = AVCOL_RANGE_JPEG;
    context->flags |= AV_CODEC_FLAG_QSCALE;
    context->global_quality = FF_QP2LAMBDA * 3;
    int result = avcodec_open2(context.get(), encoder, nullptr);
    if (result < 0) {
        return "Unable to open the JPEG encoder: " + ffmpegError(result);
    }

    std::unique_ptr<AVFrame, FrameDeleter> converted(av_frame_alloc());
    converted->format = AV_PIX_FMT_YUV420P;
    converted->color_range = AVCOL_RANGE_JPEG;
    converted->width = source->width;
    converted->height = source->height;
    result = av_frame_get_buffer(converted.get(), 0);
    if (result < 0) {
        return "Unable to allocate the image buffer: " + ffmpegError(result);
    }

    std::unique_ptr<SwsContext, SwsDeleter> scaler(sws_getContext(
        source->width, source->height, static_cast<AVPixelFormat>(source->format),
        source->width, source->height, AV_PIX_FMT_YUV420P,
        SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!scaler) {
        return "Unable to convert the frame to YUV 4:2:0";
    }
    const int* coefficients = sws_getCoefficients(SWS_CS_DEFAULT);
    sws_setColorspaceDetails(scaler.get(), coefficients, source->color_range == AVCOL_RANGE_JPEG,
                             coefficients, 1, 0, 1 << 16, 1 << 16);
    sws_scale(scaler.get(), source->data, source->linesize, 0, source->height,
              converted->data, converted->linesize);
    converted->quality = FF_QP2LAMBDA * 3;

    std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    result = avcodec_send_frame(context.get(), converted.get());
    if (result >= 0) {
        result = avcodec_receive_packet(context.get(), packet.get());
    }
    if (result < 0) {
        return "Unable to encode the frame as JPEG: " + ffmpegError(result);
    }
    jpeg.assign(packet->data, packet->data + packet->size);
    return "";
}

// Decodes frames in display order and encodes the one at `index` as JPEG.
std::string extractFrame(const std::vector<unsigned char>& data, int index, std::vector<unsigned char>& jpeg) {
    if (index < 0) {
        return "Frame index must not be negative";
    }

    Input input;
    std::string error = openInput(data, input);
    if (!error.empty()) {
        return error;
    }
    AVStream* stream = input.stream();

    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (!decoder) {
        return std::string("No decoder available for video codec: ") + avcodec_get_name(stream->codecpar->codec_id);
    }
    std::unique_ptr<AVCodecContext, CodecContextDeleter> codec(avcodec_alloc_context3(decoder));
    if (!codec || avcodec_parameters_to_context(codec.get(), stream->codecpar) < 0) {
        return "Unable to set up the video decoder";
    }
    const int opened = avcodec_open2(codec.get(), decoder, nullptr);
    if (opened < 0) {
        return "Unable to open the video decoder: " + ffmpegError(opened);
    }

    std::unique_ptr<AVPacket, PacketDeleter> packet(av_packet_alloc());
    std::unique_ptr<AVFrame, FrameDeleter> frame(av_frame_alloc());
    int decoded = 0;
    bool found = false;

    // Returns false once the target frame was handled or an error occurred.
    auto drain = [&]() {
        while (true) {
            const int received = avcodec_receive_frame(codec.get(), frame.get());
            if (received == AVERROR(EAGAIN) || received == AVERROR_EOF) {
                return true;
            }
            if (received < 0) {
                error = "Unable to decode the video: " + ffmpegError(received);
                return false;
            }
            if (decoded++ == index) {
                found = true;
                error = encodeJpeg(frame.get(), jpeg);
                return false;
            }
        }
    };

    bool keepGoing = true;
    while (keepGoing && av_read_frame(input.format.get(), packet.get()) >= 0) {
        if (packet->stream_index == input.videoStream) {
            const int sent = avcodec_send_packet(codec.get(), packet.get());
            if (sent < 0 && sent != AVERROR(EAGAIN)) {
                error = "Unable to decode the video: " + ffmpegError(sent);
                keepGoing = false;
            } else {
                keepGoing = drain();
            }
        }
        av_packet_unref(packet.get());
    }
    if (keepGoing) {
        avcodec_send_packet(codec.get(), nullptr);
        drain();
    }

    if (!error.empty()) {
        return error;
    }
    if (!found) {
        return "Frame index " + std::to_string(index) + " is out of range (the video has " +
               std::to_string(decoded) + " frames)";
    }
    return "";
}

}  // namespace

int add(int a, int b) {
    std::printf("Adding %d and %d\n", a, b);
    return a + b;
}

int multiply(int a, int b) {
    std::printf("Multiplying %d and %d\n", a, b);
    return a * b;
}

class Video {
public:
    // Throws a JavaScript Error when the data is not a readable video.
    explicit Video(const emscripten::val& bytes)
        : data_(copyBytes(bytes)), metadata_(loadOrThrow(data_)) {}

    std::string container() const { return metadata_.container; }
    double fileSize() const { return static_cast<double>(metadata_.fileSize); }
    double duration() const { return metadata_.duration; }
    std::string videoCodec() const { return metadata_.videoCodec; }
    unsigned int width() const { return metadata_.width; }
    unsigned int height() const { return metadata_.height; }
    double frameRate() const { return metadata_.frameRate; }

    // Returns the JPEG file bytes of the frame at `index` (0-based, display order).
    emscripten::val getFrame(int index) const {
        std::vector<unsigned char> jpeg;
        const std::string error = extractFrame(data_, index, jpeg);
        if (!error.empty()) {
            throwError(error);
        }
        return emscripten::val::global("Uint8Array").new_(
            emscripten::typed_memory_view(jpeg.size(), jpeg.data()));
    }

    void printMetadata() const {
        std::printf("Video metadata:\n");
        std::printf("Container: %s\n", metadata_.container.c_str());
        std::printf("File size: %llu bytes\n", static_cast<unsigned long long>(metadata_.fileSize));
        std::printf("Duration: %.3f seconds\n", metadata_.duration);
        std::printf("Video codec: %s\n", metadata_.videoCodec.c_str());
        std::printf("Resolution: %u x %u\n", metadata_.width, metadata_.height);
        if (metadata_.frameRate > 0) {
            std::printf("Average frame rate: %.3f fps\n", metadata_.frameRate);
        }
    }

private:
    std::vector<unsigned char> data_;
    VideoMetadata metadata_;
};

EMSCRIPTEN_BINDINGS(my_math_module) {
    emscripten::function("add", &add);
    emscripten::function("multiply", &multiply);
    emscripten::class_<Video>("Video")
        .constructor<const emscripten::val&>()
        .property("container", &Video::container)
        .property("fileSize", &Video::fileSize)
        .property("duration", &Video::duration)
        .property("videoCodec", &Video::videoCodec)
        .property("width", &Video::width)
        .property("height", &Video::height)
        .property("frameRate", &Video::frameRate)
        .function("getFrame", &Video::getFrame)
        .function("printMetadata", &Video::printMetadata);
}
