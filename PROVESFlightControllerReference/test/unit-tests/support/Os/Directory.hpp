// ======================================================================
// \title  Os/Directory.hpp (host-test stub)
// \brief  In-memory Os::Directory stand-in for host tests (Cycle M).
//
// Mirrors the subset of lib/fprime/Os/Directory.hpp the DataRecorder uses:
// the Status and OpenMode enums (Directory.hpp:21-40), open(path, mode),
// isOpen(), rewind(), read(buffer, size) and close(). Backed by the same
// process-wide fake filesystem as Os/File.hpp (Os::Test::fileSystem()).
//
// Observable behaviour (cycle-m-plan/01-normative.md section 9):
//   * open(path, READ) returns DOESNT_EXIST unless path is in `directories`;
//     CREATE_IF_MISSING adds it; CREATE_EXCLUSIVE adds it or ALREADY_EXISTS.
//   * The listing is taken at open: the base name of every file in `files`
//     and every directory in `directories` whose path is <path>/<name> with
//     no further '/', in lexicographic order (reversed when
//     reverseDirectoryOrder). read() yields one per call, then NO_MORE_FILES;
//     NOT_OPENED when closed.
//   * rewind() returns NOT_SUPPORTED, as ZephyrDirectory does
//     (fprime-zephyr/Os/Directory.cpp:47-49).
//   * Injections: failDirectoryOpen (open returns OTHER_ERROR) and
//     dirReadFailAt (the 0-based read() call after an open that returns
//     OTHER_ERROR; -1 off).
//   * open counts "dirOpen" and read counts "dirRead" in opCounts. An open
//     directory is not a file handle and does not count in openHandles.
// ======================================================================

#ifndef UnitTestSupport_Os_Directory_HPP
#define UnitTestSupport_Os_Directory_HPP

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include "File.hpp"

namespace Os {

class Directory {
  public:
    enum Status {
        OP_OK,
        DOESNT_EXIST,
        NO_PERMISSION,
        NOT_OPENED,
        NOT_DIR,
        NO_MORE_FILES,
        FILE_LIMIT,
        BAD_DESCRIPTOR,
        ALREADY_EXISTS,
        NOT_SUPPORTED,
        OTHER_ERROR,
    };

    enum OpenMode { READ, CREATE_IF_MISSING, CREATE_EXCLUSIVE, MAX_OPEN_MODE };

    Directory() : m_open(false), m_index(0), m_reads(0) {}
    ~Directory() { this->close(); }
    Directory(const Directory&) = delete;
    Directory& operator=(const Directory&) = delete;

    Status open(const char* path, OpenMode mode) {
        Test::recordOperation("dirOpen");
        Test::FileSystemState& fs = Test::fileSystem();
        this->close();
        if (fs.failDirectoryOpen) {
            return OTHER_ERROR;
        }
        const std::string dir(path);
        const bool present = (dir == "/") || (fs.directories.count(dir) != 0);
        if (!present) {
            if (mode == READ) {
                return DOESNT_EXIST;
            }
            fs.directories.insert(dir);
        } else if (mode == CREATE_EXCLUSIVE) {
            return ALREADY_EXISTS;
        }
        const std::string prefix = (dir == "/") ? dir : dir + "/";
        std::vector<std::string> names;
        for (std::map<std::string, std::vector<U8> >::const_iterator it = fs.files.begin(); it != fs.files.end();
             ++it) {
            addChild(prefix, it->first, names);
        }
        for (std::set<std::string>::const_iterator it = fs.directories.begin(); it != fs.directories.end(); ++it) {
            addChild(prefix, *it, names);
        }
        std::sort(names.begin(), names.end());
        if (fs.reverseDirectoryOrder) {
            std::reverse(names.begin(), names.end());
        }
        this->m_entries = names;
        this->m_index = 0;
        this->m_reads = 0;
        this->m_open = true;
        return OP_OK;
    }

    bool isOpen() { return this->m_open; }

    Status rewind() { return NOT_SUPPORTED; }

    Status read(char* fileNameBuffer, FwSizeType buffSize) {
        Test::recordOperation("dirRead");
        if (!this->m_open) {
            return NOT_OPENED;
        }
        const I32 call = this->m_reads;
        this->m_reads++;
        if (Test::fileSystem().dirReadFailAt >= 0 && call == Test::fileSystem().dirReadFailAt) {
            return OTHER_ERROR;
        }
        if (this->m_index >= this->m_entries.size()) {
            return NO_MORE_FILES;
        }
        const std::string& name = this->m_entries[this->m_index];
        this->m_index++;
        if (fileNameBuffer != nullptr && buffSize > 0) {
            const size_t n = std::min(static_cast<size_t>(buffSize - 1), name.size());
            std::memcpy(fileNameBuffer, name.data(), n);
            fileNameBuffer[n] = '\0';
        }
        return OP_OK;
    }

    void close() {
        this->m_open = false;
        this->m_entries.clear();
        this->m_index = 0;
        this->m_reads = 0;
    }

  private:
    static void addChild(const std::string& prefix, const std::string& path, std::vector<std::string>& names) {
        if (path.size() <= prefix.size() || path.compare(0, prefix.size(), prefix) != 0) {
            return;
        }
        const std::string rest = path.substr(prefix.size());
        if (rest.find('/') != std::string::npos) {
            return;
        }
        names.push_back(rest);
    }

    bool m_open;
    std::vector<std::string> m_entries;
    size_t m_index;
    I32 m_reads;
};

}  // namespace Os

#endif
