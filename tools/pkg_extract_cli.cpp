// pt_pkg_extract <package.pkg> <output folder>: the Android setup's extractor (src/pkginstall) from a terminal, to check it
// against the desktop installer's output (the SHA-256 of the three archives must match).
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>

#include "pkginstall/pkg_extract.h"

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s <package.pkg> <output folder>\n", argv[0]);
        return 2;
    }
    const int fd = open(argv[1], O_RDONLY);
    struct stat st{};
    if (fd < 0 || fstat(fd, &st) != 0) {
        std::perror(argv[1]);
        return 1;
    }
    pt::pkg::Source source;
    source.size = static_cast<uint64_t>(st.st_size);
    source.read = [fd](uint64_t offset, void* buffer, size_t count) {
        auto* out = static_cast<char*>(buffer);
        while (count > 0) {
            const ssize_t got = pread(fd, out, count, static_cast<off_t>(offset));
            if (got <= 0) return false;
            out += got;
            offset += static_cast<uint64_t>(got);
            count -= static_cast<size_t>(got);
        }
        return true;
    };
    const auto start = std::chrono::steady_clock::now();
    int last = -1;
    const auto result = pt::pkg::ExtractPt(source, argv[2], [&](const pt::pkg::Progress& p) {
        const int percent = p.total ? static_cast<int>(p.done * 100 / p.total) : 0;
        if (percent != last && percent % 10 == 0) std::printf("%3d%% %s\n", percent, p.stage.c_str());
        last = percent;
        return true;
    });
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::printf("content %s, title %s, %.1f s\n", result.content_id.c_str(), result.title_id.c_str(), seconds);
    for (const auto& w : result.warnings) std::printf("warning: %s\n", w.c_str());
    if (!result.ok) {
        std::fprintf(stderr, "error: %s\n", result.error.c_str());
        return 1;
    }
    std::printf("ok\n");
    return 0;
}
