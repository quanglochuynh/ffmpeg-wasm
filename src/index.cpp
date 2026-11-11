#include <emscripten/bind.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

struct Atom {
    std::string type;
    std::size_t payload;
    std::size_t end;
};

struct Track {
    std::string handler;
    std::string codec;
    unsigned int width = 0;
    unsigned int height = 0;
    std::uint32_t timescale = 0;
    std::uint64_t sampleCount = 0;
    std::uint64_t sampleDuration = 0;
};

struct Movie {
    std::string majorBrand;
    std::uint32_t timescale = 0;
    std::uint64_t duration = 0;
    std::vector<Track> tracks;
};

std::uint32_t readU32(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return (static_cast<std::uint32_t>(bytes[offset]) << 24) |
           (static_cast<std::uint32_t>(bytes[offset + 1]) << 16) |
           (static_cast<std::uint32_t>(bytes[offset + 2]) << 8) |
           static_cast<std::uint32_t>(bytes[offset + 3]);
}

std::uint64_t readU64(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return (static_cast<std::uint64_t>(readU32(bytes, offset)) << 32) |
           readU32(bytes, offset + 4);
}

std::string readFourCC(const std::vector<unsigned char>& bytes, std::size_t offset) {
    return std::string(bytes.begin() + offset, bytes.begin() + offset + 4);
}

bool readAtom(
    const std::vector<unsigned char>& bytes,
    std::size_t cursor,
    std::size_t parentEnd,
    Atom& atom,
    std::string& error
) {
    if (cursor > parentEnd || parentEnd - cursor < 8) {
        error = "Truncated atom header";
        return false;
    }

    std::uint64_t size = readU32(bytes, cursor);
    std::size_t headerSize = 8;
    atom.type = readFourCC(bytes, cursor + 4);

    if (size == 1) {
        if (parentEnd - cursor < 16) {
            error = "Truncated extended atom header";
            return false;
        }
        size = readU64(bytes, cursor + 8);
        headerSize = 16;
    } else if (size == 0) {
        size = parentEnd - cursor;
    }

    if (size < headerSize || size > parentEnd - cursor) {
        error = "Invalid atom size";
        return false;
    }

    atom.payload = cursor + headerSize;
    atom.end = cursor + static_cast<std::size_t>(size);
    return true;
}

bool parseMovieHeader(
    const std::vector<unsigned char>& bytes,
    const Atom& atom,
    Movie& movie,
    std::string& error
) {
    if (atom.payload >= atom.end) {
        error = "Invalid movie header";
        return false;
    }

    const unsigned char version = bytes[atom.payload];
    if (version == 0 && atom.end - atom.payload >= 20) {
        movie.timescale = readU32(bytes, atom.payload + 12);
        movie.duration = readU32(bytes, atom.payload + 16);
    } else if (version == 1 && atom.end - atom.payload >= 32) {
        movie.timescale = readU32(bytes, atom.payload + 20);
        movie.duration = readU64(bytes, atom.payload + 24);
    } else {
        error = "Unsupported or truncated movie header";
        return false;
    }
    return true;
}

bool parseTrackHeader(
    const std::vector<unsigned char>& bytes,
    const Atom& atom,
    Track& track,
    std::string& error
) {
    if (atom.end - atom.payload < 12) {
        error = "Truncated track handler";
        return false;
    }
    if (track.handler.empty()) {
        track.handler = readFourCC(bytes, atom.payload + 8);
    }
    return true;
}

bool parseMediaHeader(
    const std::vector<unsigned char>& bytes,
    const Atom& atom,
    Track& track,
    std::string& error
) {
    if (atom.payload >= atom.end) {
        error = "Invalid media header";
        return false;
    }

    const unsigned char version = bytes[atom.payload];
    if (version == 0 && atom.end - atom.payload >= 20) {
        track.timescale = readU32(bytes, atom.payload + 12);
    } else if (version == 1 && atom.end - atom.payload >= 32) {
        track.timescale = readU32(bytes, atom.payload + 20);
    } else {
        error = "Unsupported or truncated media header";
        return false;
    }
    return true;
}

bool parseSampleDescription(
    const std::vector<unsigned char>& bytes,
    const Atom& atom,
    Track& track,
    std::string& error
) {
    if (atom.end - atom.payload < 8) {
        error = "Truncated sample description";
        return false;
    }

    const std::uint32_t entryCount = readU32(bytes, atom.payload + 4);
    std::size_t cursor = atom.payload + 8;
    for (std::uint32_t i = 0; i < entryCount; ++i) {
        Atom entry;
        if (!readAtom(bytes, cursor, atom.end, entry, error)) {
            return false;
        }
        track.codec = entry.type;

        if (track.handler == "vide" && entry.end - cursor >= 36) {
            track.width = (static_cast<unsigned int>(bytes[cursor + 32]) << 8) |
                          bytes[cursor + 33];
            track.height = (static_cast<unsigned int>(bytes[cursor + 34]) << 8) |
                           bytes[cursor + 35];
        }
        cursor = entry.end;
    }
    return true;
}

