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
        return fs.files.find(path) != fs.files.end();
    }
};

}  // namespace Os

#endif
