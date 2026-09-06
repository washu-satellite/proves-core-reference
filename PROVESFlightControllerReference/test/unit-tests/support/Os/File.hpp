// ======================================================================
// \title  Os/File.hpp (host-test stub)
// \brief  In-memory Os::File stand-in with fault injection for host tests.
//
// Mirrors the subset of lib/fprime/Os/File.hpp used by components under test:
// open(path, mode), open(path, mode, overwrite), read/write with by-reference
// size and WaitType, and close(). Backed by a process-wide in-memory
// filesystem controlled through Os::Test::fileSystem() / resetFileSystem().
// ======================================================================

#ifndef UnitTestSupport_Os_File_HPP
#define UnitTestSupport_Os_File_HPP

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "../FpTypesStub.hpp"

namespace Os {

namespace Test {

//! Process-wide fake filesystem state, inspectable and adjustable by tests.
struct FileSystemState {
    std::map<std::string, std::vector<U8>> files;
    bool failOpenCreate = false;  //!< open(OPEN_CREATE) returns OTHER_ERROR
    bool failWrite = false;       //!< write() returns OTHER_ERROR
    bool partialWrite = false;    //!< write() succeeds but reports size - 1 bytes
    bool failFlush = false;       //!< flush() returns OTHER_ERROR
    bool failRename = false;      //!< FileSystem::rename() returns OTHER_ERROR and changes nothing
};

inline FileSystemState& fileSystem() {
    static FileSystemState state;
    return state;
}

inline void resetFileSystem() {
    fileSystem() = FileSystemState();
}

}  // namespace Test

class File {
  public:
    enum Mode { OPEN_NO_MODE, OPEN_READ, OPEN_CREATE, OPEN_WRITE, OPEN_SYNC_WRITE, OPEN_APPEND };
    enum OverwriteType { NO_OVERWRITE, OVERWRITE };
    enum SeekType { RELATIVE, ABSOLUTE };
    enum WaitType { NO_WAIT, WAIT };
    enum Status {
        OP_OK,
        DOESNT_EXIST,
        NO_SPACE,
        NO_PERMISSION,
        BAD_SIZE,
        NOT_OPENED,
        FILE_EXISTS,
        NOT_SUPPORTED,
        INVALID_MODE,
        DELAY,
        INVALID_ARGUMENT,
        OTHER_ERROR
    };

    File() : m_open(false), m_mode(OPEN_NO_MODE), m_position(0) {}
    ~File() { this->close(); }

    Status open(const char* path, Mode mode) { return this->open(path, mode, NO_OVERWRITE); }

    Status open(const char* path, Mode mode, OverwriteType overwrite) {
        (void)overwrite;
        Test::FileSystemState& fs = Test::fileSystem();
        if (mode == OPEN_READ) {
            if (fs.files.find(path) == fs.files.end()) {
                return DOESNT_EXIST;
            }
        } else if (mode == OPEN_CREATE) {
            if (fs.failOpenCreate) {
                return OTHER_ERROR;
            }
            fs.files[path].clear();
        } else {
            return INVALID_MODE;
        }
        this->m_path = path;
        this->m_mode = mode;
        this->m_open = true;
        this->m_position = 0;
        return OP_OK;
    }

    void close() {
        this->m_open = false;
        this->m_mode = OPEN_NO_MODE;
        this->m_position = 0;
    }

    Status read(U8* buffer, FwSizeType& size, WaitType wait) {
        (void)wait;
        if (!this->m_open || this->m_mode != OPEN_READ) {
            size = 0;
            return NOT_OPENED;
        }
        const std::vector<U8>& content = Test::fileSystem().files[this->m_path];
        FwSizeType available = 0;
        if (this->m_position < content.size()) {
            available = static_cast<FwSizeType>(content.size() - this->m_position);
        }
        const FwSizeType count = std::min(size, available);
        if (count > 0) {
            std::memcpy(buffer, content.data() + this->m_position, static_cast<size_t>(count));
        }
        this->m_position += static_cast<size_t>(count);
        size = count;
        return OP_OK;
    }

    //! Mirrors Os::File::flush(): fs_sync on the target, a no-op here beyond
    //! fault injection (writes already land in the in-memory content).
    Status flush() {
        if (!this->m_open) {
            return NOT_OPENED;
        }
        if (Test::fileSystem().failFlush) {
            return OTHER_ERROR;
        }
        return OP_OK;
    }

    Status write(const U8* buffer, FwSizeType& size, WaitType wait) {
        (void)wait;
        Test::FileSystemState& fs = Test::fileSystem();
        if (!this->m_open || this->m_mode != OPEN_CREATE) {
            size = 0;
            return NOT_OPENED;
        }
        if (fs.failWrite) {
            size = 0;
            return OTHER_ERROR;
        }
        FwSizeType count = size;
        if (fs.partialWrite && count > 0) {
            count -= 1;
        }
        std::vector<U8>& content = fs.files[this->m_path];
        content.insert(content.end(), buffer, buffer + static_cast<size_t>(count));
        size = count;
        return OP_OK;
    }

  private:
    bool m_open;
    Mode m_mode;
    std::string m_path;
    size_t m_position;
};

}  // namespace Os

#endif
