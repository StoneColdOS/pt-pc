#include "pkginstall/pkg_extract.h"

#include <mbedtls/aes.h>
#include <mbedtls/md.h>
#include <mbedtls/rsa.h>
#include <mbedtls/sha256.h>
#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <random>

#include "pkginstall/pkg_keys.h"

namespace pt::pkg {
namespace {

// the wording of the desktop helper (installer/Extractor/Program.cs), so both installers say the same thing
constexpr const char* kRetailMessage =
    "This is a store (PlayStation Store) package, or it is damaged. Store packages are encrypted for the console that owns "
    "the license and are not opened here. Use a fake PKG (fPKG) built from your own dump of P.T.";
constexpr const char* kNotPtMessage =
    "This package does not hold P.T.'s game data. Choose the P.T. package (CUSA01127, CUSA01114 or CUSA01098).";
constexpr const char* kNotPkgMessage = "This is not a PS4 package (.pkg). Choose the P.T. .pkg file.";
constexpr const char* kDamagedMessage = "The package is damaged or incomplete (it may not have finished downloading or copying).";

constexpr uint32_t kPkgMagic = 0x7F434E54;
constexpr int64_t kPfsMagic = 20130315;
constexpr uint32_t kPfscMagic = 0x43534650;
constexpr uint32_t kXtsSector = 0x1000;

const std::map<std::string, std::string> kReleases = {{"CUSA01127", "US"}, {"CUSA01114", "Europe"}, {"CUSA01098", "Japan"}};

uint32_t Be32(const uint8_t* p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
uint64_t Be64(const uint8_t* p) { return (uint64_t(Be32(p)) << 32) | Be32(p + 4); }
template <typename T> T Le(const uint8_t* p) {
    T v{};
    std::memcpy(&v, p, sizeof(T));  // every target here is little-endian
    return v;
}

struct Failure {
    std::string message;
};
[[noreturn]] void Fail(const std::string& message) { throw Failure{message}; }

int Random(void*, unsigned char* out, size_t size) {
    static std::random_device device;
    for (size_t i = 0; i < size; ++i) out[i] = static_cast<unsigned char>(device());
    return 0;
}

// RSA-2048 private key operation with PKCS#1 v1.5 padding; nullopt when the padding does not check (wrong key)
std::optional<std::vector<uint8_t>> RsaDecrypt(const uint8_t* ciphertext, const uint8_t* n, const uint8_t* e, size_t e_size,
                                               const uint8_t* d, const uint8_t* p, const uint8_t* q) {
    mbedtls_rsa_context rsa;
    mbedtls_rsa_init(&rsa);
    std::optional<std::vector<uint8_t>> result;
    if (mbedtls_rsa_set_padding(&rsa, MBEDTLS_RSA_PKCS_V15, MBEDTLS_MD_NONE) == 0 &&
        mbedtls_rsa_import_raw(&rsa, n, 256, p, 128, q, 128, d, 256, e, e_size) == 0 && mbedtls_rsa_complete(&rsa) == 0) {
        std::vector<uint8_t> out(256);
        size_t size = 0;
        if (mbedtls_rsa_pkcs1_decrypt(&rsa, Random, nullptr, &size, ciphertext, out.data(), out.size()) == 0) {
            out.resize(size);
            result = std::move(out);
        }
    }
    mbedtls_rsa_free(&rsa);
    return result;
}

std::array<uint8_t, 32> Sha256(const void* data, size_t size) {
    std::array<uint8_t, 32> out{};
    mbedtls_sha256(static_cast<const unsigned char*>(data), size, out.data(), 0);
    return out;
}

std::array<uint8_t, 32> HmacSha256(const uint8_t* key, size_t key_size, const void* data, size_t size) {
    std::array<uint8_t, 32> out{};
    mbedtls_md_hmac(mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), key, key_size, static_cast<const unsigned char*>(data), size,
                    out.data());
    return out;
}

using ReadFn = std::function<void(uint64_t offset, uint8_t* buffer, size_t count)>;  // throws Failure on a bad read

// A PFS image (outer or inner): superblock, inode table, directories, and where a file's bytes are
class Pfs {
public:
    struct Inode {
        int64_t size = 0;
        uint32_t blocks = 0;
        std::vector<int64_t> direct;  // db[0..11]
        std::vector<int64_t> indirect;  // ib[0..4]
    };
    struct Entry {
        std::string name;
        int type = 0;  // 2 file, 3 directory
        uint32_t inode = 0;
    };

