#include "omnireduce/context.hpp"
#include "omnireduce/omnireduce.hpp"
#include "omnireduce/cuda_utils.hpp"

namespace omnireduce {
    void *OmniMaster(void *ctx) {
        OmniContext* d_ctx_ptr = (OmniContext *) ctx;

        d_ctx_ptr->ret = master(d_ctx_ptr);

        return NULL;
    }

    OmniContext::OmniContext() :
            num_worker_threads (1), master_ready(0), data_ready(0), results(0), tensor_update_ptr(NULL), result_id(0), one_msec(1) {
        
        tid_counter.store(0);
        threadid.store(0);
        init();
        StartMaster();
    }
    OmniContext::~OmniContext() {

        StopMaster();
    }

    void OmniContext::set_num_worker_threads(uint32_t nt) {
        num_worker_threads = nt;
    }

    uint32_t OmniContext::get_num_worker_threads() {
        return num_worker_threads;
    }  

    void OmniContext::wait_master_ready() {
        boost::unique_lock<boost::mutex> lock(master_ready_mutex);

        while (master_ready!=num_worker_threads)
            master_ready_event.wait(lock);
    }    

    void OmniContext::set_master_ready() {

        boost::unique_lock<boost::mutex> lock(master_ready_mutex);

        if ((++master_ready) == num_worker_threads)
            master_ready_event.notify_one();
    }

    void OmniContext::send_tensor(TensorUpdate* tuptr) {
        boost::unique_lock<boost::mutex> lock(data_ready_mutex);

        while (data_ready!=0)
            data_pop_event.wait(lock);

        tensor_update_ptr = tuptr;
        data_ready = num_worker_threads;
        data_push_event.notify_all();
    }

    bool OmniContext::receive_tensor(TensorUpdate& tu, uint32_t thread_id) {
        boost::unique_lock<boost::mutex> lock(data_ready_mutex);

        while (data_ready!=(uint32_t)(thread_id+1)) {
            if (data_push_event.wait_for(lock, one_msec) == boost::cv_status::timeout)
                return false;
        }

        tu = *tensor_update_ptr; // Copy
        uint32_t message_size = omnireduce_par.getMessageSize();
        uint32_t message_count = tu.count / message_size;
        if ((tu.count % message_size)!=0)
            message_count += 1;

        if (data_ready != 1){
            if (message_count%num_worker_threads>thread_id) 
                message_count = message_count/num_worker_threads+1;
            else
                message_count /= num_worker_threads;
            tu.count = message_count * message_size;
        } else {
            tu.count -= tu.start_idx;
        }
        tensor_update_ptr->start_idx += tu.count;

        if ((--data_ready) == 0)
            data_pop_event.notify_one();

        return true;
    }

    bool OmniContext::send_result(const int32_t rid) {
        boost::unique_lock<boost::mutex> lock(result_mutex);

        while (results == num_worker_threads) {
            if (result_pop_event.wait_for(lock, one_msec) == boost::cv_status::timeout)
                return false;
        }

        if ((++results)==num_worker_threads) {
            result_id = rid;
            result_push_event.notify_all();
        }

        return true;
    }

    void OmniContext::receive_result(const int32_t rid) {
        boost::unique_lock<boost::mutex> lock(result_mutex);
        while (results != num_worker_threads && result_id != rid) {
            result_push_event.wait(lock);
        }

        results = 0;
        result_id = 0;

        result_pop_event.notify_all();
    }

    void OmniContext::StartMaster() {
        pthread_attr_t attr;
        cpu_set_t cpus;
        pthread_attr_init(&attr);
        CPU_ZERO(&cpus);
        CPU_SET(10, &cpus);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
        if (pthread_create(&masterThread, &attr, OmniMaster, this)) {
            std::cerr<<"Error starting master thread"<<std::endl;
            exit(1);
        }

        wait_master_ready();
    }

    void OmniContext::StopMaster() {

        force_quit = true;

        int join_ret = pthread_join(masterThread, NULL);
        if (join_ret) {
            std::cerr<<"Error joining master thread: returned"<<std::endl;
            exit(1);            
        }
        if (this->ret < 0) {
            std::cerr<<"Master thread returned"<<std::endl;
            exit(1);              
        }
    }

