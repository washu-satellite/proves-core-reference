// ======================================================================
// \title  Os/FileSystem.hpp (host-test stub)
// \brief  In-memory Os::FileSystem stand-in for host tests.
//
// Mirrors the subset of lib/fprime/Os/FileSystem.hpp used by components under
// test: the Status enum, static rename() and static exists(). Backed by the
// same process-wide fake filesystem as Os/File.hpp, so files created through
// Os::File are visible here and vice versa.
//
// rename() deliberately mirrors Zephyr's fatfs_rename, which unlinks the
// destination before moving the entry (so the replace is NOT atomic on the
// target); tests that care about the resulting window drive it explicitly.
// ======================================================================

#ifndef UnitTestSupport_Os_FileSystem_HPP
#define UnitTestSupport_Os_FileSystem_HPP

#include <map>
#include <string>
#include <vector>

#include "File.hpp"

namespace Os {

class FileSystem {
  public:
    enum Status {
        OP_OK,
        ALREADY_EXISTS,
        NO_SPACE,
        NO_PERMISSION,
        NOT_DIR,
        IS_DIR,
        NOT_EMPTY,
        INVALID_PATH,
        DOESNT_EXIST,
        FILE_LIMIT,
        BUSY,
        NO_MORE_FILES,
        BUFFER_TOO_SMALL,
        EXDEV_ERROR,
        OVERFLOW_ERROR,
        NOT_SUPPORTED,
        OTHER_ERROR
    };

    //! Move sourcePath onto destPath, replacing destPath if it exists.
    static Status rename(const char* sourcePath, const char* destPath) {
        Test::FileSystemState& fs = Test::fileSystem();
        Test::recordOperation("rename");
        if (fs.failRename) {
            // Injected failure: neither path is touched.
            return OTHER_ERROR;
        }
        const std::map<std::string, std::vector<U8> >::iterator source = fs.files.find(sourcePath);
        if (source == fs.files.end()) {
            return DOESNT_EXIST;
        }
        if (std::string(sourcePath) == std::string(destPath)) {
            return OP_OK;
        }
        const std::vector<U8> content = source->second;
        // Unlink the destination first, as fatfs_rename does, then move.
        fs.files.erase(destPath);
        fs.files[destPath] = content;
        fs.files.erase(sourcePath);
        return OP_OK;
    }

    //! True when the path names an existing file (stat on the target).
    static bool exists(const char* path) {
        Test::FileSystemState& fs = Test::fileSystem();
        Test::recordOperation("exists");
        return fs.files.find(path) != fs.files.end();
    }

    // ---- Cycle M (DataRecorder) additions, 01-normative.md section 9 ----

    //! Remove a file: DOESNT_EXIST when absent; failRemove injects OTHER_ERROR.
    static Status removeFile(const char* path) {
        Test::recordOperation("removeFile");
        Test::FileSystemState& fs = Test::fileSystem();
        if (fs.failRemove) {
            return OTHER_ERROR;
        }
        if (fs.files.erase(path) == 0) {
            return DOESNT_EXIST;
        }
        return OP_OK;
    }

    //! Size of a file in bytes; DOESNT_EXIST when absent.
    static Status getFileSize(const char* path, FwSizeType& size) {
        Test::recordOperation("getFileSize");
        Test::FileSystemState& fs = Test::fileSystem();
        const std::map<std::string, std::vector<U8> >::const_iterator it = fs.files.find(path);
        if (it == fs.files.end()) {
            return DOESNT_EXIST;
        }
        size = static_cast<FwSizeType>(it->second.size());
        return OP_OK;
    }

    //! totalBytes from the fake; free = total - sum of file sizes (floored at 0).
    static Status getFreeSpace(const char* path, FwSizeType& totalBytes, FwSizeType& freeBytes) {
        (void)path;
        Test::recordOperation("getFreeSpace");
        Test::FileSystemState& fs = Test::fileSystem();
        if (fs.failGetFreeSpace) {
            return OTHER_ERROR;
        }
        FwSizeType used = 0;
        for (std::map<std::string, std::vector<U8> >::const_iterator it = fs.files.begin(); it != fs.files.end();
             ++it) {
            used += static_cast<FwSizeType>(it->second.size());
        }
        totalBytes = fs.totalBytes;
        freeBytes = (used < fs.totalBytes) ? (fs.totalBytes - used) : 0;
        return OP_OK;
    }

    //! Create a directory. The parent must be "/" or an existing directory
    //! (else DOESNT_EXIST); an existing directory is OP_OK unless
    //! errorIfAlreadyExists (ALREADY_EXISTS); failCreateDirectory injects OTHER_ERROR.
    static Status createDirectory(const char* path, bool errorIfAlreadyExists = false) {
        Test::recordOperation("createDirectory");
        Test::FileSystemState& fs = Test::fileSystem();
        if (fs.failCreateDirectory) {
            return OTHER_ERROR;
        }
        const std::string p(path);
        if (fs.directories.count(p) != 0) {
            return errorIfAlreadyExists ? ALREADY_EXISTS : OP_OK;
        }
        const std::string::size_type slash = p.find_last_of('/');
        const std::string parent = (slash == 0 || slash == std::string::npos) ? std::string("/") : p.substr(0, slash);
        if (parent != "/" && fs.directories.count(parent) == 0) {
            return DOESNT_EXIST;
        }
        fs.directories.insert(p);
        return OP_OK;
    }
};

}  // namespace Os

#endif
