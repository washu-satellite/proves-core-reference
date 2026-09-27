// ======================================================================
// \title  Os/Mutex.hpp (host-test stub)
// \brief  No-op Os::Mutex / Os::ScopeLock stand-ins for host unit tests.
//
// Mirrors the shape ModeManager.cpp uses (lib/fprime/Os/Mutex.hpp:51,80):
// a default-constructible Mutex with lock()/unLock(), and a ScopeLock that
// takes a Mutex by reference. Host tests are single-threaded, so the lock is
// bookkeeping only; the depth counter lets a test assert on balanced use.
// ======================================================================

#ifndef UnitTestSupport_Os_Mutex_HPP
#define UnitTestSupport_Os_Mutex_HPP

#include "../FpTypesStub.hpp"

namespace Os {

class Mutex {
  public:
    Mutex() : m_depth(0) {}
    ~Mutex() {}
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock() { this->m_depth++; }
    void unLock() { this->m_depth--; }
    void unlock() { this->unLock(); }
    I32 depth() const { return this->m_depth; }

  private:
    I32 m_depth;
};

class ScopeLock {
  public:
    explicit ScopeLock(Mutex& mutex) : m_mutex(mutex) { this->m_mutex.lock(); }
    ~ScopeLock() { this->m_mutex.unLock(); }
    ScopeLock(const ScopeLock&) = delete;
    ScopeLock& operator=(const ScopeLock&) = delete;

  private:
    Mutex& m_mutex;
};

}  // namespace Os

#endif
