/* Some code is based on nvdla.cpp which has:
* nvdla.cpp
* Driver for Verilator testbench
* NVDLA Open Source Project
*
* Copyright (c) 2017 NVIDIA Corporation.  Licensed under the NVDLA Open
* Hardware License.  For more information, see the "LICENSE" file that came
* with this distribution.
*
* Copyright (c) 2022 Barcelona Supercomputing Center
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
*
* Authors: Guillem Lopez Paradis
*/

#ifndef __WRAPPER_NVDLA_HH__
#define __WRAPPER_NVDLA_HH__

#define NVDLA_WEIGHT_COMPRESSION_ENABLE
#define NVDLA_WINOGRAD_ENABLE
#define NVDLA_BATCH_ENABLE
#define NVDLA_SECONDARY_MEMIF_ENABLE
#define NVDLA_SDP_LUT_ENABLE
#define NVDLA_SDP_BS_ENABLE
#define NVDLA_SDP_BN_ENABLE
#define NVDLA_SDP_EW_ENABLE
#define NVDLA_BDMA_ENABLE
#define NVDLA_RUBIK_ENABLE
#define NVDLA_RUBIK_CONTRACT_ENABLE
#define NVDLA_RUBIK_RESHAPE_ENABLE
#define NVDLA_PDP_ENABLE
#define NVDLA_CDP_ENABLE
#define NVDLA_MAC_ATOMIC_C_SIZE 64
#define NVDLA_MAC_ATOMIC_K_SIZE 32
#define NVDLA_MEMORY_ATOMIC_SIZE 32
#define NVDLA_MAX_BATCH_SIZE 32
#define NVDLA_CBUF_BANK_NUMBER 16
#define NVDLA_CBUF_BANK_WIDTH 64
#define NVDLA_CBUF_BANK_DEPTH 512
#define NVDLA_SDP_BS_THROUGHPUT 16
#define NVDLA_SDP_BN_THROUGHPUT 16
#define NVDLA_SDP_EW_THROUGHPUT 4
#define NVDLA_PDP_THROUGHPUT 8
#define NVDLA_CDP_THROUGHPUT 8
#define NVDLA_PRIMARY_MEMIF_LATENCY 1200
#define NVDLA_SECONDARY_MEMIF_LATENCY 128
#define NVDLA_PRIMARY_MEMIF_MAX_BURST_LENGTH 1
#define NVDLA_PRIMARY_MEMIF_WIDTH 512
#define NVDLA_SECONDARY_MEMIF_MAX_BURST_LENGTH 4
#define NVDLA_SECONDARY_MEMIF_WIDTH 512
#define NVDLA_MEM_ADDRESS_WIDTH 64




#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <iostream>
#include <iterator>
#include <fstream>
#include <vector>
#include <algorithm> // for std::copy

#include <queue>
#include <map>
#include <vector>

#include "VNV_nvdla.h"
#include "verilated.h"
#include "verilated_vcd_c.h"
#include "csbMaster.hh"
#include "axiResponder.hh"
#include "rtl_packet_nvdla.hh"



class CSBMaster;
class AXIResponder;

class Wrapper_nvdla {

    public:
        Wrapper_nvdla(int id_nvdla, bool traceOn, std::string name, const unsigned int maxReq,
                      int _dma_enable, int _spm_latency, int _spm_line_size, int _spm_line_num, int pft_enable);
        ~Wrapper_nvdla();

        void tick();
        outputNVDLA& tick(inputNVDLA in);
        uint64_t getTickCount();
        void enableTracing();
        void disableTracing();
        void advanceTickCount();
        void reset();
        void addInput(int writeCSB);
        void init();

        void addReadReq(bool read_sram, bool read_timing,
                        uint32_t read_addr, uint32_t read_bytes);
        void addWriteReq(bool write_sram, bool write_timing,
                         uint32_t write_addr, uint8_t write_data);
        void addLongWriteReq(bool write_sram, bool write_timing,
                         uint32_t write_addr, uint32_t length, uint8_t* write_data, uint64_t mask);
        void clearOutput();

        VNV_nvdla *dla;// = new VNV_nvdla;
        uint64_t tickcount;
        VerilatedVcdC* tfp;
        std::string tfpname;
        bool traceOn;

        int id_nvdla;

        // CSB Wrapper
        CSBMaster *csb;
        AXIResponder *axi_dbb;
        AXIResponder *axi_cvsram;

        // RTL Packet
        outputNVDLA output;

        // SPM & DMA
        int dma_enable;
        int spm_latency;
        // all the sizes are in bytes
        const uint32_t spm_line_size;
        const uint32_t spm_line_num;
        std::map<uint64_t, std::vector<uint8_t> > spm;
        // todo: need a spm write waiting list to temporarily store dirty data if write mask != 0xffffffffffffffff so that we can load clean from mem

        struct spm_wr_txn{
            uint64_t addr;
            uint64_t mask;
            uint32_t countdown;
            uint8_t data[512 / 8];
        };
        std::list<spm_wr_txn> spm_write_queue;

        void addDMAReadReq(uint64_t read_addr, uint32_t read_bytes);

        uint8_t read_spm(uint64_t addr);
        void write_spm(uint64_t addr, uint8_t data);
        void flush_spm();

        // software prefetching
        int prefetch_enable;
};

#endif 