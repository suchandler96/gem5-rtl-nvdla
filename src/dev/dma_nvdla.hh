/*
 * Copyright (c) 2012-2013, 2015, 2017, 2019 ARM Limited
 * All rights reserved.
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2004-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __DEV_DMA_NVDLA_HH__
#define __DEV_DMA_NVDLA_HH__

#include <deque>
#include <memory>

#include "base/addr_range_map.hh"
#include "base/chunk_generator.hh"
#include "base/circlebuf.hh"
#include "dev/io_device.hh"
#include "dev/dma_device.hh"
#include "mem/backdoor.hh"
#include "sim/drain.hh"
#include "sim/system.hh"

namespace gem5
{

class ClockedObject;


/**
 * Buffered DMA engine helper class
 *
 * This class implements a simple DMA engine that feeds a FIFO
 * buffer. The size of the buffer, the maximum number of pending
 * requests and the maximum request size are all set when the engine
 * is instantiated.
 *
 * An <i>asynchronous</i> transfer of a <i>block</i> of data
 * (designated by a start address and a size) is started by calling
 * the startFill() method. The DMA engine will aggressively try to
 * keep the internal FIFO full. As soon as there is room in the FIFO
 * for more data <i>and</i> there are free request slots, a new fill
 * will be started.
 *
 * Data in the FIFO can be read back using the get() and tryGet()
 * methods. Both request a block of data from the FIFO. However, get()
 * panics if the block cannot be satisfied, while tryGet() simply
 * returns false. The latter call makes it possible to implement
 * custom buffer underrun handling.
 *
 * A simple use case would be something like this:
 * \code{.cpp}
 *     // Create a DMA engine with a 1KiB buffer. Issue up to 8 concurrent
 *     // uncacheable 64 byte (maximum) requests.
 *     DmaNvdla *dma = new DmaNvdla(port, 1024, 64, 8,
 *                                        Request::UNCACHEABLE);
 *
 *     // Start copying 4KiB data from 0xFF000000
 *     dma->startFill(0xFF000000, 0x1000);
 *
 *     // Some time later when there is data in the FIFO.
 *     uint8_t data[8];
 *     dma->get(data, sizeof(data))
 * \endcode
 *
 *
 * The DMA engine allows new blocks to be requested as soon as the
 * last request for a block has been sent (i.e., there is no need to
 * wait for pending requests to complete). This can be queried with
 * the atEndOfBlock() method and more advanced implementations may
 * override the onEndOfBlock() callback.
 */
class DmaNvdla : public Drainable, public Serializable
{
  public:
    DmaNvdla(ClockedObject* owner,
             bool proactive_get,
             DmaPort &port, bool _is_write, size_t size,
             unsigned max_req_size,
             unsigned max_pending,
             Request::Flags flags=0,
             EventFunctionWrapper event=[this]{});

    ~DmaNvdla();

  public: // Serializable
    void serialize(CheckpointOut &cp) const override;
    void unserialize(CheckpointIn &cp) override;

  public: // Drainable
    DrainState drain() override;

  public: // FIFO access
    /**
     * @{
     * @name FIFO access
     */
    /**
     * Try to read data from the FIFO.
     *
     * This method reads len bytes of data from the FIFO and stores
     * them in the memory location pointed to by dst. The method
     * fails, and no data is written to the buffer, if the FIFO
     * doesn't contain enough data to satisfy the request.
     *
     * @param dst Pointer to a destination buffer
     * @param len Amount of data to read.
     * @return true on success, false otherwise.
     */
    bool tryGet(uint8_t *dst, size_t len);

    template<typename T>
    bool
    tryGet(T &value)
    {
        return tryGet(static_cast<T *>(&value), sizeof(T));
    };

    /**
     * Read data from the FIFO and panic on failure.
     *
     * @see tryGet()
     *
     * @param dst Pointer to a destination buffer
     * @param len Amount of data to read.
     */
    void get(uint8_t *dst, size_t len);

    template<typename T>
    T
    get()
    {
        T value;
        get(static_cast<uint8_t *>(&value), sizeof(T));
        return value;
    };

