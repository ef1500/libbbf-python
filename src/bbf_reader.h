#pragma once
#include "libbbf.h"
#define XXH_INLINE_ALL
#include "xxhash.h"

#include <string>
#include <string_view> // C++17: Essential for zero-copy parsing
#include <vector>
#include <map>
#include <cstring>
#include <future>
#include <thread>
#include <algorithm>

// Platform specific includes for MMAP
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

struct MemoryMappedFile {
    void* data = nullptr;
    size_t size = 0;
#ifdef _WIN32
    HANDLE hFile = INVALID_HANDLE_VALUE;
    HANDLE hMap = NULL;
#else
    int fd = -1;
#endif

    bool map(const std::string& path) {
#ifdef _WIN32
        hFile = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER li;
        GetFileSizeEx(hFile, &li);
        size = (size_t)li.QuadPart;
        if (size == 0) { CloseHandle(hFile); return false; } 
        hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMap) { CloseHandle(hFile); return false; }
        data = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
#else
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;
        struct stat st;
        if (fstat(fd, &st) < 0) { close(fd); return false; }
        size = st.st_size;
        if (size == 0) { close(fd); return false; }
        data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (data == MAP_FAILED) { data = nullptr; close(fd); return false; }
#endif
        return data != nullptr;
    }

    void unmap() {
        if (!data) return;
#ifdef _WIN32
        UnmapViewOfFile(data);
        if (hMap) CloseHandle(hMap);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
        hMap = NULL; hFile = INVALID_HANDLE_VALUE;
#else
        munmap(data, size);
        if (fd >= 0) close(fd);
        fd = -1;
#endif
        data = nullptr;
        size = 0;
    }

    ~MemoryMappedFile() { unmap(); }
};

class BBFReader {
private:
    // Cached pointers to avoid recalculating offsets repeatedly
    const char* data_ptr = nullptr;
    const BBFSection* sections_ = nullptr;
    const BBFMetadata* meta_ = nullptr;
    const BBFPageEntry* pages_ = nullptr;
    const BBFAssetEntry* assets_ = nullptr;
    const char* stringPool_ = nullptr;
    size_t stringPoolSize_ = 0;

public:
    BBFFooter footer;
    BBFHeader header;
    MemoryMappedFile mmap;
    bool isValid = false;

    BBFReader(const std::string& path) {
        if (!mmap.map(path)) return;
        data_ptr = static_cast<const char*>(mmap.data);

        // Basic Size Check
        if (mmap.size < sizeof(BBFHeader) + sizeof(BBFFooter)) return;

        // Read Header
        std::memcpy(&header, data_ptr, sizeof(BBFHeader));
        if (std::memcmp(header.magic, "BBF1", 4) != 0) return;

        // Read Footer
        std::memcpy(&footer, data_ptr + mmap.size - sizeof(BBFFooter), sizeof(BBFFooter));
        if (std::memcmp(footer.magic, "BBF1", 4) != 0) return;

        // Cache Table Pointers
        // Note: In production, you should add bounds checks here to ensure offsets are within mmap.size
        sections_ = reinterpret_cast<const BBFSection*>(data_ptr + footer.sectionTableOffset);
        meta_     = reinterpret_cast<const BBFMetadata*>(data_ptr + footer.metaTableOffset);
        pages_    = reinterpret_cast<const BBFPageEntry*>(data_ptr + footer.pageTableOffset);
        assets_   = reinterpret_cast<const BBFAssetEntry*>(data_ptr + footer.assetTableOffset);
        
        stringPool_ = data_ptr + footer.stringPoolOffset;
        stringPoolSize_ = footer.assetTableOffset - footer.stringPoolOffset;

        isValid = true;
    }

    // Optimized: Returns string_view (no allocation)
    // Helper to allow returning std::string for legacy binding support if needed, 
    // but internal logic should prefer views.
    std::string_view getStringView(uint32_t offset) const {
        if (offset >= stringPoolSize_) return {};
        // Requires strings in file to be null-terminated.
        return std::string_view(stringPool_ + offset);
    }

