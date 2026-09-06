// ======================================================================
// \title  Os/Mutex.hpp (host-test stub)
// \brief  Single-threaded stand-in for Os::Mutex / Os::ScopeLock.
//
// Mirrors the subset of lib/fprime/Os/Mutex.hpp that components under test
// use: default construction, lock()/unLock(), and the RAII ScopeLock. Host
// tests drive components from one thread, so the operations are no-ops; the
// type only has to exist so the real component source compiles and the
// lock/unlock pairing is preserved for readers.
// ======================================================================

#ifndef UnitTestSupport_Os_Mutex_HPP
#define UnitTestSupport_Os_Mutex_HPP

namespace Os {

//! Host stand-in for the F Prime mutex; no contention exists on the host.
class Mutex final {
  public:
    Mutex() = default;
    ~Mutex() = default;
    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;

    void lock() {}    //!< No-op: host tests are single-threaded.
    void unLock() {}  //!< No-op: host tests are single-threaded.
};

//! RAII wrapper matching Os::ScopeLock's shape.
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
