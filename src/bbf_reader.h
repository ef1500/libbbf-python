#pragma once
#include "libbbf.h"
#define XXH_INLINE_ALL
#include "xxhash.h"
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <future>
#include <thread>
#include <mutex>

// Platform specific includes for MMAP
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#endif

// Simple Memory Mapping Wrapper
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
        hMap = CreateFileMapping(hFile, NULL, PAGE_READONLY, 0, 0, NULL);
        if (!hMap) return false;
        data = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 0);
#else
        fd = open(path.c_str(), O_RDONLY);
        if (fd < 0) return false;
        struct stat st;
        fstat(fd, &st);
        size = st.st_size;
        data = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
#endif
        return data != nullptr;
    }

    ~MemoryMappedFile() {
#ifdef _WIN32
        if (data) UnmapViewOfFile(data);
        if (hMap) CloseHandle(hMap);
        if (hFile != INVALID_HANDLE_VALUE) CloseHandle(hFile);
#else
        if (data && data != MAP_FAILED) munmap(data, size);
        if (fd >= 0) close(fd);
#endif
    }
};

class BBFReader {
public:
    BBFFooter footer;
    BBFHeader header;
    MemoryMappedFile mmap;
    bool isValid = false;

    BBFReader(const std::string& path) {
        if (!mmap.map(path)) return;
        if (mmap.size < sizeof(BBFHeader) + sizeof(BBFFooter)) return;

        std::memcpy(&header, mmap.data, sizeof(BBFHeader));
        if (std::memcmp(header.magic, "BBF1", 4) != 0) return;

        std::memcpy(&footer, (uint8_t*)mmap.data + mmap.size - sizeof(BBFFooter), sizeof(BBFFooter));
        if (std::memcmp(footer.magic, "BBF1", 4) != 0) return;

        isValid = true;
    }

    std::string getString(uint32_t offset) const {
        const char* poolStart = (const char*)mmap.data + footer.stringPoolOffset;
        size_t poolSize = footer.assetTableOffset - footer.stringPoolOffset;
        if (offset >= poolSize) return "";
        return std::string(poolStart + offset);
    }

    struct PySection {
        std::string title;
        uint32_t startPage;
        uint32_t parent;
    };

    std::vector<PySection> getSections() {
        std::vector<PySection> result;
        if (!isValid) return result;
        const BBFSection* secs = reinterpret_cast<const BBFSection*>((const uint8_t*)mmap.data + footer.sectionTableOffset);
        for (uint32_t i = 0; i < footer.sectionCount; i++) {
            result.push_back({getString(secs[i].sectionTitleOffset), secs[i].sectionStartIndex, secs[i].parentSectionIndex});
        }
        return result;
    }

    std::vector<std::pair<std::string, std::string>> getMetadata() {
        std::vector<std::pair<std::string, std::string>> result;
        if (!isValid) return result;
        const BBFMetadata* meta = reinterpret_cast<const BBFMetadata*>((const uint8_t*)mmap.data + footer.metaTableOffset);
        for (uint32_t i = 0; i < footer.keyCount; i++) {
            result.push_back({getString(meta[i].keyOffset), getString(meta[i].valOffset)});
        }
        return result;
    }

    // Helper to get raw bytes for Python
    std::string getPageBytes(uint32_t pageIndex) {
        if (!isValid || pageIndex >= footer.pageCount) return "";
        
        const BBFPageEntry* pages = reinterpret_cast<const BBFPageEntry*>((const uint8_t*)mmap.data + footer.pageTableOffset);
        const BBFAssetEntry* assets = reinterpret_cast<const BBFAssetEntry*>((const uint8_t*)mmap.data + footer.assetTableOffset);
        
        const auto& asset = assets[pages[pageIndex].assetIndex];
        return std::string((const char*)mmap.data + asset.offset, asset.length);
    }

    std::map<std::string, uint64_t> getPageInfo(uint32_t pageIndex) {
        std::map<std::string, uint64_t> info;
        if (!isValid || pageIndex >= footer.pageCount) return info;

        const BBFPageEntry* pages = reinterpret_cast<const BBFPageEntry*>((const uint8_t*)mmap.data + footer.pageTableOffset);
        const BBFAssetEntry* assets = reinterpret_cast<const BBFAssetEntry*>((const uint8_t*)mmap.data + footer.assetTableOffset);
        const auto& asset = assets[pages[pageIndex].assetIndex];

        info["length"] = asset.length;
        info["offset"] = asset.offset;
        info["hash"] = asset.xxh3Hash;
        info["type"] = asset.type;
        return info;
    }

    // Implements verifyAssetsParallel from bbfenc.cpp
    bool verify() {
        if (!isValid) return false;
        
        // 1. Directory Hash Check
        size_t metaStart = footer.stringPoolOffset;
        size_t metaSize = mmap.size - sizeof(BBFFooter) - metaStart;
        uint64_t calcIndexHash = XXH3_64bits((const uint8_t*)mmap.data + metaStart, metaSize);
        
        if (calcIndexHash != footer.indexHash) return false;

        // 2. Asset Integrity Check
        const BBFAssetEntry* assets = reinterpret_cast<const BBFAssetEntry*>((const uint8_t*)mmap.data + footer.assetTableOffset);
        size_t count = footer.assetCount;

        auto verifyRange = [&](size_t start, size_t end) -> bool {
            for (size_t i = start; i < end; ++i) {
                const auto& a = assets[i];
                uint64_t h = XXH3_64bits((const uint8_t*)mmap.data + a.offset, a.length);
                if (h != a.xxh3Hash) return false;
            }
            return true;
        };

        // Determine thread count
        size_t numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 1;
        
        size_t chunkSize = count / numThreads;
        std::vector<std::future<bool>> futures;

        for (size_t i = 0; i < numThreads; ++i) {
            size_t start = i * chunkSize;
            size_t end = (i == numThreads - 1) ? count : start + chunkSize;
            futures.push_back(std::async(std::launch::async, verifyRange, start, end));
        }

        for (auto& f : futures) {
            if (!f.get()) return false;
        }
        return true;
    }
};