    /** Get the amount of data stored in the FIFO */
    size_t size() const { return buffer.size(); }

    /** @} */
  public: // FIFO fill control
    /**
     * @{
     * @name FIFO fill control
     */
    /**
     * Start filling the FIFO.
     *
     * @warn It's considered an error to call start on an active DMA
     * engine unless the last request from the active block has been
     * sent (i.e., atEndOfBlock() is true).
     *
     * @param start Physical address to copy from.
     * @param size Size of the block to copy.
     */
    void startFill(Addr start, size_t size, uint8_t* d = nullptr);

    /**
     * Stop the DMA engine.
     *
     * Stop filling the FIFO and ignore incoming responses for pending
     * requests. The onEndOfBlock() callback will not be called after
     * this method has been invoked. However, once the last response
     * has been received, the onIdle() callback will still be called.
     */
    void stopFill();

    /**
     * Has the DMA engine sent out the last request for the active
     * block?
     */
    bool atEndOfBlock() const { return nextAddr == endAddr; }

    /**
     * Is the DMA engine active (i.e., are there still in-flight
     * accesses)?
     */
    bool
    isActive() const
    {
        return !(pendingRequests.empty() && atEndOfBlock());
    }

    /** @} */
  protected: // Callbacks
    /**
     * @{
     * @name Callbacks
     */
    /**
     * End of block callback
     *
     * This callback is called <i>once</i> after the last access in a
     * block has been sent. It is legal for a derived class to call
     * startFill() from this method to initiate a transfer.
     */
    virtual void onEndOfBlock() {};

    /**
     * Last response received callback
     *
     * This callback is called when the DMA engine becomes idle (i.e.,
     * there are no pending requests).
     *
     * It is possible for a DMA engine to reach the end of block and
     * become idle at the same tick. In such a case, the
     * onEndOfBlock() callback will be called first. This callback
     * will <i>NOT</i> be called if that callback initiates a new DMA transfer.
     */
    virtual void onIdle() {};

    /** @} */
  private: // Configuration
    /** Maximum request size in bytes */
    const Addr maxReqSize;
    /** Maximum FIFO size in bytes */
    const size_t fifoSize;
    /** Request flags */
    const Request::Flags reqFlags;

    DmaPort &port;

    const int cacheLineSize;

  private:
    class DmaDoneEvent : public Event
    {
      public:
        DmaDoneEvent(DmaNvdla *_parent, size_t max_size);

        void kill();
        void cancel();
        bool canceled() const { return _canceled; }
        void reset(size_t size, Addr addr);
        void process();

        bool done() const { return _done; }
        size_t requestSize() const { return _requestSize; }
        const uint8_t *data() const { return _data.data(); }
        uint8_t *data() { return _data.data(); }

      private:
        DmaNvdla *parent;
        bool _done = false;
        bool _canceled = false;
        size_t _requestSize;
      public:
        std::vector<uint8_t> _data;
        Addr _addr;
    };

    typedef std::unique_ptr<DmaDoneEvent> DmaDoneEventUPtr;

    /**
     * DMA request done, handle incoming data and issue new
     * request.
     */
    void dmaDone();

    /** Handle pending requests that have been flagged as done. */
    void handlePending();

    /** Try to issue new DMA requests or bypass DMA requests*/
    void resumeFill();

    /** Try to issue new DMA requests during normal execution*/
    void resumeFillTiming();

    /** Try to bypass DMA requests in non-caching mode */
    void resumeFillBypass();

  private: // Internal state
    Fifo<uint8_t> buffer;

    Addr nextAddr = 0;
    Addr endAddr = 0;

    std::deque<DmaDoneEventUPtr> pendingRequests;
    std::deque<DmaDoneEventUPtr> freeRequests;
    bool is_write;
    bool proactive_get;     // whether owner will treGet() proactively

  public:
    std::vector<std::pair<Addr, std::vector<uint8_t>>> owner_fetch_buffer;
    ClockedObject* owner;   // e.g., SPM, rtlNVDLA, ...
    EventFunctionWrapper accessDMADataEvent;
};

} // namespace gem5

#endif // __DEV_DMA_NVDLA_HH__
