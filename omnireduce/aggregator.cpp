#include "omnireduce/aggregator.hpp"
#include "omnireduce/aggcontext.hpp"
#ifdef USE_CNAT
#include <array>
#endif
namespace omnireduce {
    thread_local static uint32_t num_server_threads;
    thread_local static uint32_t thread_id;
    thread_local static uint32_t block_size;
    thread_local static uint32_t message_size;
    thread_local static uint32_t num_slots_per_thread;
    thread_local static uint32_t num_blocks_per_thread;
    thread_local static uint32_t num_qps_per_aggregator_per_thread;
    thread_local static uint32_t num_workers;
    thread_local static uint32_t num_aggregators;
    thread_local static uint32_t element_size;
    thread_local static uint32_t buff_unit_size;
    thread_local static uint32_t num_comm_buff;
    thread_local static uint32_t typecode;
#ifdef USE_CNAT
    thread_local static constexpr uint32_t encoding_to_sign_and_exp[] =
            { 0,  18,  19,  20,  21,  22,  23,  24,  25,  26,  27,  28,  29,  30,
              31,  32,  33,  34,  35,  36,  37,  38,  39,  40,  41,  42,  43,  44,
              45,  46,  47,  48,  49,  50,  51,  52,  53,  54,  55,  56,  57,  58,
              59,  60,  61,  62,  63,  64,  65,  66,  67,  68,  69,  70,  71,  72,
              73,  74,  75,  76,  77,  78,  79,  80,  81,  82,  83,  84,  85,  86,
              87,  88,  89,  90,  91,  92,  93,  94,  95,  96,  97,  98,  99, 100,
              101, 102, 103, 104, 105, 106, 107, 108, 109, 110, 111, 112, 113, 114,
              115, 116, 117, 118, 119, 120, 121, 122, 123, 124, 125, 126, 127, 128,
              129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142,
              143, 144, 256, 274, 275, 276, 277, 278, 279, 280, 281, 282, 283, 284,
              285, 286, 287, 288, 289, 290, 291, 292, 293, 294, 295, 296, 297, 298,
              299, 300, 301, 302, 303, 304, 305, 306, 307, 308, 309, 310, 311, 312,
              313, 314, 315, 316, 317, 318, 319, 320, 321, 322, 323, 324, 325, 326,
              327, 328, 329, 330, 331, 332, 333, 334, 335, 336, 337, 338, 339, 340,
              341, 342, 343, 344, 345, 346, 347, 348, 349, 350, 351, 352, 353, 354,
              355, 356, 357, 358, 359, 360, 361, 362, 363, 364, 365, 366, 367, 368,
              369, 370, 371, 372, 373, 374, 375, 376, 377, 378, 379, 380, 381, 382,
              383, 384, 385, 386, 387, 388, 389, 390, 391, 392, 393, 394, 395, 396,
              397, 398, 399, 400};

    uint8_t cnat_compress_cpu(const float& input){
        if (input == 0) return 0;
        int exp;
        float prob = abs(frexpf(input, &exp)) * 2. - 1.; // [0.5, 1) -> [0, 1)
        if (0.5 >= prob) exp -= 1;
        exp += 127;
        uint8_t encode;
        if (exp<=17) encode = 0;
        else if (exp<=143) encode = uint8_t(exp-17);
        else encode = 127;
        if (input < 0) encode += 128;
        return encode;
    }
#endif

