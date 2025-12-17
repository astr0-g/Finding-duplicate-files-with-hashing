#include <iostream>
#include <filesystem>
#include <unordered_map>
#include <vector>
#include <string>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <functional>

namespace fs = std::filesystem;

// ==============================================
// Data Structures
// ==============================================
using FileSize = uintmax_t;                     // File size
using HashString = std::string;                 // Hash string
using PathList = std::vector<fs::path>;         // File path list

using SizeGroupMap = std::unordered_map<FileSize, PathList>;   // Phase 1
using HashGroupMap = std::unordered_map<HashString, PathList>; // Phase 2

constexpr size_t PARTIAL_READ_SIZE = 4096;  // 4KB for partial hash

// ==============================================
// Hash Computation Functions
// ==============================================
HashString compute_hash(const fs::path& filepath, size_t max_bytes = 0) {
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        return "";
    }

    size_t hash1 = 0;
    size_t hash2 = 0;
    
    char buffer[8192];
    size_t total_read = 0;
    std::hash<std::string> hasher;

    while (file) {
        size_t to_read = sizeof(buffer);
        if (max_bytes > 0) {
            to_read = std::min(to_read, max_bytes - total_read);
        }

        file.read(buffer, to_read);
        auto bytes_read = file.gcount();

        if (bytes_read > 0) {
            std::string chunk(buffer, bytes_read);
            hash1 ^= hasher(chunk) + 0x9e3779b9 + (hash1 << 6) + (hash1 >> 2);
            hash2 ^= hasher(chunk) * 31 + hash2;
            total_read += bytes_read;
        }

        if (max_bytes > 0 && total_read >= max_bytes) {
            break;
        }
    }

    std::ostringstream oss;
    oss << std::hex << std::setfill('0') 
        << std::setw(16) << hash1 
        << std::setw(16) << hash2;
    return oss.str();
}

// Partial hash: only read first 4KB
HashString partial_hash(const fs::path& filepath) {
    return compute_hash(filepath, PARTIAL_READ_SIZE);
}

// Full hash: read entire file
HashString full_hash(const fs::path& filepath) {
    return compute_hash(filepath);  // max_bytes = 0, read all
}

// ==============================================
// Phase 1: Group by File Size (Filter by Size)
// ==============================================
SizeGroupMap group_by_size(const fs::path& directory) {
    SizeGroupMap size_groups;

    for (const auto& entry : fs::recursive_directory_iterator(directory)) {
        if (entry.is_regular_file()) {
            FileSize size = entry.file_size();
            size_groups[size].push_back(entry.path());  // O(1) average insertion time
        }
    }

    // Remove groups with only 1 file
    for (auto it = size_groups.begin(); it != size_groups.end(); ) {
        if (it->second.size() < 2) {
            it = size_groups.erase(it);
        } else {
            ++it;
        }
    }

    return size_groups;
}

// ==============================================
// Phase 2: Two-Step Hashing (Two-Step Hashing)
// ==============================================
// Step A - Partial Hash
// Step B - Full Hash
// ==============================================

std::vector<PathList> find_duplicates(const fs::path& directory) {
    std::vector<PathList> duplicate_groups;

    // ==================
    // Phase 1: Group by size
    // ==================
    std::cout << "Phase 1: Grouping files by size...\n";
    SizeGroupMap size_groups = group_by_size(directory);
    std::cout << "  Found " << size_groups.size() << " size groups with potential duplicates\n";

    // ==================
    // Phase 2: Two-step hashing
    // ==================
    std::cout << "Phase 2: Computing hashes...\n";

    for (const auto& [size, paths] : size_groups) {

        // ------------------
        // Step A: Partial hash
        // ------------------
        HashGroupMap partial_groups;

        for (const auto& path : paths) {
            HashString hash = partial_hash(path);
            if (!hash.empty()) {
                partial_groups[hash].push_back(path);
            }
        }

        // ------------------
        // Step B: Full hash
        // ------------------
        for (const auto& [phash, candidates] : partial_groups) {
            // If partial hash group has only 1 file, skip
            if (candidates.size() < 2) {
                continue;
            }

            HashGroupMap full_groups;

            for (const auto& path : candidates) {
                HashString hash = full_hash(path);
                if (!hash.empty()) {
                    full_groups[hash].push_back(path);
                }
            }

            // Collect confirmed duplicate files
            for (auto& [fhash, dupes] : full_groups) {
                if (dupes.size() >= 2) {
                    duplicate_groups.push_back(std::move(dupes));
                }
            }
        }
    }

    return duplicate_groups;
}

// ==============================================
// Helper Function: Format file size
// ==============================================
std::string format_size(uintmax_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit_index = 0;
    double size = static_cast<double>(bytes);

    while (size >= 1024 && unit_index < 4) {
        size /= 1024;
        unit_index++;
    }

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << size << " " << units[unit_index];
    return oss.str();
}

int main() {
    std::string dir_path;
    std::cout << "Enter directory path: ";
    std::getline(std::cin, dir_path);
    
    if (dir_path.empty()) {
        std::cerr << "Error: No directory provided\n";
        return 1;
    }

    if (dir_path.size() >= 2) {
        char first = dir_path.front();
        char last = dir_path.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            dir_path = dir_path.substr(1, dir_path.size() - 2);
        }
    }

    fs::path directory = dir_path;

    if (!fs::exists(directory)) {
        std::cerr << "Error: Directory does not exist: " << directory << "\n";
        return 1;
    }

    if (!fs::is_directory(directory)) {
        std::cerr << "Error: Path is not a directory: " << directory << "\n";
        return 1;
    }

    std::cout << "========================================\n";
    std::cout << "Duplicate File Finder\n";
    std::cout << "========================================\n";
    std::cout << "Scanning directory: " << directory << "\n\n";

    // Find duplicate files
    auto duplicates = find_duplicates(directory);

    // Output results
    std::cout << "\n========================================\n";
    std::cout << "Results: Found " << duplicates.size() << " duplicate groups\n";
    std::cout << "========================================\n\n";

    uintmax_t total_wasted = 0;

    for (size_t i = 0; i < duplicates.size(); ++i) {
        const auto& group = duplicates[i];
        uintmax_t file_size = fs::file_size(group[0]);
        uintmax_t wasted = file_size * (group.size() - 1);
        total_wasted += wasted;

        std::cout << "Group " << (i + 1) << ": " 
                  << group.size() << " files, "
                  << format_size(file_size) << " each, "
                  << "wasted: " << format_size(wasted) << "\n";

        for (const auto& path : group) {
            std::cout << "  - " << path << "\n";
        }
        std::cout << "\n";
    }

    std::cout << "========================================\n";
    std::cout << "Total wasted space: " << format_size(total_wasted) << "\n";
    std::cout << "========================================\n";

    return 0;
}