    void OmniContext::AllReduce(float *ptr, int count, uint8_t* bitmap_ptr, int block_count)
    {
        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        tu.ptr = ptr;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.root = 0;
        tu.type = FLOAT32;
        tu.op = ALLREDUCE;
        tu.bitmap_ptr = bitmap_ptr;
        tu.block_count = block_count;
        tu.devId = -1;
        tu.async = false;
        send_tensor(&tu);
        receive_result(tensor_id);
    }

    void OmniContext::AllReduce(int32_t *ptr, int count, uint8_t* bitmap_ptr, int block_count)
    {
        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        tu.ptr = ptr;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.root = 0;
        tu.type = INT32;
        tu.op = ALLREDUCE;
        tu.bitmap_ptr = bitmap_ptr;
        tu.block_count = block_count;
        tu.devId = -1;
        tu.async = false;
        send_tensor(&tu);
        receive_result(tensor_id);
    }

#ifdef USE_CUDA
    void OmniContext::AllReduce(float *ptr, int count, uint8_t* bitmap_ptr, int block_count, cudaStream_t stream, int devId)
    {  
        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        cudaMemcpy(host_tensor, ptr, count*sizeof(float), cudaMemcpyDeviceToHost);
        tu.ptr = host_tensor;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.root = 0;
        tu.type = FLOAT32;
        tu.op = ALLREDUCE;
        tu.bitmap_ptr = bitmap_ptr;
        tu.block_count = block_count;
        tu.devId = devId;
        tu.async = false;
        send_tensor(&tu);
        receive_result(tensor_id);
        cudaMemcpy(ptr, host_tensor, count*sizeof(float), cudaMemcpyHostToDevice);      
    }
    void OmniContext::AllReduce(int32_t *ptr, int count, uint8_t* bitmap_ptr, int block_count, cudaStream_t stream, int devId)
    {  
        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        cudaMemcpy(host_tensor, ptr, count*sizeof(int32_t), cudaMemcpyDeviceToHost);
        tu.ptr = host_tensor;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.root = 0;
        tu.type = INT32;
        tu.op = ALLREDUCE;
        tu.bitmap_ptr = bitmap_ptr;
        tu.block_count = block_count;
        tu.devId = devId;
        tu.async = false;
        send_tensor(&tu);
        receive_result(tensor_id);
        cudaMemcpy(ptr, host_tensor, count*sizeof(int32_t), cudaMemcpyHostToDevice);      
    }
    void OmniContext::AllReduce(float *ptr, int count, uint8_t* bitmap_ptr, int block_count, cudaStream_t stream, int devId, bool async)
    {
        cudaStreamSynchronize(stream);
        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        tu.ptr = ptr;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.root = 0;
        tu.type = FLOAT32;
        tu.op = ALLREDUCE;
        tu.bitmap_ptr = bitmap_ptr;
        tu.block_count = block_count;
        tu.devId = devId;
        tu.async = async;
        send_tensor(&tu);
        receive_result(tensor_id);        
    }
    void OmniContext::AllReduce(int32_t *ptr, int count, uint8_t* bitmap_ptr, int block_count, cudaStream_t stream, int devId, bool async)
    {
        cudaStreamSynchronize(stream);
        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        tu.ptr = ptr;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.root = 0;
        tu.type = INT32;
        tu.op = ALLREDUCE;
        tu.bitmap_ptr = bitmap_ptr;
        tu.block_count = block_count;
        tu.devId = devId;
        tu.async = async;
        send_tensor(&tu);
        receive_result(tensor_id);
    }
    void OmniContext::AllReduce(float *ptr, int count, cudaStream_t stream, int devId)
    {
        uint32_t block_size = omnireduce_par.getBlockSize();
        uint32_t block_count = count/block_size;
        if (count%block_size!=0)
            block_count += 1;
        uint8_t *d_bitmap;
        cudaMalloc((void **)&d_bitmap, block_count);
        compute_bitmap(ptr, d_bitmap, count, block_size, stream);
        cudaMemcpy((uint8_t *)bitmap, (uint8_t *)d_bitmap, block_count, cudaMemcpyDeviceToHost);
        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        tu.ptr = ptr;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.root = 0;
        tu.type = FLOAT32;
        tu.op = ALLREDUCE;
        tu.bitmap_ptr = bitmap;
        tu.block_count = block_count;
        tu.devId = devId;
        tu.async = true;
        send_tensor(&tu);
        receive_result(tensor_id);
        cudaFree(d_bitmap);      
    }
    void OmniContext::AllReduce(int32_t *ptr, int count, cudaStream_t stream, int devId)
    {
        uint32_t block_size = omnireduce_par.getBlockSize();
        uint32_t block_count = count/block_size;
        if (count%block_size!=0)
            block_count += 1;
        uint8_t *d_bitmap;
        cudaMalloc((void **)&d_bitmap, block_count);
        compute_bitmap(ptr, d_bitmap, count, block_size, stream);
        cudaMemcpy((uint8_t *)bitmap, (uint8_t *)d_bitmap, block_count, cudaMemcpyDeviceToHost);
        int32_t tensor_id = tid_counter.fetch_add(1)+1;
        TensorUpdate tu;
        tu.ptr = ptr;
        tu.count = count;
        tu.start_idx = 0;
        tu.id = tensor_id;
        tu.root = 0;
        tu.type = INT32;
        tu.op = ALLREDUCE;
        tu.bitmap_ptr = bitmap;
        tu.block_count = block_count;
        tu.devId = devId;
        tu.async = true;
        send_tensor(&tu);
        receive_result(tensor_id);
        cudaFree(d_bitmap);
    }
#endif

