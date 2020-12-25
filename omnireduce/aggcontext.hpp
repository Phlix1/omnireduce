#pragma once
#include "omnireduce/common.hpp"

namespace omnireduce {
    class AggContext {
        public:
            static AggContext& getInstance() {
                static AggContext instance;
                return instance;
            }
            AggContext(AggContext const&) = delete;
            void operator=(AggContext const&) = delete;            
            uint32_t num_server_threads;
            int ret;
            int serverId;
            int *socks;
            void *comm_buf;
            struct ibv_context *ib_ctx;
            struct ibv_port_attr port_attr;
            struct ibv_pd *pd;
            struct ibv_cq **cq;
            struct ibv_qp **qp;
            struct ibv_mr *mr;
            struct remote_con_data_t *remote_props_array;
            std::atomic_uint_fast32_t threadid;
            AggContext();
            ~AggContext();
            void init();
            void StartMaster();
            void StopMaster();
            pthread_t aggmasterThread;
    };
}