    int post_send_server(AggContext* dctx_ptr, uint32_t num, uint32_t slot, uint32_t qp_num, uint32_t buff_index)
    {
	    struct ibv_send_wr sr;
	    struct ibv_sge sge;
	    struct ibv_send_wr *bad_wr = NULL;
        int qid;
        int mid;
        qid = qp_num_revert[qp_num];
        mid = qp_num_to_peerid[qp_num];
        int rc;
        memset(&sge, 0, sizeof(sge));
        uint8_t *send_buff = (uint8_t *)dctx_ptr->comm_buf+(num_slots_per_thread*(2*message_size)*num_server_threads*(num_workers+buff_index)
                             +slot*(2*message_size))*buff_unit_size;
        sge.addr = (uintptr_t)send_buff;
        sge.length = block_size*num*element_size+num*sizeof(uint32_t);
        sge.lkey = dctx_ptr->mr->lkey;
        memset(&sr, 0, sizeof(sr));
        sr.wr_id = 0;
        sr.sg_list = &sge;
        sr.num_sge = 1;
        sr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        sr.send_flags = IBV_SEND_SIGNALED;
        sr.wr.rdma.remote_addr = dctx_ptr->remote_props_array[mid].addr+(buff_index*(2*message_size)*num_slots_per_thread*num_server_threads
                                 +slot*(2*message_size))*buff_unit_size;
        sr.wr.rdma.rkey = dctx_ptr->remote_props_array[mid].rkey;
        sr.imm_data = (typecode << 28) + (num << 16) + slot;
        rc = ibv_post_send(dctx_ptr->qp[qid], &sr, &bad_wr);
        if (rc)
            fprintf(stderr, "failed to post SR %d\n", rc);
        return rc;
    }

    int post_receive_server(AggContext* dctx_ptr, uint32_t slot, uint32_t thread_id, uint32_t qp_num)
    {
	    struct ibv_recv_wr rr;
	    struct ibv_sge sge;
	    struct ibv_recv_wr *bad_wr;
	    int rc;
        int qid;
        if (unlikely(qp_num==0))
            qid = thread_id*num_qps_per_aggregator_per_thread*num_workers
                    +slot%(num_qps_per_aggregator_per_thread*num_workers);
        else
            qid = qp_num_revert[qp_num];
        //std::cout<<"recv: qp_num="<<dctx_ptr->qp[qid]->qp_num<<std::endl;
        memset(&sge, 0, sizeof(sge));
        sge.addr = (uintptr_t)(dctx_ptr->comm_buf);
        sge.length = 0;
        sge.lkey = dctx_ptr->mr->lkey;
        memset(&rr, 0, sizeof(rr));
        rr.wr_id = 0;
        rr.sg_list = &sge;
        rr.num_sge = 1;
        rc = ibv_post_recv(dctx_ptr->qp[qid], &rr, &bad_wr);
        if (rc)
            fprintf(stderr, "failed to post RR\n");
        return rc;        
    }

    void *aggregator(void* arg) {
        AggContext* dctx_ptr = (AggContext*) arg;
        int ret = 0;
        int ne = 0;
        uint32_t blocks_per_packet = 0;
        uint32_t slot = 0;
        uint32_t global_slot = 0;
        uint32_t wid = 0;
        uint32_t bid = 0;
        uint32_t * meta_ptr = NULL;
        thread_id = dctx_ptr->threadid.fetch_add(1);
        buff_unit_size = omnireduce_par.getBuffUnitSize();
        num_server_threads = omnireduce_par.getNumWorkerThreads();
        block_size = omnireduce_par.getBlockSize();
        message_size = omnireduce_par.getMessageSize();
        num_slots_per_thread = omnireduce_par.getNumSlotsPerTh();
        num_blocks_per_thread = num_slots_per_thread*(message_size/block_size);
        num_qps_per_aggregator_per_thread = omnireduce_par.getNumQpsPerAggTh();
        num_workers = omnireduce_par.getNumWorkers();
        num_aggregators = omnireduce_par.getNumAggregators();
        num_comm_buff = omnireduce_par.getNumCommbuff();
        element_size = 4;

        uint32_t *current_offset = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread*message_size);
        memset(current_offset, 0, sizeof(uint32_t)*num_slots_per_thread*message_size);
        
