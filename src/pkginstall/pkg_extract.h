#pragma once
// Takes P.T.'s three game archives (chunk1.psarc, texture.qar, pathid_list_ps4.bin) out of a fake PKG made from a dump: the
// same job as the desktop installers' extraction helper (installer/Extractor), in C++ for the Android app's setup screen.
//
// The chain (docs/formats/pkg.md): entry_keys slot 3 opens with the public PKG-derived key 3, which opens image_key, whose
// RSA layer opens with the public fake keyset into the EKPFS. HMAC-SHA256 of the EKPFS and the PFS seed gives the XTS keys
// of the outer PFS; its one file, pfs_image.dat, is a PFSC (zlib-compressed blocks) image of the inner PFS that holds the
// game files. A store (retail) package's image_key does not open with the fake keyset: it is refused, never decrypted.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace pt::pkg {

struct Source {
    // reads exactly `count` bytes at `offset`; false on a short read or an error
    std::function<bool(uint64_t offset, void* buffer, size_t count)> read;
    uint64_t size = 0;
};

struct Progress {
    uint64_t done = 0;   // bytes of the three archives written so far
    uint64_t total = 0;  // bytes of the three archives together
    std::string stage;   // what is being done, for the screen
};

struct Result {
    bool ok = false;
    bool cancelled = false;
    std::string error;       // a sentence for the player when !ok
    std::string content_id;  // UP4511-CUSA01127_00-PPPPPPPPTTTTTTTT
    std::string title_id;    // CUSA01127
    std::vector<std::string> warnings;
};

// The fixed names written into `destination` (created if missing; files already there with these names are replaced only
// when the whole extraction succeeds). `progress` returns false to cancel.
Result ExtractPt(const Source& source, const std::filesystem::path& destination,
                 const std::function<bool(const Progress&)>& progress = {});

}  // namespace pt::pkg