    Pfs(ReadFn raw, std::optional<std::array<uint8_t, 32>> ekpfs, uint64_t pkg_pfs_flags) : raw_(std::move(raw)) {
        uint8_t header[0x400];
        raw_(0, header, sizeof(header));
        if (Le<int64_t>(header) != 1 || Le<int64_t>(header + 8) != kPfsMagic) Fail(kDamagedMessage);
        mode_ = Le<uint16_t>(header + 0x1C);
        block_size_ = Le<uint32_t>(header + 0x20);
        const int64_t inode_count = Le<int64_t>(header + 0x30);
        const int64_t inode_blocks = Le<int64_t>(header + 0x40);
        if (block_size_ < kXtsSector || block_size_ > (1u << 20) || inode_count < 1 || inode_count > 1'000'000 || inode_blocks < 1 ||
            inode_blocks > 10'000)
            Fail(kDamagedMessage);
        if (mode_ & 0x4) {
            // the XTS keys: HMAC-SHA256(EKPFS, u32 1 || seed), tweak key then data key; the newer key derivation hashes the
            // seed into the EKPFS first (package flag 0x2000000000000000)
            if (!ekpfs) Fail(kDamagedMessage);
            const uint8_t* seed = header + 0x370;
            std::array<uint8_t, 32> key = *ekpfs;
            if (pkg_pfs_flags & 0x2000000000000000ULL) key = HmacSha256(ekpfs->data(), 32, seed, 16);
            uint8_t message[20];
            const uint32_t index = 1;
            std::memcpy(message, &index, 4);
            std::memcpy(message + 4, seed, 16);
            const auto keys = HmacSha256(key.data(), key.size(), message, sizeof(message));
            uint8_t xts_key[32];
            std::memcpy(xts_key, keys.data() + 16, 16);  // data key
            std::memcpy(xts_key + 16, keys.data(), 16);  // tweak key
            mbedtls_aes_xts_init(&xts_);
            if (mbedtls_aes_xts_setkey_dec(&xts_, xts_key, 256) != 0) Fail(kDamagedMessage);
            encrypted_ = true;
        }
        const bool is_signed = mode_ & 0x1;
        const size_t inode_size = is_signed ? 0x2C8 : 0xA8;
        std::vector<uint8_t> block(block_size_);
        const size_t per_block = block_size_ / inode_size;
        for (int64_t b = 0; b < inode_blocks && static_cast<int64_t>(inodes_.size()) < inode_count; ++b) {
            Read(block_size_ * (1 + b), block.data(), block.size());
            for (size_t j = 0; j < per_block && static_cast<int64_t>(inodes_.size()) < inode_count; ++j) {
                const uint8_t* p = block.data() + j * inode_size;
                Inode inode;
                inode.size = Le<int64_t>(p + 8);
                inode.blocks = Le<uint32_t>(p + 0x60);
                for (int i = 0; i < 17; ++i) {
                    const int64_t value = is_signed ? Le<int32_t>(p + 0x64 + i * 36 + 32) : Le<int32_t>(p + 0x64 + i * 4);
                    (i < 12 ? inode.direct : inode.indirect).push_back(value);
                }
                inodes_.push_back(std::move(inode));
            }
        }
    }
    ~Pfs() {
        if (encrypted_) mbedtls_aes_xts_free(&xts_);
    }
    Pfs(const Pfs&) = delete;
    Pfs& operator=(const Pfs&) = delete;

    uint32_t BlockSize() const { return block_size_; }

    const Inode& InodeAt(uint32_t index) const {
        if (index >= inodes_.size()) Fail(kDamagedMessage);
        return inodes_[index];
    }

    // decrypted bytes of the image
    void Read(uint64_t offset, uint8_t* out, size_t count) {
        if (!encrypted_) {
            raw_(offset, out, count);
            return;
        }
        const uint64_t first = offset / kXtsSector;
        const uint64_t last = (offset + count - 1) / kXtsSector;
        const uint64_t start_sector = block_size_ / kXtsSector;  // the superblock's first block is not encrypted
        std::vector<uint8_t>& buffer = sector_buffer_;
        buffer.resize((last - first + 1) * kXtsSector);
        raw_(first * kXtsSector, buffer.data(), buffer.size());
        for (uint64_t s = first; s <= last; ++s) {
            if (s < start_sector) continue;
            uint8_t* sector = buffer.data() + (s - first) * kXtsSector;
            uint8_t unit[16] = {};
            std::memcpy(unit, &s, 8);
            if (mbedtls_aes_crypt_xts(&xts_, MBEDTLS_AES_DECRYPT, kXtsSector, unit, sector, sector) != 0) Fail(kDamagedMessage);
        }
        std::memcpy(out, buffer.data() + (offset - first * kXtsSector), count);
    }

    std::vector<Entry> List(uint32_t dir) {
        const Inode& inode = InodeAt(dir);
        std::vector<Entry> entries;
        std::vector<uint8_t> block(block_size_);
        for (int64_t b : Blocks(inode)) {
            Read(static_cast<uint64_t>(b) * block_size_, block.data(), block.size());
            for (size_t p = 0; p + 16 <= block.size();) {
                const uint32_t number = Le<uint32_t>(block.data() + p);
                const int32_t type = Le<int32_t>(block.data() + p + 4);
                const int32_t name_size = Le<int32_t>(block.data() + p + 8);
                const int32_t entry_size = Le<int32_t>(block.data() + p + 12);
                if (entry_size <= 0) break;
                if (name_size < 0 || p + 16 + static_cast<size_t>(name_size) > block.size()) Fail(kDamagedMessage);
                entries.push_back({std::string(reinterpret_cast<const char*>(block.data() + p + 16), static_cast<size_t>(name_size)), type, number});
                p += static_cast<size_t>(entry_size);
            }
        }
        return entries;
    }

    // a file's blocks in order; a read-only image keeps them contiguous, a signed one lists them through indirect blocks
    std::vector<int64_t> Blocks(const Inode& inode) {
        std::vector<int64_t> blocks;
        if (inode.blocks == 0) return blocks;
        if (inode.blocks > 50'000'000 || inode.direct[0] < 1) Fail(kDamagedMessage);
        const bool listed = (mode_ & 0x1) && inode.blocks > 1 && inode.direct[1] != -1;
        if (!listed) {
            for (uint32_t i = 0; i < inode.blocks; ++i) blocks.push_back(inode.direct[0] + i);
            return blocks;
        }
        for (uint32_t i = 0; i < 12 && i < inode.blocks; ++i) blocks.push_back(inode.direct[i]);
        const size_t per_block = block_size_ / 36;
        std::vector<uint8_t> table(block_size_);
        auto read_table = [&](int64_t block) { Read(static_cast<uint64_t>(block) * block_size_, table.data(), table.size()); };
        if (blocks.size() < inode.blocks) {
            read_table(inode.indirect[0]);
            for (size_t i = 0; i < per_block && blocks.size() < inode.blocks; ++i) blocks.push_back(Le<int32_t>(table.data() + i * 36 + 32));
        }
        if (blocks.size() < inode.blocks) {
            std::vector<uint8_t> level(block_size_);
            Read(static_cast<uint64_t>(inode.indirect[1]) * block_size_, level.data(), level.size());
            for (size_t j = 0; j < per_block && blocks.size() < inode.blocks; ++j) {
                read_table(Le<int32_t>(level.data() + j * 36 + 32));
                for (size_t i = 0; i < per_block && blocks.size() < inode.blocks; ++i) blocks.push_back(Le<int32_t>(table.data() + i * 36 + 32));
            }
        }
        if (blocks.size() < inode.blocks) Fail(kDamagedMessage);
        return blocks;
    }

private:
    ReadFn raw_;
    uint16_t mode_ = 0;
    uint32_t block_size_ = 0;
    bool encrypted_ = false;
    mbedtls_aes_xts_context xts_{};
    std::vector<Inode> inodes_;
    std::vector<uint8_t> sector_buffer_;
};

// The bytes of a file in a PFS image, through its block list
class FileView {
public:
    FileView(Pfs& pfs, const Pfs::Inode& inode) : pfs_(pfs), size_(static_cast<uint64_t>(inode.size)), blocks_(pfs.Blocks(inode)) {}
    uint64_t Size() const { return size_; }
    void Read(uint64_t offset, uint8_t* out, size_t count) {
        if (offset + count > size_) Fail(kDamagedMessage);
        const uint64_t bs = pfs_.BlockSize();
        while (count > 0) {
            const uint64_t index = offset / bs;
            const uint64_t within = offset % bs;
            // runs of consecutive blocks are read in one go
            uint64_t run = 1;
            while (index + run < blocks_.size() && blocks_[index + run] == blocks_[index] + static_cast<int64_t>(run) && (run - 1) * bs + (bs - within) < count) ++run;
            const size_t take = static_cast<size_t>(std::min<uint64_t>(count, run * bs - within));
            pfs_.Read(static_cast<uint64_t>(blocks_[index]) * bs + within, out, take);
            out += take;
            offset += take;
            count -= take;
        }
    }

private:
    Pfs& pfs_;
    uint64_t size_;
    std::vector<int64_t> blocks_;
};

// A PFSC image: fixed-size blocks, each zlib-compressed unless that would not shrink it
class Pfsc {
public:
    explicit Pfsc(FileView& file) : file_(file) {
        uint8_t header[0x30];
        file_.Read(0, header, sizeof(header));
        if (Le<uint32_t>(header) != kPfscMagic) Fail(kDamagedMessage);
        block_size_ = Le<int32_t>(header + 0x0C);
        const int64_t block_size2 = Le<int64_t>(header + 0x10);
        const int64_t offsets = Le<int64_t>(header + 0x18);
        size_ = Le<int64_t>(header + 0x28);
        if (block_size_ <= 0 || block_size_ > (1 << 22) || block_size2 != block_size_ || size_ <= 0) Fail(kDamagedMessage);
        const uint64_t count = static_cast<uint64_t>(size_) / static_cast<uint64_t>(block_size_) + 1;
        if (offsets < 0 || static_cast<uint64_t>(offsets) + count * 8 > file_.Size()) Fail(kDamagedMessage);
        map_.resize(count);
        file_.Read(static_cast<uint64_t>(offsets), reinterpret_cast<uint8_t*>(map_.data()), count * 8);
        block_.resize(block_size_);
        packed_.resize(block_size_);
    }
    ~Pfsc() {
        if (inflating_) inflateEnd(&zs_);
    }
    uint64_t Size() const { return static_cast<uint64_t>(size_); }

