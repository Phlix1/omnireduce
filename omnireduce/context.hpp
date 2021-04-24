/**
  * OmniReduce project
  * author: jiawei.fei@kaust.edu.sa
  */

#pragma once

#include "omnireduce/common.hpp"

namespace omnireduce {
    void *OmniMaster(void *ctx);
  
    class OmniContext {
        public:
            static OmniContext& getInstance(void *CommBuf, size_t BufSize) {
                static OmniContext instance(CommBuf, BufSize);
                return instance;
            }
         
            OmniContext(OmniContext const&) = delete;
            void operator=(OmniContext const&) = delete;
            void wait_master_ready();
            void set_master_ready();
            void set_num_worker_threads(uint32_t);
            uint32_t get_num_worker_threads();
            void set_block_size(uint32_t);

            void receive_result(const int32_t);
            bool send_result(const int32_t);
            void send_tensor(TensorUpdate*);
            bool receive_tensor(TensorUpdate&, uint32_t);

            void init();
            void StartMaster();
            void StopMaster();
            void send_address(int, TensorUpdateType, uint32_t);

            void AllReduce(float*, int, uint8_t*, int);
            void AllReduce(int32_t*, int, uint8_t*, int);
            void AllReduce(float*, int);
            void AllReduce(int32_t*, int);
            int workerId;
            int *socks;
            void *comm_buf;
            void *total_comm_buff;
            size_t buf_size;
            void *cuda_comm_buf;
            struct ibv_context *ib_ctx;
            struct ibv_port_attr port_attr;
            struct ibv_pd *pd;
            struct ibv_cq **cq;
            struct ibv_qp **qp;
            struct ibv_cq *cq_address;
            struct ibv_qp **qp_address;
            struct ibv_mr *mr;
            uint32_t *src_;
            struct ibv_mr *mr_;
            struct remote_con_data_t *remote_props_array;
            std::atomic_uint_fast32_t threadid;
            int ret;
        
        private:
            OmniContext(void *CommBuf, size_t BufSize);
            virtual ~OmniContext();

            pthread_t masterThread;

            std::atomic_uint_fast32_t tid_counter;
            boost::mutex master_ready_mutex, data_ready_mutex, result_mutex;
            boost::condition_variable master_ready_event, data_push_event, data_pop_event, result_push_event, result_pop_event;
            uint32_t num_worker_threads;

            uint32_t master_ready;
            uint32_t data_ready;
            uint32_t results;
            TensorUpdate* tensor_update_ptr;
            int32_t result_id;


            boost::chrono::milliseconds one_msec;
            boost::chrono::microseconds one_microsec;
    };
}