    struct PySection {
        std::string title;
        uint32_t startPage;
        uint32_t parent;
    };

    std::vector<PySection> getSections() const {
        std::vector<PySection> result;
        if (!isValid) return result;
        
        result.reserve(footer.sectionCount); // Optimization: Reserve memory
        for (uint32_t i = 0; i < footer.sectionCount; i++) {
            // Explicit conversion to std::string here is okay as we are handing off to Python
            result.push_back({
                std::string(getStringView(sections_[i].sectionTitleOffset)), 
                sections_[i].sectionStartIndex, 
                sections_[i].parentSectionIndex
            });
        }
        return result;
    }

    std::vector<std::pair<std::string, std::string>> getMetadata() const {
        std::vector<std::pair<std::string, std::string>> result;
        if (!isValid) return result;

        result.reserve(footer.keyCount);
        for (uint32_t i = 0; i < footer.keyCount; i++) {
            result.emplace_back(
                getStringView(meta_[i].keyOffset), 
                getStringView(meta_[i].valOffset)
            );
        }
        return result;
    }

    // Zero-copy accessor for PyBind
    // Returns {pointer, size}
    std::pair<const char*, size_t> getPageRaw(uint32_t pageIndex) const {
        if (!isValid || pageIndex >= footer.pageCount) return {nullptr, 0};
        
        // Indirect addressing: Page -> Asset -> Offset/Length
        const auto& asset = assets_[pages_[pageIndex].assetIndex];
        return { data_ptr + asset.offset, asset.length };
    }

    // Legacy support (copies data)
    std::string getPageBytes(uint32_t pageIndex) const {
        auto raw = getPageRaw(pageIndex);
        if (!raw.first) return "";
        return std::string(raw.first, raw.second);
    }

    std::map<std::string, uint64_t> getPageInfo(uint32_t pageIndex) const {
        if (!isValid || pageIndex >= footer.pageCount) return {};

        const auto& asset = assets_[pages_[pageIndex].assetIndex];
        return {
            {"length", asset.length},
            {"offset", asset.offset},
            {"hash", asset.xxh3Hash},
            {"type", asset.type}
        };
    }

    bool verify() const {
        if (!isValid) return false;
        
        // 1. Directory Hash Check
        size_t metaStart = footer.stringPoolOffset;
        size_t metaSize = mmap.size - sizeof(BBFFooter) - metaStart;
        if (XXH3_64bits(data_ptr + metaStart, metaSize) != footer.indexHash) return false;

        // 2. Asset Integrity Check
        size_t count = footer.assetCount;
        const auto* local_assets = assets_; // Copy pointer for lambda capture
        const auto* local_data = data_ptr;

        auto verifyRange = [local_assets, local_data](size_t start, size_t end) -> bool {
            for (size_t i = start; i < end; ++i) {
                const auto& a = local_assets[i];
                if (XXH3_64bits((const uint8_t*)local_data + a.offset, a.length) != a.xxh3Hash) {
                    return false;
                }
            }
            return true;
        };

        // Optimization: Don't spawn threads for small files
        size_t numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 1;

        // Heuristic: If assets < 128, threading overhead > hashing gain
        if (count < 128 || numThreads == 1) {
            return verifyRange(0, count);
        }

        size_t chunkSize = count / numThreads;
        std::vector<std::future<bool>> futures;
        futures.reserve(numThreads);

        for (size_t i = 0; i < numThreads; ++i) {
            size_t start = i * chunkSize;
            size_t end = (i == numThreads - 1) ? count : start + chunkSize;
            // Launch async
            futures.push_back(std::async(std::launch::async, verifyRange, start, end));
        }

        for (auto& f : futures) {
            if (!f.get()) return false;
        }
        return true;
    }
};