    void Read(uint64_t offset, uint8_t* out, size_t count) {
        if (offset + count > Size()) Fail(kDamagedMessage);
        while (count > 0) {
            const uint64_t index = offset / block_size_;
            Load(index);
            const size_t within = static_cast<size_t>(offset % block_size_);
            const size_t take = std::min<size_t>(count, block_size_ - within);
            std::memcpy(out, block_.data() + within, take);
            out += take;
            offset += take;
            count -= take;
        }
    }

private:
    void Load(uint64_t index) {
        if (index == loaded_) return;
        if (index + 1 >= map_.size()) Fail(kDamagedMessage);
        const int64_t begin = map_[index];
        const int64_t size = map_[index + 1] - begin;
        if (begin < 0 || size < 0) Fail(kDamagedMessage);
        if (size == block_size_) {
            file_.Read(static_cast<uint64_t>(begin), block_.data(), block_.size());
        } else if (size > block_size_) {
            std::fill(block_.begin(), block_.end(), 0);  // a sparse block
        } else {
            // a zlib stream: 2 header bytes, then raw deflate
            if (size < 2) Fail(kDamagedMessage);
            packed_.resize(static_cast<size_t>(size));
            file_.Read(static_cast<uint64_t>(begin), packed_.data(), packed_.size());
            if (!inflating_) {
                std::memset(&zs_, 0, sizeof(zs_));
                if (inflateInit2(&zs_, -15) != Z_OK) Fail(kDamagedMessage);
                inflating_ = true;
            } else {
                inflateReset(&zs_);
            }
            zs_.next_in = packed_.data() + 2;
            zs_.avail_in = static_cast<uInt>(packed_.size() - 2);
            zs_.next_out = block_.data();
            zs_.avail_out = static_cast<uInt>(block_.size());
            const int status = inflate(&zs_, Z_FINISH);
            if (status != Z_STREAM_END && status != Z_OK && status != Z_BUF_ERROR) Fail(kDamagedMessage);
            std::fill(block_.begin() + (block_.size() - zs_.avail_out), block_.end(), 0);
        }
        loaded_ = index;
    }