        uint32_t **block_next_offset = (uint32_t **)malloc(sizeof(uint32_t *)*num_slots_per_thread*message_size);
        for (uint32_t i=0; i<num_slots_per_thread*message_size; i++){
            block_next_offset[i] = (uint32_t *)malloc(sizeof(uint32_t)*num_workers);
            memset(block_next_offset[i], 0, sizeof(uint32_t)*num_workers);
        }
        uint32_t **slot_to_qps = (uint32_t **)malloc(sizeof(uint32_t *)*num_slots_per_thread);
        for (uint32_t i=0; i<num_slots_per_thread; i++){
            slot_to_qps[i] = (uint32_t *)malloc(sizeof(uint32_t)*num_workers);
            memset(slot_to_qps[i], 0, sizeof(uint32_t)*num_workers);
        }  
        uint32_t *finished_blocks = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread);
        memset(finished_blocks, 0, sizeof(uint32_t)*num_slots_per_thread);
        uint32_t *completed_blocks = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread);
        memset(completed_blocks, 0, sizeof(uint32_t)*num_slots_per_thread);

        uint32_t **current_offsets = (uint32_t **)malloc(sizeof(uint32_t*)*num_slots_per_thread);
        uint32_t **next_offsets = (uint32_t **)malloc(sizeof(uint32_t*)*num_slots_per_thread);
        for (uint32_t i=0; i<num_slots_per_thread; i++){
            current_offsets[i] = (uint32_t *)malloc(sizeof(uint32_t)*message_size);
            next_offsets[i] = (uint32_t *)malloc(sizeof(uint32_t)*message_size);
            memset(current_offsets[i], 0, sizeof(uint32_t)*message_size);
            memset(next_offsets[i], 0, sizeof(uint32_t)*message_size);
        }
        uint32_t *min_next_offset = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread*message_size);
        memset(min_next_offset, 0, sizeof(uint32_t)*num_slots_per_thread*message_size);
        uint32_t *register_count = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread);
        memset(register_count, 0, sizeof(uint32_t)*num_slots_per_thread);
        uint32_t *set = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread);
        memset(set, 0, sizeof(uint32_t)*num_slots_per_thread);
        uint32_t *buff_index = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread);
        memset(buff_index, 0, sizeof(uint32_t)*num_slots_per_thread);
        dctx_ptr->set_master_ready();
        struct ibv_wc wc[MAX_CONCURRENT_WRITES * 2];
        for (uint32_t i=0; i<num_slots_per_thread*(num_workers/num_aggregators); i++)
            post_receive_server(dctx_ptr, i, thread_id, 0);
        while (!force_quit)
        {
            ne = ibv_poll_cq(dctx_ptr->cq[thread_id], MAX_CONCURRENT_WRITES * 2, (struct ibv_wc*)wc);
            if (ne>0)
            {
                for (int i = 0; i < ne; ++i)
                {
                    if (wc[i].status == IBV_WC_SUCCESS)
                    {
                        if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM)
                        {
                            block_size = omnireduce_par.getBlockSize();
                            num_blocks_per_thread = num_slots_per_thread*(message_size/block_size);
                            typecode = (wc[i].imm_data & 0xF0000000) >> 28;
                            switch (typecode)
                            {
                                case INT32:
                                    element_size = 4;
                                    break;
                                case FLOAT32:
                                    element_size = 4;
                                    break;
                                default:
                                    std::cerr<<"Data type error"<<std::endl;
                                    exit(1);    
                            }
                            blocks_per_packet = (wc[i].imm_data & 0x0FFF0000) >> 16;
                            slot = (wc[i].imm_data & 0x0000FFFF)%num_slots_per_thread;
                            global_slot = slot + num_slots_per_thread*thread_id;
                            wid = qp_num_to_peerid[wc[i].qp_num];
                            if (register_count[slot]<num_workers)
                            {
                                slot_to_qps[slot][wid] = wc[i].qp_num;
                                register_count[slot]++;
                            }
                            //std::cout<<"receive: thread id="<<thread_id<<";slot="<<global_slot<<"; blocks_per_packet= "<<blocks_per_packet<<"; worker id="<<wid<<"; qp_num="<<wc[i].qp_num<<std::endl;
                            meta_ptr = (uint32_t *)((uint8_t *)(dctx_ptr->comm_buf)+(wid*num_slots_per_thread*(2*message_size)*num_server_threads
                                        + slot*(2*message_size)+thread_id*(2*message_size)*num_slots_per_thread)*buff_unit_size
                                        + block_size*blocks_per_packet*element_size);
                            for(uint32_t k=0; k<blocks_per_packet; k++)
                            {
                                bid = (meta_ptr[k]/block_size)%num_blocks_per_thread;
                                block_next_offset[bid][wid] = meta_ptr[k];
                                min_next_offset[bid] = block_next_offset[bid][0];
                                for(uint32_t j=1; j<num_workers; j++)
                                {
                                    if (min_next_offset[bid] > block_next_offset[bid][j])
                                        min_next_offset[bid] = block_next_offset[bid][j];
                                }
                                if (current_offset[bid]<min_next_offset[bid])
                                {
                                    current_offsets[slot][completed_blocks[slot]] = current_offset[bid];
                                    next_offsets[slot][completed_blocks[slot]] = min_next_offset[bid];
                                    completed_blocks[slot]++;
                                }
                                switch (typecode)
                                {
                                    case FLOAT32:
                                        {
                                            float *aggregation_pool_float32 = (float *)((uint8_t *)(dctx_ptr->comm_buf)+((num_workers+num_comm_buff)*num_slots_per_thread*(2*message_size)*num_server_threads
                                                                     +set[slot]*num_slots_per_thread*message_size*num_server_threads)*buff_unit_size
                                                                     +((bid+thread_id*num_blocks_per_thread)*block_size)*element_size);
                                            float *recv_buff_float32 = (float *)((uint8_t *)(dctx_ptr->comm_buf)+(wid*num_slots_per_thread*(2*message_size)*num_server_threads
                                                                     +thread_id*(2*message_size)*num_slots_per_thread+slot*(2*message_size))*buff_unit_size
                                                                     +k*block_size*element_size);
                                            for(uint32_t j=0; j<block_size; j++)
                                                aggregation_pool_float32[j] += recv_buff_float32[j];                                        
                                        }
                                        break;
                                    case INT32:
                                        {
                                            int32_t *aggregation_pool_int32 = (int32_t *)((uint8_t *)(dctx_ptr->comm_buf)+((num_workers+num_comm_buff)*num_slots_per_thread*(2*message_size)*num_server_threads
                                                                     +set[slot]*num_slots_per_thread*message_size*num_server_threads)*buff_unit_size
                                                                     +((bid+thread_id*num_blocks_per_thread)*block_size)*element_size);
                                            int32_t *recv_buff_int32 = (int32_t *)((uint8_t *)(dctx_ptr->comm_buf)+(wid*num_slots_per_thread*(2*message_size)*num_server_threads
                                                                     +thread_id*(2*message_size)*num_slots_per_thread+slot*(2*message_size))*buff_unit_size
                                                                     +k*block_size*element_size);
                                            for(uint32_t j=0; j<block_size; j++)
                                                aggregation_pool_int32[j] += recv_buff_int32[j];
                                        }
                                        break;
                                    default:
                                        std::cerr<<"Data type error"<<std::endl;
                                        exit(1);
                                }

                            }
                            if (completed_blocks[slot] >= ((message_size/block_size)-finished_blocks[slot]))
                            {
                                uint8_t *send_buf = (uint8_t *)(dctx_ptr->comm_buf)+(num_slots_per_thread*(2*message_size)*num_server_threads*(num_workers+buff_index[slot])
                                                +global_slot*(2*message_size))*buff_unit_size;
                                for(uint32_t k=0; k<completed_blocks[slot]; k++)
                                {
                                    uint8_t *aggregation_ptr = (uint8_t *)(dctx_ptr->comm_buf)+(num_slots_per_thread*(2*message_size)*num_server_threads*(num_workers+num_comm_buff)
                                                                +num_slots_per_thread*message_size*num_server_threads*set[slot])*buff_unit_size
                                                                +((next_offsets[slot][k]/block_size)%num_blocks_per_thread+thread_id*num_blocks_per_thread)*block_size*element_size;
                                    memcpy(send_buf+k*block_size*element_size, aggregation_ptr, block_size*element_size);
                                }
                                memcpy(send_buf+completed_blocks[slot]*block_size*element_size, next_offsets[slot], completed_blocks[slot]*sizeof(uint32_t));
                                
                                uint8_t *shadow_aggregator = (uint8_t *)(dctx_ptr->comm_buf)+((num_workers+num_comm_buff)*num_slots_per_thread*(2*message_size)*num_server_threads
                                                            +((set[slot]+1)%2)*num_slots_per_thread*message_size*num_server_threads)*buff_unit_size
                                                            +global_slot*message_size*element_size;
                                memset(shadow_aggregator, 0, message_size*element_size);
                                
                                for(uint32_t k=0; k<num_workers; k++)
                                {
                                    for(uint32_t j=0; j<completed_blocks[slot]; j++)
                                    {
                                        if (block_next_offset[(next_offsets[slot][j]/block_size)%num_blocks_per_thread][k]==next_offsets[slot][j]
                                            && next_offsets[slot][j]<omnireduce_par.getInfOffset(0))
                                        {
                                            post_receive_server(dctx_ptr, global_slot, thread_id, slot_to_qps[slot][k]);
                                            break;
                                        }
                                    }
                                    //std::cout<<"send: slot="<<global_slot<<std::endl;
                                    ret = post_send_server(dctx_ptr, completed_blocks[slot], global_slot, slot_to_qps[slot][k], buff_index[slot]);
			                        if(ret)
			                        {
			                            fprintf(stderr, "failed to post SR\n");
		                                    exit(1);
			                        }
                                }
                                buff_index[slot] = (buff_index[slot]+1)%num_comm_buff;
                                for(uint32_t k=0; k<completed_blocks[slot]; k++)
                                {
                                    if (next_offsets[slot][k]>=omnireduce_par.getInfOffset(0))
                                    {
                                        current_offset[(next_offsets[slot][k]/block_size)%num_blocks_per_thread]=0;
                                        for(uint32_t j=0; j<num_workers; j++)
                                            block_next_offset[(next_offsets[slot][k]/block_size)%num_blocks_per_thread][j] = 0;
                                        finished_blocks[slot]++;
                                    }
                                    else
                                    {
                                        current_offset[(next_offsets[slot][k]/block_size)%num_blocks_per_thread] = next_offsets[slot][k];
                                    }
                                    
                                }
                                completed_blocks[slot] = 0;
                                if (finished_blocks[slot]==(message_size/block_size))
                                {
                                    finished_blocks[slot] = 0;
                                    buff_index[slot] = 0;
                                    for(uint32_t k=0; k<num_workers; k++)
                                        post_receive_server(dctx_ptr, global_slot, thread_id, slot_to_qps[slot][k]);
                                }
                                set[slot] = (set[slot]+1)%2;
                            }
                        } //if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM)
                    }
                } //for (int i = 0; i < ne; ++i)
            } //if (ne>0)
        } //while (!force_quit)
        return NULL;
    }//aggregator

    int dr_post_receive_server(AggContext* dctx_ptr, uint32_t slot, uint32_t thread_id, uint32_t qp_num)
    {
        int rc;
        struct ibv_recv_wr rr;
        struct ibv_sge sge;
        struct ibv_recv_wr *bad_wr;
        int qid;
        if (unlikely(qp_num==0))
            qid = thread_id*num_qps_per_aggregator_per_thread*num_workers
                    +slot%(num_qps_per_aggregator_per_thread*num_workers);
        else
            qid = qp_num_revert[qp_num];
        memset(&sge, 0, sizeof(sge));
        sge.addr = (uintptr_t)(dctx_ptr->comm_buf);
        sge.length = block_size*element_size;
        sge.lkey = dctx_ptr->mr->lkey;
        memset(&rr, 0, sizeof(rr));
        rr.wr_id = 0;
        rr.sg_list = &sge;
        rr.num_sge = 1;
        rc = ibv_post_recv(dctx_ptr->qp[qid], &rr, &bad_wr);
        if (rc)
            fprintf(stderr, "failed to post RR\n");
        return rc;          
    }

    int dr_post_send_server(AggContext* dctx_ptr, uint32_t current_offset, uint32_t next_offset, uint32_t set, uint32_t slot, uint32_t qp_num)
    {
        int rc;
        struct ibv_send_wr sr;
        struct ibv_sge sge;
        struct ibv_send_wr *bad_wr = NULL;
        int qid;
        int mid;
        uint32_t length = dctx_ptr->tensor_size - current_offset;
        if (length>block_size)
            length = block_size;
        qid = qp_num_revert[qp_num];
        mid = qp_num_to_peerid[qp_num];
        memset(&sge, 0, sizeof(sge));  
        uint8_t *tmp = (uint8_t *)dctx_ptr->comm_buf+(num_slots_per_thread*block_size*num_server_threads*(num_workers+set)
                       +slot*block_size)*buff_unit_size;
        sge.addr = (uintptr_t)tmp;
        sge.length = length*element_size;
        sge.lkey = dctx_ptr->mr->lkey;
        memset(&sr, 0, sizeof(sr));
        sr.wr_id = 0;
        sr.sg_list = &sge;
        sr.num_sge = 1;
        sr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
        sr.send_flags = IBV_SEND_SIGNALED;
        sr.wr.rdma.remote_addr =  dctx_ptr->remote_props_array[mid].addr + current_offset*element_size;
        sr.wr.rdma.rkey = dctx_ptr->remote_props_array[mid].rkey;
        sr.imm_data = next_offset;
        rc = ibv_post_send(dctx_ptr->qp[qid], &sr, &bad_wr);
        if (rc)
            fprintf(stderr, "failed to post SR %d\n", rc);
        return rc;        
    }

    void *dr_aggregator(void* arg) {
        AggContext* dctx_ptr = (AggContext*) arg;
        int ret = 0;
        int ne = 0;
        int worker_count = 0;
        uint32_t wid = 0;
        uint32_t next_offset = 0;
        uint32_t slot = 0;
        num_qps_per_aggregator_per_thread = omnireduce_par.getNumQpsPerAggTh();
        num_server_threads = omnireduce_par.getNumWorkerThreads();
        num_slots_per_thread = omnireduce_par.getNumSlotsPerTh();
        thread_id = dctx_ptr->threadid.fetch_add(1);
        num_workers = omnireduce_par.getNumWorkers();
        block_size = omnireduce_par.getBlockSize();
        buff_unit_size = omnireduce_par.getBuffUnitSize();
        num_aggregators = omnireduce_par.getNumAggregators();
        num_comm_buff = omnireduce_par.getNumCommbuff();

        uint32_t **block_next_offset = (uint32_t **)malloc(sizeof(uint32_t *)*num_slots_per_thread);
        for (uint32_t i=0; i<num_slots_per_thread; i++){
            block_next_offset[i] = (uint32_t *)malloc(sizeof(uint32_t)*num_workers);
            memset(block_next_offset[i], 0, sizeof(uint32_t)*num_workers);
        }
        uint32_t *min_next_offset = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread);
        memset(min_next_offset, 0, sizeof(uint32_t)*num_slots_per_thread);
        uint32_t **slot_to_qps = (uint32_t **)malloc(sizeof(uint32_t *)*num_slots_per_thread);
        for (uint32_t i=0; i<num_slots_per_thread; i++){
            slot_to_qps[i] = (uint32_t *)malloc(sizeof(uint32_t)*num_workers);
            memset(slot_to_qps[i], 0, sizeof(uint32_t)*num_workers);
        }  
        uint32_t *register_count = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread);
        memset(register_count, 0, sizeof(uint32_t)*num_slots_per_thread);
        uint32_t *set = (uint32_t *)malloc(sizeof(uint32_t)*num_slots_per_thread);
        memset(set, 0, sizeof(uint32_t)*num_slots_per_thread);
        dctx_ptr->set_master_ready();

        struct ibv_wc wc[MAX_CONCURRENT_WRITES * 2];
        for (uint32_t i=0; i<num_slots_per_thread*(num_workers/num_aggregators); i++)
            dr_post_receive_server(dctx_ptr, i, thread_id, 0);        
        while (!force_quit)
        {
            ne = ibv_poll_cq(dctx_ptr->cq[thread_id], MAX_CONCURRENT_WRITES * 2, (struct ibv_wc*)wc);
            if (ne>0)
            {
                for (int i = 0; i < ne; ++i)
                {
                    if (wc[i].status == IBV_WC_SUCCESS)
                    {
                        if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM)
                        {
                            block_size = omnireduce_par.getBlockSize();
                            element_size = dctx_ptr->element_size;
                            next_offset = wc[i].imm_data;
                            slot = (next_offset/block_size)%num_slots_per_thread;
                            wid = qp_num_to_peerid[wc[i].qp_num];
                            uint32_t global_slot = slot+num_slots_per_thread*thread_id;
                            if (register_count[slot]<num_workers)
                            {
                                slot_to_qps[slot][wid] = wc[i].qp_num;
                                register_count[slot]++;
                            }
                            switch (dctx_ptr->typecode)
                            {
                                case FLOAT32:
                                    {
                                        float *aggregation_pool_float32 = (float *)((uint8_t*)dctx_ptr->comm_buf+(num_slots_per_thread*block_size*num_server_threads*(num_workers+set[slot])
                                                                           +block_size*(slot+num_slots_per_thread*thread_id))*sizeof(float));
                                        float *recv_buff_float32 = (float *)((uint8_t*)dctx_ptr->comm_buf+(wid*num_slots_per_thread*block_size*num_server_threads
                                                                           +block_size*(slot+num_slots_per_thread*thread_id))*sizeof(float));
                                        for(uint32_t k=0; k<block_size; k++){
                                            aggregation_pool_float32[k] += recv_buff_float32[k];
                                        }
                                    }
                                    break;
#ifdef USE_CNAT
                                case UINT8:
                                    {
                                        float *aggregation_pool_float32 = (float *)((uint8_t*)dctx_ptr->agg_buf + (block_size*(slot+num_slots_per_thread*thread_id))*sizeof(float));
                                        auto *send_buff_uint8 = (uint8_t*)dctx_ptr->comm_buf+(num_slots_per_thread*block_size*num_server_threads*(num_workers+set[slot])
                                                                                                                  +block_size*(slot+num_slots_per_thread*thread_id))*sizeof(float);
                                        auto *recv_buff_uint8 = (uint8_t*)dctx_ptr->comm_buf+(wid*num_slots_per_thread*block_size*num_server_threads
                                                                                                           +block_size*(slot+num_slots_per_thread*thread_id))*sizeof(float);
                                        for(uint32_t k=0; k<block_size; k++){
                                            uint32_t sign_and_exp = encoding_to_sign_and_exp[recv_buff_uint8[k]] << 23;
                                            aggregation_pool_float32[k] += reinterpret_cast<float &>(sign_and_exp);
                                        }
                                    }
                                    break;
#endif
                                case INT32:
                                    {
                                        int32_t *aggregation_pool_int32 = (int32_t *)((uint8_t*)dctx_ptr->comm_buf+(num_slots_per_thread*block_size*num_server_threads*(num_workers+set[slot])
                                                                           +block_size*(slot+num_slots_per_thread*thread_id))*sizeof(int32_t));
                                        int32_t *recv_buff_int32 = (int32_t *)((uint8_t*)dctx_ptr->comm_buf+(wid*num_slots_per_thread*block_size*num_server_threads
                                                                           +block_size*(slot+num_slots_per_thread*thread_id))*sizeof(int32_t));
                                        for(uint32_t k=0; k<block_size; k++){
                                            aggregation_pool_int32[k] += recv_buff_int32[k];
                                        }
                                    }
                                    break;
                                default:
                                    std::cerr<<"Data type error"<<std::endl;
                                    exit(1);
                            }
                            block_next_offset[slot][wid] = next_offset;
                            min_next_offset[slot] = block_next_offset[slot][0];
                            for(uint32_t k=1; k<num_workers; k++){
                                if (min_next_offset[slot] > block_next_offset[slot][k])
                                    min_next_offset[slot] = block_next_offset[slot][k];
                            }
                            if(dctx_ptr->current_offset_thread[thread_id][slot]<min_next_offset[slot])
                            {
#ifdef USE_CNAT
                                auto *aggregation_pool_float32 = (float *)((uint8_t*)dctx_ptr->agg_buf + (block_size*(slot+num_slots_per_thread*thread_id))*sizeof(float));
                                auto *send_buff_uint8 = (uint8_t*)dctx_ptr->comm_buf+(num_slots_per_thread*block_size*num_server_threads*(num_workers+set[slot])
                                                                                      +block_size*(slot+num_slots_per_thread*thread_id))*sizeof(float);
                                for(uint32_t k=0; k<block_size; k++){
                                    send_buff_uint8[k] = cnat_compress_cpu(aggregation_pool_float32[k]);
				    aggregation_pool_float32[k] = 0;
                                }
#else
				switch (dctx_ptr->typecode)
                                {
                                    case FLOAT32:
                                        {
                                            float *shadow_aggregation_pool_float32 = (float *)((uint8_t*)dctx_ptr->comm_buf
                                                                                +(num_slots_per_thread*block_size*num_server_threads*(num_workers+(set[slot]+1)%num_comm_buff)
                                                                                +block_size*(slot+num_slots_per_thread*thread_id))*sizeof(float));
                                            for(uint32_t k=0; k<block_size; k++){
                                                shadow_aggregation_pool_float32[k] = 0.0;
                                            }
                                        }
                                        break;
                                    case INT32:
                                        {
                                            int32_t *shadow_aggregation_pool_int32 = (int32_t *)((uint8_t*)dctx_ptr->comm_buf
                                                                                +(num_slots_per_thread*block_size*num_server_threads*(num_workers+(set[slot]+1)%num_comm_buff)
                                                                                +block_size*(slot+num_slots_per_thread*thread_id))*sizeof(int32_t));
                                            for(uint32_t k=0; k<block_size; k++){
                                                shadow_aggregation_pool_int32[k] = 0;
                                            }
                                        }
                                        break;
                                    default:
                                        std::cerr<<"Data type error"<<std::endl;
                                        exit(1);
                                }
#endif
                                for(uint32_t k=0; k<num_workers; k++)
                                {
                                    if (min_next_offset[slot]==block_next_offset[slot][k])
                                    {
                                        ret = dr_post_receive_server(dctx_ptr, slot+num_slots_per_thread*thread_id, thread_id, slot_to_qps[slot][k]);
                                    }
                                    ret = dr_post_send_server(dctx_ptr, dctx_ptr->current_offset_thread[thread_id][slot], min_next_offset[slot], 
                                                                set[slot], slot+num_slots_per_thread*thread_id, slot_to_qps[slot][k]);
                                }
                                if (min_next_offset[slot]<omnireduce_par.getInfOffset(0))
                                {
                                    dctx_ptr->current_offset_thread[thread_id][slot] = min_next_offset[slot];
                                }
                                else
                                {
                                    for(uint32_t k=0; k<num_workers; k++)
                                        block_next_offset[slot][k] = 0;
                                }                           
                                set[slot] = (set[slot]+1)%num_comm_buff;                                                                         
                            } //if(dctx_ptr->current_offset_thread[thread_id][slot]<min_next_offset[slot])
                        }//if (wc[i].opcode == IBV_WC_RECV_RDMA_WITH_IMM)
                    } //if (wc[i].status == IBV_WC_SUCCESS)
                    else
                    {
                        std::cout<<"error code "<<wc[i].status<<" operation code "<<wc[i].opcode<<std::endl;
                    }
                } //for (int i = 0; i < ne; ++i)
            } //if (ne>0)
        } //while (!force_quit)
    } //dr_aggregator
}
