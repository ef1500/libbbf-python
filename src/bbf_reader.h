#pragma once
#include "libbbf.h"
#define XXH_INLINE_ALL
#include "xxhash.h"

#include <string>
#include <string_view> 
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

class BBFReader 
{
private:
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

    BBFReader(const std::string& path) 
    {
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

        // Safety: Ensure tables point inside the file
        if (footer.assetTableOffset >= mmap.size || footer.pageTableOffset >= mmap.size) return;

        sections_ = reinterpret_cast<const BBFSection*>(data_ptr + footer.sectionTableOffset);
        meta_     = reinterpret_cast<const BBFMetadata*>(data_ptr + footer.metaTableOffset);
        pages_    = reinterpret_cast<const BBFPageEntry*>(data_ptr + footer.pageTableOffset);
        assets_   = reinterpret_cast<const BBFAssetEntry*>(data_ptr + footer.assetTableOffset);
        
        stringPool_ = data_ptr + footer.stringPoolOffset;
        stringPoolSize_ = footer.assetTableOffset - footer.stringPoolOffset;

        isValid = true;
    }

    // Get string view
    std::string_view getStringView(uint32_t offset) const 
    {
        if (offset >= stringPoolSize_) return {};
        return std::string_view(stringPool_ + offset);
    }

    struct PySection 
    {
        std::string title;
        uint32_t startPage;
        uint32_t parent;
    };

    // Get sections
    std::vector<PySection> getSections() const 
    {
        std::vector<PySection> result;
        if (!isValid) return result;
        
        result.reserve(footer.sectionCount);
        for (uint32_t i = 0; i < footer.sectionCount; i++) 
        {
            result.push_back(
                {
                std::string(getStringView(sections_[i].sectionTitleOffset)), 
                sections_[i].sectionStartIndex, 
                sections_[i].parentSectionIndex
            });
        }
        return result;
    }

    // Get metadata
    std::vector<std::pair<std::string, std::string>> getMetadata() const 
    {
        std::vector<std::pair<std::string, std::string>> result;
        if (!isValid) return result;

        result.reserve(footer.keyCount);
        for (uint32_t i = 0; i < footer.keyCount; i++) 
        {
            result.emplace_back(
                getStringView(meta_[i].keyOffset), 
                getStringView(meta_[i].valOffset)
            );
        }
        return result;
    }

    // Get page raw
    std::pair<const char*, size_t> getPageRaw(uint32_t pageIndex) const 
    {
        if (!isValid || pageIndex >= footer.pageCount) return {nullptr, 0};
        
        const auto& asset = assets_[pages_[pageIndex].assetIndex];
        // Bounds check on asset
        if (asset.offset + asset.length > mmap.size) return {nullptr, 0};

        return { data_ptr + asset.offset, static_cast<size_t>(asset.length) };
    }

    // Get footer info
    std::map<std::string, uint64_t> getFooterInfo() const
    {
        // If invalid, return empty set.
        if (!isValid) return {};

        // Otherwise return all this juicy information!
        return 
        {
            {"stringPoolOffset", footer.stringPoolOffset},
            {"assetTableOffset", footer.assetTableOffset},
            {"assetCount", static_cast<uint64_t>(footer.assetCount)},

            {"pageTableOffset", footer.pageTableOffset},
            {"pageCount", static_cast<uint64_t>(footer.pageCount)},

            {"sectionTableOffset", footer.sectionTableOffset},
            {"sectionCount", footer.sectionCount},

            {"metaTableOffset", footer.metaTableOffset},
            {"keyCount", static_cast<uint64_t>(footer.keyCount)},

            {"extraOffset", footer.extraOffset},
            {"indexHash", footer.indexHash}
        };
    }

    // Get page info
    std::map<std::string, uint64_t> getPageInfo(uint32_t pageIndex) const 
    {
        if (!isValid || pageIndex >= footer.pageCount) return {};

        const auto& asset = assets_[pages_[pageIndex].assetIndex];
        return {
            {"length", asset.length},
            {"offset", asset.offset},
            {"hash", asset.xxh3Hash},
            {"type", asset.type},
            {"flags", asset.flags}, // Add flags
            {"decodedLength", asset.decodedLength} // ADDED: v1.1 Spec
        };
    }

    // verify a specific page
    std::map<uint64_t, bool> verifyPage(uint32_t pageIndex)
    {
        if (!isValid || pageIndex >= footer.pageCount) return {};
        // Get page
        const auto& asset = assets_[pages_[pageIndex].assetIndex];
        // Check mmap length
        if (asset.offset + asset.length > mmap.size) return {};
        // hash
        uint64_t xxhHash = XXH3_64bits((const uint8_t*)data_ptr + asset.offset, asset.length);

        bool match = (xxhHash == asset.xxh3Hash);
        return {{xxhHash, match}};
    }

    // Returns -1 for Success, -2 for Directory Error, or >=0 for Asset Index Error
    int64_t verify() const 
    {
        if (!isValid) return -2;
        
        // 1. Directory Hash Check
        size_t metaStart = footer.stringPoolOffset;
        size_t metaSize = mmap.size - sizeof(BBFFooter) - metaStart;
        if (XXH3_64bits(data_ptr + metaStart, metaSize) != footer.indexHash) return -2;

        // 2. Asset Integrity Check
        size_t count = footer.assetCount;
        const auto* local_assets = assets_;
        const auto* local_data = data_ptr;
        size_t max_size = mmap.size;

        // Lambda returns -1 if OK, or the index if Bad
        auto verifyRange = [local_assets, local_data, max_size](size_t start, size_t end) -> int64_t 
        {
            for (size_t i = start; i < end; ++i) 
            {
                const auto& a = local_assets[i];
                // Bounds check before hash
                if (a.offset + a.length > max_size) return (int64_t)i;
                
                if (XXH3_64bits((const uint8_t*)local_data + a.offset, a.length) != a.xxh3Hash) 
                {
                    return (int64_t)i; // Return the corrupted index
                }
            }
            return -1; // Success
        };

        size_t numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 1;

        if (count < 128 || numThreads == 1) 
        {
            return verifyRange(0, count);
        }

        size_t chunkSize = count / numThreads;
        std::vector<std::future<int64_t>> futures; // Changed from bool to int64_t
        futures.reserve(numThreads);

        for (size_t i = 0; i < numThreads; ++i) 
        {
            size_t start = i * chunkSize;
            size_t end = (i == numThreads - 1) ? count : start + chunkSize;
            futures.push_back(std::async(std::launch::async, verifyRange, start, end));
        }

        // Check results
        for (auto& f : futures) 
        {
            int64_t result = f.get();
            if (result != -1) return result; // Bubble up the error index
        }
        return -1; // All good
    }
};