    FileView& file_;
    int32_t block_size_ = 0;
    int64_t size_ = 0;
    std::vector<int64_t> map_;
    std::vector<uint8_t> block_, packed_;
    uint64_t loaded_ = UINT64_MAX;
    z_stream zs_{};
    bool inflating_ = false;
};

struct Found {
    std::string path;
    uint32_t inode = 0;
    bool at_root = false;
};

void Collect(Pfs& pfs, uint32_t dir, const std::string& prefix, bool root, std::vector<Found>& out, int depth) {
    if (depth > 16) return;
    for (const auto& entry : pfs.List(dir)) {
        if (entry.name == "." || entry.name == "..") continue;
        if (entry.type == 2) {
            out.push_back({prefix + entry.name, entry.inode, root});
        } else if (entry.type == 3 && entry.name != "sce_sys" && entry.name != "sce_module") {
            Collect(pfs, entry.inode, prefix + entry.name + "/", false, out, depth + 1);
        }
    }
}

std::string Lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

std::string BaseName(const std::string& path) {
    const auto slash = path.rfind('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

}  // namespace

Result ExtractPt(const Source& source, const std::filesystem::path& destination, const std::function<bool(const Progress&)>& progress) {
    Result result;
    std::vector<std::filesystem::path> partial;
    try {
        ReadFn raw = [&](uint64_t offset, uint8_t* buffer, size_t count) {
            if (offset > source.size || count > source.size - offset || !source.read(offset, buffer, count)) Fail(kDamagedMessage);
        };
        uint8_t header[0x1000];
        if (source.size < sizeof(header)) Fail(kNotPkgMessage);
        raw(0, header, sizeof(header));
        if (Be32(header) != kPkgMagic) Fail(kNotPkgMessage);
        const uint32_t entry_count = Be32(header + 0x10);
        const uint32_t table = Be32(header + 0x18);
        result.content_id.assign(reinterpret_cast<const char*>(header + 0x40), strnlen(reinterpret_cast<const char*>(header + 0x40), 0x24));
        result.title_id = result.content_id.size() >= 16 ? result.content_id.substr(7, 9) : "";
        const uint64_t pfs_flags = Be64(header + 0x408);
        const uint64_t pfs_offset = Be64(header + 0x410);
        const uint64_t pfs_size = Be64(header + 0x418);
        if (entry_count == 0 || entry_count > 10'000 || pfs_offset >= source.size || pfs_size > source.size - pfs_offset) Fail(kDamagedMessage);

        // the entry table: entry_keys (0x10) and image_key (0x20) with their 32-byte records
        std::vector<uint8_t> entries(static_cast<size_t>(entry_count) * 32);
        raw(table, entries.data(), entries.size());
        auto find_entry = [&](uint32_t id, std::vector<uint8_t>& data) -> const uint8_t* {
            for (uint32_t i = 0; i < entry_count; ++i) {
                const uint8_t* record = entries.data() + i * 32;
                if (Be32(record) != id) continue;
                const uint32_t offset = Be32(record + 16);
                const uint32_t size = Be32(record + 20);
                if (size > 0x10000) Fail(kDamagedMessage);
                data.resize(size);
                raw(offset, data.data(), size);
                return record;
            }
            Fail(kDamagedMessage);
        };
        std::vector<uint8_t> entry_keys, image_key;
        find_entry(0x10, entry_keys);
        const uint8_t* image_record = find_entry(0x20, image_key);
        if (entry_keys.size() < 0x20 + 7 * 0x20 + 4 * 0x100 || image_key.size() < 0x100) Fail(kRetailMessage);

        // EKPFS: slot 3 with the PKG-derived key 3, image_key through AES-CBC, then the fake keyset
        using namespace keys;
        const auto dk3 = RsaDecrypt(entry_keys.data() + 0x20 + 7 * 0x20 + 3 * 0x100, kDerivedKey3Modulus, kDerivedKey3PublicExponent,
                                    sizeof(kDerivedKey3PublicExponent), kDerivedKey3PrivateExponent, kDerivedKey3Prime1, kDerivedKey3Prime2);
        if (!dk3) Fail(kRetailMessage);
        std::vector<uint8_t> hash_input(image_record, image_record + 32);
        hash_input.insert(hash_input.end(), dk3->begin(), dk3->end());
        const auto iv_key = Sha256(hash_input.data(), hash_input.size());
        std::vector<uint8_t> image_plain(0x100);
        {
            mbedtls_aes_context aes;
            mbedtls_aes_init(&aes);
            uint8_t iv[16];
            std::memcpy(iv, iv_key.data(), 16);
            const bool ok = mbedtls_aes_setkey_dec(&aes, iv_key.data() + 16, 128) == 0 &&
                            mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, 0x100, iv, image_key.data(), image_plain.data()) == 0;
            mbedtls_aes_free(&aes);
            if (!ok) Fail(kDamagedMessage);
        }
        const auto ekpfs_bytes = RsaDecrypt(image_plain.data(), kFakeModulus, kFakePublicExponent, sizeof(kFakePublicExponent),
                                            kFakePrivateExponent, kFakePrime1, kFakePrime2);
        if (!ekpfs_bytes || ekpfs_bytes->size() != 32) Fail(kRetailMessage);
        std::array<uint8_t, 32> ekpfs{};
        std::memcpy(ekpfs.data(), ekpfs_bytes->data(), 32);

        // the outer PFS and its pfs_image.dat, the PFSC-compressed inner PFS
        Pfs outer([&](uint64_t offset, uint8_t* buffer, size_t count) { raw(pfs_offset + offset, buffer, count); }, ekpfs, pfs_flags);
        uint32_t outer_uroot = UINT32_MAX;
        for (const auto& e : outer.List(0))
            if (e.name == "uroot" && e.type == 3) outer_uroot = e.inode;
        if (outer_uroot == UINT32_MAX) Fail(kDamagedMessage);
        uint32_t image_inode = UINT32_MAX;
        for (const auto& e : outer.List(outer_uroot))
            if (e.name == "pfs_image.dat" && e.type == 2) image_inode = e.inode;
        if (image_inode == UINT32_MAX) Fail(kDamagedMessage);
        FileView image(outer, outer.InodeAt(image_inode));
        Pfsc pfsc(image);
        Pfs inner([&](uint64_t offset, uint8_t* buffer, size_t count) { pfsc.Read(offset, buffer, count); }, std::nullopt, 0);
        uint32_t inner_uroot = UINT32_MAX;
        for (const auto& e : inner.List(0))
            if (e.name == "uroot" && e.type == 3) inner_uroot = e.inode;
        if (inner_uroot == UINT32_MAX) Fail(kDamagedMessage);

        // the archives by content, as the desktop helper picks them: the expected name at the root first, else the largest
        // valid file of that kind anywhere in the tree
        std::vector<Found> files;
        Collect(inner, inner_uroot, "", true, files, 0);
        auto head = [&](const Found& f, int64_t from, size_t count) {
            FileView view(inner, inner.InodeAt(f.inode));
            std::vector<uint8_t> bytes;
            if (view.Size() < count) return bytes;
            bytes.resize(count);
            view.Read(from < 0 ? view.Size() - static_cast<uint64_t>(-from) : static_cast<uint64_t>(from), bytes.data(), count);
            return bytes;
        };
        auto choose = [&](const char* name, const char* kind, const std::function<bool(const Found&)>& valid) -> std::optional<Found> {
            for (const auto& f : files)
                if (f.at_root && BaseName(f.path) == name && valid(f)) return f;
            std::optional<Found> best;
            uint64_t best_size = 0;
            for (const auto& f : files) {
                const std::string lower = Lower(BaseName(f.path));
                if (lower.find(kind) == std::string::npos || !valid(f)) continue;
                const uint64_t size = static_cast<uint64_t>(inner.InodeAt(f.inode).size);
                if (!best || size > best_size) best = f, best_size = size;
            }
            if (best) result.warnings.push_back(std::string(name) + " not at the package root; using " + best->path);
            return best;
        };
        const auto psarc = choose("chunk1.psarc", ".psarc", [&](const Found& f) {
            const auto h = head(f, 0, 4);
            return h.size() == 4 && std::memcmp(h.data(), "PSAR", 4) == 0;
        });
        const auto qar = choose("texture.qar", ".qar", [&](const Found& f) {
            const auto h = head(f, -0x24, 0x24);
            return h.size() == 0x24 && h[0x16] == 'a' && h[0x17] == 'q';
        });
        const auto pathid = choose("pathid_list_ps4.bin", "pathid_list", [&](const Found& f) { return inner.InodeAt(f.inode).size >= 32; });
        if (!psarc || !qar) Fail(kNotPtMessage);
        std::vector<uint8_t> pathid_bytes;
        if (pathid && inner.InodeAt(pathid->inode).size < 64 * 1024 * 1024)
            pathid_bytes = head(*pathid, 0, static_cast<size_t>(inner.InodeAt(pathid->inode).size));
        const std::string pathid_text(pathid_bytes.begin(), pathid_bytes.end());
        if (pathid_text.find("pt14_") == std::string::npos && !kReleases.count(result.title_id)) Fail(kNotPtMessage);
        const auto release = kReleases.find(result.title_id);
        if (release == kReleases.end()) {
            result.warnings.push_back("content ID " + result.content_id + " is not a known P.T. release; installed because its archives are P.T.'s");
        } else if (result.title_id != "CUSA01127") {
            result.warnings.push_back(result.title_id + " is the " + release->second + " release; the port is tested with the US release CUSA01127");
        }

        // out: each archive to <name>.part, renamed into place once all three are written
        std::error_code ec;
        std::filesystem::create_directories(destination, ec);
        if (ec) Fail("Cannot create the game folder: " + ec.message());
        struct Job {
            const char* name;
            const Found* found;
        };
        std::vector<Job> jobs{{"chunk1.psarc", &*psarc}, {"texture.qar", &*qar}};
        if (pathid) jobs.push_back({"pathid_list_ps4.bin", &*pathid});
        Progress state;
        for (const auto& job : jobs) state.total += static_cast<uint64_t>(inner.InodeAt(job.found->inode).size);
        std::vector<uint8_t> buffer(4 << 20);
        for (const auto& job : jobs) {
            FileView view(inner, inner.InodeAt(job.found->inode));
            if (view.Size() > (8ull << 30)) Fail(kDamagedMessage);
            const auto target = destination / (std::string(job.name) + ".part");
            partial.push_back(target);
            std::ofstream out(target, std::ios::binary | std::ios::trunc);
            if (!out) Fail("Cannot write " + target.string() + ". Is there enough free space?");
            state.stage = std::string("Extracting ") + job.name;
            for (uint64_t pos = 0; pos < view.Size();) {
                const size_t count = static_cast<size_t>(std::min<uint64_t>(buffer.size(), view.Size() - pos));
                view.Read(pos, buffer.data(), count);
                out.write(reinterpret_cast<const char*>(buffer.data()), static_cast<std::streamsize>(count));
                if (!out) Fail("Writing " + std::string(job.name) + " failed. Is there enough free space?");
                pos += count;
                state.done += count;
                if (progress && !progress(state)) {
                    result.cancelled = true;
                    Fail("Cancelled.");
                }
            }
            out.close();
            if (!out) Fail("Writing " + std::string(job.name) + " failed. Is there enough free space?");
        }
        for (const auto& job : jobs) {
            const auto part = destination / (std::string(job.name) + ".part");
            std::filesystem::rename(part, destination / job.name, ec);
            if (ec) Fail("Cannot finish " + std::string(job.name) + ": " + ec.message());
        }
        partial.clear();
        std::ofstream(destination / "source.txt", std::ios::trunc)
            << "content_id=" << result.content_id << "\ntitle_id=" << result.title_id
            << "\nregion=" << (release == kReleases.end() ? "unknown" : release->second) << "\nsource=pkg (Android setup)\n";
        result.ok = true;
    } catch (const Failure& failure) {
        result.error = failure.message;
    } catch (const std::exception& e) {
        result.error = std::string("Extraction failed: ") + e.what();
    }
    for (const auto& path : partial) {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }
    return result;
}

}  // namespace pt::pkg