bool parseTimeToSample(
    const std::vector<unsigned char>& bytes,
    const Atom& atom,
    Track& track,
    std::string& error
) {
    if (atom.end - atom.payload < 8) {
        error = "Truncated time-to-sample table";
        return false;
    }

    const std::uint32_t entryCount = readU32(bytes, atom.payload + 4);
    std::size_t cursor = atom.payload + 8;
    if (entryCount > (atom.end - cursor) / 8) {
        error = "Invalid time-to-sample table";
        return false;
    }

    for (std::uint32_t i = 0; i < entryCount; ++i) {
        const std::uint32_t count = readU32(bytes, cursor);
        const std::uint32_t duration = readU32(bytes, cursor + 4);
        track.sampleCount += count;
        track.sampleDuration += static_cast<std::uint64_t>(count) * duration;
        cursor += 8;
    }
    return true;
}

bool isContainer(const std::string& type) {
    return type == "moov" || type == "mdia" || type == "minf" ||
           type == "stbl" || type == "edts" || type == "dinf" ||
           type == "tapt" || type == "udta" || type == "meta";
}

bool parseAtoms(
    const std::vector<unsigned char>& bytes,
    std::size_t begin,
    std::size_t end,
    Movie& movie,
    Track* track,
    unsigned int depth,
    std::string& error
) {
    if (depth > 24) {
        error = "Atom nesting is too deep";
        return false;
    }

    std::size_t cursor = begin;
    while (cursor < end) {
        Atom atom;
        if (!readAtom(bytes, cursor, end, atom, error)) {
            return false;
        }

        if (atom.type == "trak") {
            Track newTrack;
            if (!parseAtoms(bytes, atom.payload, atom.end, movie, &newTrack, depth + 1, error)) {
                return false;
            }
            movie.tracks.push_back(newTrack);
        } else if (atom.type == "mvhd") {
            if (!parseMovieHeader(bytes, atom, movie, error)) {
                return false;
            }
        } else if (track && atom.type == "hdlr") {
            if (!parseTrackHeader(bytes, atom, *track, error)) {
                return false;
            }
        } else if (track && atom.type == "mdhd") {
            if (!parseMediaHeader(bytes, atom, *track, error)) {
                return false;
            }
        } else if (track && atom.type == "stsd") {
            if (!parseSampleDescription(bytes, atom, *track, error)) {
                return false;
            }
        } else if (track && atom.type == "stts") {
            if (!parseTimeToSample(bytes, atom, *track, error)) {
                return false;
            }
        } else if (atom.type == "ftyp" && atom.end - atom.payload >= 4) {
            movie.majorBrand = readFourCC(bytes, atom.payload);
        } else if (isContainer(atom.type)) {
            std::size_t childBegin = atom.payload;
            if (atom.type == "meta") {
                if (atom.end - childBegin >= 4 &&
                    (atom.end - childBegin < 8 ||
                     readU32(bytes, childBegin) < 8 ||
                     readU32(bytes, childBegin) > atom.end - childBegin)) {
                    childBegin += 4;
                }
            }
            if (!parseAtoms(bytes, childBegin, atom.end, movie, track, depth + 1, error)) {
                return false;
            }
        }
        cursor = atom.end;
    }
    return true;
}

const char* codecName(const std::string& codec) {
    if (codec == "avc1" || codec == "avc3") return "H.264";
    if (codec == "hvc1" || codec == "hev1") return "HEVC";
    if (codec == "mp4a") return "AAC";
    if (codec == "av01") return "AV1";
    return "unknown";
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

bool printVideoMetadata(const emscripten::val& input) {
    const std::size_t length = input["length"].as<std::size_t>();
    if (length == 0) {
        std::fprintf(stderr, "Video data is empty\n");
        return false;
    }

    std::vector<unsigned char> bytes(length);
    emscripten::val(emscripten::typed_memory_view(bytes.size(), bytes.data())).call<void>("set", input);

    Movie movie;
    std::string error;
    if (!parseAtoms(bytes, 0, bytes.size(), movie, nullptr, 0, error)) {
        std::fprintf(stderr, "Unable to parse video metadata: %s\n", error.c_str());
        return false;
    }

    const Track* video = nullptr;
    for (const Track& track : movie.tracks) {
        if (track.handler == "vide") {
            video = &track;
            break;
        }
    }
    if (!video) {
        std::fprintf(stderr, "No video track found\n");
        return false;
    }
    if (movie.timescale == 0) {
        std::fprintf(stderr, "Video has an invalid or missing duration\n");
        return false;
    }

    const char* container = movie.majorBrand == "qt  " ? "QuickTime/MOV" : "ISO Base Media";
    std::printf("Video metadata:\n");
    std::printf("Container: %s\n", container);
    std::printf("File size: %llu bytes\n", static_cast<unsigned long long>(bytes.size()));
    std::printf("Duration: %.3f seconds\n", static_cast<double>(movie.duration) / movie.timescale);
    std::printf("Video codec: %s (%s)\n", codecName(video->codec), video->codec.c_str());
    std::printf("Resolution: %u x %u\n", video->width, video->height);
    if (video->timescale > 0 && video->sampleDuration > 0) {
        const double seconds = static_cast<double>(video->sampleDuration) / video->timescale;
        if (seconds > 0) {
            std::printf("Average frame rate: %.3f fps\n", video->sampleCount / seconds);
        }
    }
    return true;
}

EMSCRIPTEN_BINDINGS(my_math_module) {
    emscripten::function("add", &add);
    emscripten::function("multiply", &multiply);
    emscripten::function("printVideoMetadata", &printVideoMetadata);
}
