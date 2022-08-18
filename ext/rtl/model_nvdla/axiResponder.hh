/* nvdla.cpp
 * Driver for Verilator testbench
 * NVDLA Open Source Project
 *
 * Copyright (c) 2017 NVIDIA Corporation.  Licensed under the NVDLA Open
 * Hardware License.  For more information, see the "LICENSE" file that came
 * with this distribution.
 */

#ifndef __AXI_RESPONDER__
#define __AXI_RESPONDER__

#include <list>

#include "wrapper_nvdla.hh"

class Wrapper_nvdla;

class AXIResponder {
public:
    struct connections {
        uint8_t *aw_awvalid;
        uint8_t *aw_awready;
        uint8_t *aw_awid;
        uint8_t *aw_awlen;
        uint64_t *aw_awaddr;

        uint8_t *w_wvalid;
        uint8_t *w_wready;
        uint32_t *w_wdata;
        uint64_t *w_wstrb;
        uint8_t *w_wlast;

        uint8_t *b_bvalid;
        uint8_t *b_bready;
        uint8_t *b_bid;

        uint8_t *ar_arvalid;
        uint8_t *ar_arready;
        uint8_t *ar_arid;
        uint8_t *ar_arlen;
        uint64_t *ar_araddr;

        uint8_t *r_rvalid;
        uint8_t *r_rready;
        uint8_t *r_rid;
        uint8_t *r_rlast;
        uint32_t *r_rdata;
    };

private:

#define AXI_BLOCK_SIZE 4096
#define AXI_WIDTH 512

    const static int AXI_R_LATENCY = 2;
    const static int AXI_R_DELAY = 0;

    struct axi_r_txn {
        int rvalid;
        int rlast;
        bool burst;
        uint32_t rdata[AXI_WIDTH / 32];
        uint8_t rid;
    };
    std::queue<axi_r_txn> r_fifo;
    std::queue<axi_r_txn> r0_fifo;

    struct axi_aw_txn {
        uint8_t awid;
        uint32_t awaddr;
        uint8_t awlen;
    };
    std::queue<axi_aw_txn> aw_fifo;

    struct axi_w_txn {
        uint32_t wdata[AXI_WIDTH / 32];
        uint64_t wstrb;
        uint8_t wlast;
    };
    std::queue<axi_w_txn> w_fifo;

    struct axi_b_txn {
        uint8_t bid;
    };
    std::queue<axi_b_txn> b_fifo;

    std::map<uint32_t, std::vector<uint8_t> > ram;

    struct connections dla;
    const char *name;
    Wrapper_nvdla *wrapper;
    // map key:addr, data:txn
    std::map<uint32_t, std::list<axi_r_txn>> inflight_req;
    std::queue<uint32_t> inflight_req_order;
    bool sram;
    unsigned int max_req_inflight;


    std::queue<uint64_t> dma_addr_fifo;
    std::map<uint64_t, uint32_t> dma_addr_record;
    std::list<uint64_t> waiting_for_dma_txn_addr_order;
    std::map<uint64_t, std::list<axi_r_txn>> waiting_for_dma_txn;
    // std::queue<std::pair<uint32_t, axi_r_txn>> spm_access_txn_fifo;

public:
    AXIResponder(struct connections _dla, Wrapper_nvdla *_wrapper,
                     const char *_name,
                     bool sram, const unsigned int maxReq);

    uint32_t getRequestsOnFlight();

    // In this function we read from memory
    uint8_t read_ram(uint32_t addr);

    uint8_t read(uint32_t addr);

    // In this function, we get read requests from traceLoaderGem5 and access memory for it
    void read_for_traceLoaderGem5(uint32_t start_addr, uint32_t length);

    // In this function, we check whether the read requests from traceLoaderGem5 have been responded by memory
    // If so, we forward the response to traceLoaderGem5
    uint32_t read_response_for_traceLoaderGem5(uint32_t start_addr, uint8_t* data_buffer);

    // In this function we read from memory
    const uint8_t* read_variable(uint32_t addr, bool timing,
                                 unsigned int bytes);

    // In this function we write to memory
    void write(uint32_t addr, uint8_t data, bool timing);
    void write_ram(uint32_t addr, uint8_t data);



    void insertPacket(uint8_t* data, axi_r_txn* txn);

    void eval_timing();
    void eval_atomic();
    void eval_ram();

    void emptyInflight();

    void inflight_resp(uint32_t addr, const uint8_t* data);
    void inflight_resp_atomic(uint32_t addr,
                              const uint8_t* data,
                              axi_r_txn *txn);

    void inflight_dma_resp(uint64_t addr, const uint8_t* data, uint32_t len);
};
#endif