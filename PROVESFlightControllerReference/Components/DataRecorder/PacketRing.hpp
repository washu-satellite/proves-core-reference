// ======================================================================
// \title  PacketRing.hpp
// \brief  F'-free fixed-size ring of variable-length packets (DataRecorder).
//
// SLOTS slots of SLOT_BYTES bytes each, all static storage. The usable
// capacity is commandable in 1..SLOTS; a push into a full ring drops the
// oldest record and counts it. Not thread-safe: the owner serialises access
// (DataRecorder holds its component lock around every call).
// ======================================================================

#ifndef Components_DataRecorder_PacketRing_HPP
#define Components_DataRecorder_PacketRing_HPP

#include <cstdint>

namespace Components {

template <uint16_t SLOTS, uint16_t SLOT_BYTES>
class PacketRing {
  public:
    PacketRing() : m_head(0), m_count(0), m_capacity(SLOTS), m_pushed(0), m_dropped(0), m_removed(0) {}

    //! Copy a packet in as the newest record; drops the oldest when full.
    //! \return false, and nothing counted, for null data, len 0 or len > SLOT_BYTES
    bool push(const uint8_t* data, uint16_t len) {
        if (data == nullptr || len == 0 || len > SLOT_BYTES) {
            return false;
        }
        if (this->m_count >= this->m_capacity) {
            this->dropOldest();
        }
        Slot& slot = this->m_slots[this->physical(this->m_count)];
        for (uint16_t i = 0; i < len; i++) {
            slot.bytes[i] = data[i];
        }
        slot.len = len;
        this->m_count++;
        this->m_pushed++;
        return true;
    }

    //! Records held now.
    uint16_t count() const { return this->m_count; }

    //! Usable slots now (1..SLOTS).
    uint16_t capacity() const { return this->m_capacity; }

    //! Set the usable slots; the surplus oldest records are dropped and counted.
    //! \return false (and no change) outside 1..SLOTS
    bool setCapacity(uint16_t capacity) {
        if (capacity == 0 || capacity > SLOTS) {
            return false;
        }
        while (this->m_count > capacity) {
            this->dropOldest();
        }
        this->m_capacity = capacity;
        return true;
    }

    //! Record i (0 = oldest). The pointer is valid until the next push/pop/setCapacity.
    bool peek(uint16_t i, const uint8_t*& data, uint16_t& len) const {
        if (i >= this->m_count) {
            return false;
        }
        const Slot& slot = this->m_slots[this->physical(i)];
        data = slot.bytes;
        len = slot.len;
        return true;
    }

    //! Remove the n oldest records (not counted as dropped).
    void pop(uint16_t n) {
        if (n > this->m_count) {
            n = this->m_count;
        }
        this->m_head = static_cast<uint16_t>((this->m_head + n) % SLOTS);
        this->m_count = static_cast<uint16_t>(this->m_count - n);
        this->m_removed += n;
    }

    //! Records accepted by push since construction.
    uint32_t pushed() const { return this->m_pushed; }

    //! Records dropped (overflow or shrink) since construction.
    uint32_t dropped() const { return this->m_dropped; }

    //! Records ever removed (popped or dropped): the id of the current oldest.
    uint32_t oldestId() const { return this->m_removed; }

  private:
    struct Slot {
        uint16_t len;
        uint8_t bytes[SLOT_BYTES];
    };

    uint16_t physical(uint16_t i) const { return static_cast<uint16_t>((this->m_head + i) % SLOTS); }

    void dropOldest() {
        this->m_head = static_cast<uint16_t>((this->m_head + 1) % SLOTS);
        this->m_count--;
        this->m_dropped++;
        this->m_removed++;
    }

    Slot m_slots[SLOTS];
    uint16_t m_head;
    uint16_t m_count;
    uint16_t m_capacity;
    uint32_t m_pushed;
    uint32_t m_dropped;
    uint32_t m_removed;
};

//! Flush trigger (cycle-m-plan 01-normative 7.3): records present and either
//! the count threshold or the interval reached.
inline bool flushDue(uint16_t count, uint16_t flushRecords, uint32_t ticksSinceFlush, uint16_t flushIntervalS) {
    return (count > 0) && ((count >= flushRecords) || (ticksSinceFlush >= flushIntervalS));
}

}  // namespace Components

#endif
