/**
  * OmniReduce project
  * author: jiawei.fei@kaust.edu.sa
  */
#pragma once

#include <string.h>
#include <vector>
#include <unordered_map>
#include <cstring>
#include <fstream>
#include <iostream>
#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string.hpp>

#define MAX_NUM_QPS 2
#define MAX_NUM_THREADS 8
#define MAX_NUM_AGGS 8
#define MAX_CONCURRENT_WRITES 4096
#define QUEUE_DEPTH_DEFAULT 4096
#define TCP_PORT 19875
#define QPNUM_FACTOR 8

namespace omnireduce {
    void parse_parameters();
    extern std::unordered_map<uint32_t, uint32_t> qp_num_revert;
    extern std::unordered_map<uint32_t, uint32_t> qp_num_to_peerid;    

    class omnireduce_params {
        private:
            uint32_t buff_unit_size;
            uint32_t num_worker_threads;
            uint32_t num_workers;
            uint32_t num_aggregators;
            uint32_t num_qps_per_aggregator_per_thread;
            uint32_t num_slots_per_thread;
            uint32_t chunk_size;
            uint32_t bitmap_chunk_size;
            uint32_t message_size;
            uint32_t block_size;
            uint32_t num_comm_buff;
            uint32_t prepost_recv_num;
            uint32_t *inf_offset;
            uint32_t direct_memory;
            uint32_t gpu_devId;
            int ib_port;
            int gid_idx;
            int sl;
            char **aggregator_ipaddr;
            char **worker_ipaddr;
        public:
            omnireduce_params();
            ~omnireduce_params();
            void setIbPort(int p) {
                ib_port=p;
            }
            void setGidIdx(int g) {
                gid_idx=g;
            }
            void setServiceLevel(int s) {
                sl=s;
            }
            void setInfOffset(uint32_t num_blocks_per_thread) {
                inf_offset = (uint32_t *)malloc(num_blocks_per_thread*sizeof(uint32_t));      
                for (uint32_t i=0; i<num_blocks_per_thread; i++)
                    inf_offset[i] = (UINT32_MAX/block_size/num_blocks_per_thread-1)*num_blocks_per_thread*block_size+i*block_size;                
            }
            void setPrepostRecvNum(uint32_t prn) {
                prepost_recv_num = prn;
            }
            void setNumSlotsPerThread() {
                num_slots_per_thread = QPNUM_FACTOR * num_qps_per_aggregator_per_thread * num_aggregators;
            }
            void setNumWorkerThreads(uint32_t tn) {
                num_worker_threads = tn;
            }
            void setNumWorkers(uint32_t wn) {
                num_workers = wn;
            }
            void setNumAggregators(uint32_t an) {
                num_aggregators = an;
            }
            void setChunkSize(uint32_t cs) {
                chunk_size = cs;
            }
            void setBitmapChunkSize(uint32_t bcs) {
                bitmap_chunk_size = bcs;
            }
            void setMessageSize(uint32_t ms) {
                message_size = ms;
            }
            void setBlockSize(uint32_t bs) {
                block_size = bs;
            }
            void setDirectMemory(uint32_t dm) {
                direct_memory = dm;
            }
            void setWorkerIps(std::string workerIps) {
                std::vector<std::string> ips;
                boost::split(ips, workerIps, boost::is_any_of(","));
                if (num_workers!=ips.size())
                {
                    std::cerr<<"Worker number error!"<<std::endl;
                    exit(1);
                }
                worker_ipaddr = (char **)malloc(num_workers*sizeof(char *));
                for (uint32_t i=0; i<num_workers; i++)
                {
                    worker_ipaddr[i] = (char*)malloc(20*sizeof(char));
                    strcpy(worker_ipaddr[i], ips[i].c_str());
                }                 
            }
            void setAggregatorIps(std::string aggregatorIps) {
                std::vector<std::string> ips;
                boost::split(ips, aggregatorIps, boost::is_any_of(","));
                if (num_aggregators!=ips.size())
                {
                    std::cerr<<"Aggregator number error!"<<std::endl;
                    exit(1);
                }
                aggregator_ipaddr = (char **)malloc(num_aggregators*sizeof(char *));
                for (uint32_t i=0; i<num_aggregators; i++)
                {
                    aggregator_ipaddr[i] = (char*)malloc(20*sizeof(char));
                    strcpy(aggregator_ipaddr[i], ips[i].c_str());
                } 
            }
            void setGpuDeviceId(uint32_t devId) {
                gpu_devId = devId;
            }
            uint32_t getBuffUnitSize() {
                return buff_unit_size;
            }
            uint32_t getNumWorkerThreads() {
                return num_worker_threads;
            }
            uint32_t getNumWorkers() {
                return num_workers;
            }
            uint32_t getNumAggregators() {
                return num_aggregators;
            }
            uint32_t getNumQpsPerAggTh() {
                return num_qps_per_aggregator_per_thread;
            }
            uint32_t getNumSlotsPerTh() {
                return num_slots_per_thread;
            }
            uint32_t getChunkSize() {
                return chunk_size;
            }
            uint32_t getBitmapChunkSize() {
                return bitmap_chunk_size;
            }
            uint32_t getMessageSize() {
                return message_size;
            }
            uint32_t getBlockSize() {
                return block_size;
            }
            uint32_t getDirectMemory() {
                return direct_memory;
            }
            uint32_t getNumCommbuff() {
                return num_comm_buff;
            }
            uint32_t getPrepostRecvNum() {
                return prepost_recv_num;
            }
            uint32_t getInfOffset(uint32_t i) {
                return inf_offset[i];
            }
            char *getAggregatorIP(uint32_t i) {
                return aggregator_ipaddr[i];
            }
            char *getWorkerIP(uint32_t i) {
                return worker_ipaddr[i];
            }
            int getIbPort() {
                return ib_port;
            }
            int getGidIdx() {
                return gid_idx;
            }
            int getServiceLevel() {
                return sl;
            }
            uint32_t getGpuDeviceId() {
                return gpu_devId;
            }
    };

    extern omnireduce_params omnireduce_par;
}