    void OmniContext::init() {
        //step 1 - read and set para
        parse_parameters();
        int cycle_buffer = sysconf(_SC_PAGESIZE);
        
        int num_devices;
        char *dev_name = NULL;
        struct ibv_device **dev_list = NULL;
        struct ibv_qp_init_attr *qp_init_attr = NULL;
        struct ibv_device *ib_dev = NULL;
        int ib_port = 1;
        int cq_size = 0;
        int mr_flags = 0;
        
        uint32_t buff_unit_size = omnireduce_par.getBuffUnitSize();
        uint32_t num_aggregators = omnireduce_par.getNumAggregators();
        uint32_t num_qps_per_aggregator_per_thread = omnireduce_par.getNumQpsPerAggTh();
        uint32_t num_qps_per_thread = num_qps_per_aggregator_per_thread*num_aggregators;
        uint32_t num_slots_per_thread = omnireduce_par.getNumSlotsPerTh();
        uint32_t message_size = omnireduce_par.getMessageSize();
        uint32_t num_comm_buff = omnireduce_par.getNumCommbuff();

        set_num_worker_threads(omnireduce_par.getNumWorkerThreads());

        size_t comm_buf_size = 0;
        remote_props_array = (struct remote_con_data_t *)malloc(num_aggregators*sizeof(struct remote_con_data_t));

        //step 2 - create resources
        /* get device names in the system */
        dev_list = ibv_get_device_list(&num_devices);
        if (!dev_list) {
            std::cerr<<"failed to get IB devices list"<<std::endl;
            exit(1);
        }
	    if (!num_devices)
	    {
	    	std::cerr<<"Found %d device(s)"<<std::endl;
	    	exit(1);
	    }
        /* search for the specific device we want to work with */
	    for (int i = 0; i < num_devices; i++)
	    {
	    	if (!dev_name)
	    	{
	    		dev_name = strdup(ibv_get_device_name(dev_list[i]));
	    		std::cout<<"IB device not specified, using first one found: "<<dev_name<<std::endl;
	    	}
	    	if (!strcmp(ibv_get_device_name(dev_list[i]), dev_name))
	    	{
	    		ib_dev = dev_list[i];
	    		break;
	    	}
	    }
        /* if the device wasn't found in host */
	    if (!ib_dev)
	    {
	    	std::cerr<<"IB device %s wasn't found"<<std::endl;
	    	exit(1);
	    }
        /* get device handle */
        ib_ctx = ibv_open_device(ib_dev);
        if(!ib_ctx)
        {
	    	std::cerr<<"failed to open device "<<dev_name<<std::endl;
	    	exit(1);            
        }
        /* Free device list */
        ibv_free_device_list(dev_list);
        dev_list = NULL;
        ib_dev = NULL;
        /* query port properties */
        if (ibv_query_port(ib_ctx, ib_port, &port_attr))
        {
            std::cerr<<"ibv_query_port on port "<<ib_port<<" failed"<<std::endl;
            exit(1);
        }
        /* allocate Protection Domain */
        pd = ibv_alloc_pd(ib_ctx);
        if (!pd)
        {
	    	std::cerr<<"ibv_alloc_pd failed"<<std::endl;
	    	exit(1);
        }
        /* create completion queue */
        cq_size = MAX_CONCURRENT_WRITES * 2;
        cq = (struct ibv_cq **)malloc(num_worker_threads*sizeof(struct ibv_cq *));	
        for (size_t i=0; i<num_worker_threads; i++)
        {
            cq[i] = ibv_create_cq(ib_ctx, cq_size, NULL, NULL, 0);
            if (!cq[i])
            {
                std::cerr<<"failed to create CQ with "<<cq_size<<" entries"<<std::endl;
                exit(1);
            }
        }   
        /* allocate the memory worker send/recv buffer */
        comm_buf_size = num_slots_per_thread*(message_size*2)*num_worker_threads*num_comm_buff;
#ifdef USE_CUDA
        ret = cudaMallocHost((void **)&comm_buf, comm_buf_size*buff_unit_size);
        ret = cudaMallocHost((void **)&host_tensor, 1024*1024*1024);
        ret = cudaMallocHost((void **)&bitmap, 1024*1024*1024);
#else
        ret = posix_memalign(reinterpret_cast<void**>(&comm_buf), cycle_buffer, comm_buf_size*buff_unit_size);
#endif
        if (ret!=0)
        {
            std::cerr<<"failed to malloc "<<comm_buf_size*buff_unit_size<<" bytes to communication memory buffer"<<std::endl;
            exit(1);
        }
        memset(comm_buf, 0, comm_buf_size*buff_unit_size);
        /* register the memory buffer */
        mr_flags = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
        mr = ibv_reg_mr(pd, comm_buf, comm_buf_size*buff_unit_size, mr_flags);
        if (!mr) {
            std::cerr<<"ibv_reg_mr failed with mr_flags="<<mr_flags<<std::endl;
            exit(1);
        }
        /* create queue pair */
        qp_init_attr = (struct ibv_qp_init_attr *)malloc(num_worker_threads*sizeof(struct ibv_qp_init_attr));
        memset(qp_init_attr, 0, num_worker_threads*sizeof(ibv_qp_init_attr));
        for (size_t i=0; i<num_worker_threads; i++)
        {
            qp_init_attr[i].qp_type = IBV_QPT_RC;
            qp_init_attr[i].sq_sig_all = 1;
            qp_init_attr[i].send_cq = cq[i];
            qp_init_attr[i].recv_cq = cq[i];
            qp_init_attr[i].cap.max_send_wr = QUEUE_DEPTH_DEFAULT;
            qp_init_attr[i].cap.max_recv_wr = QUEUE_DEPTH_DEFAULT;
            qp_init_attr[i].cap.max_send_sge = 1;
            qp_init_attr[i].cap.max_recv_sge = 1;
        }
        qp = (struct ibv_qp **)malloc(num_qps_per_thread*num_worker_threads*sizeof(struct ibv_qp *));
        for (size_t i=0; i<num_qps_per_thread*num_worker_threads; i++)
        {
            qp[i] = ibv_create_qp(pd, &qp_init_attr[i/num_qps_per_thread]);
            if (!qp[i])
            {
                std::cerr<<"failed to create QP"<<std::endl;
                exit(1);
            }
            qp_num_revert.insert(std::make_pair(qp[i]->qp_num, i));
        }
    } // init()
}