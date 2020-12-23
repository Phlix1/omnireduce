#include "aggcontext.hpp"
#include "omnireduce.hpp"


namespace omnireduce {
    void *OmniAggregatorMaster(void *ctx) {
        AggContext* d_ctx_ptr = (AggContext *) ctx;

        d_ctx_ptr->ret = aggmaster(d_ctx_ptr);

        return NULL;
    }
    AggContext::AggContext() :
            num_server_threads (1) {
        threadid.store(0);
        init();
        StartMaster();
    }
    void AggContext::StartMaster() {
        pthread_attr_t attr;
        cpu_set_t cpus;
        pthread_attr_init(&attr);
        CPU_ZERO(&cpus);
        CPU_SET(8, &cpus);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpus);
        if (pthread_create(&aggmasterThread, &attr, OmniAggregatorMaster, this)) {
            std::cerr<<"Error starting aggregator master thread"<<std::endl;
            exit(1);
        }
        while (!force_quit);
    }
    void AggContext::StopMaster() {
        force_quit = true;
        int join_ret = pthread_join(aggmasterThread, NULL);
        if (join_ret) {
            std::cerr<<"Error joining master thread: returned"<<std::endl;
            exit(1);            
        }
        if (this->ret < 0) {
            std::cerr<<"Master thread returned"<<std::endl;
            exit(1);              
        }        
    }
    AggContext::~AggContext() {
        StopMaster();
    }
    void AggContext::init() {
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
        uint32_t num_workers = omnireduce_par.getNumWorkers();
        uint32_t num_qps_per_aggregator_per_thread = omnireduce_par.getNumQpsPerAggTh();
        uint32_t num_qps_per_thread = num_qps_per_aggregator_per_thread*num_workers;
        uint32_t num_slots_per_thread = omnireduce_par.getNumSlotsPerTh();
        uint32_t message_size = omnireduce_par.getMessageSize();
        uint32_t num_comm_buff = omnireduce_par.getNumCommbuff();
        num_server_threads = omnireduce_par.getNumWorkerThreads();
        size_t comm_buf_size = 0;
        remote_props_array = (struct remote_con_data_t *)malloc(num_workers*sizeof(struct remote_con_data_t));
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
        cq = (struct ibv_cq **)malloc(num_server_threads*sizeof(struct ibv_cq *));	
        for (size_t i=0; i<num_server_threads; i++)
        {
            cq[i] = ibv_create_cq(ib_ctx, cq_size, NULL, NULL, 0);
            if (!cq[i])
            {
                std::cerr<<"failed to create CQ with "<<cq_size<<" entries"<<std::endl;
                exit(1);
            }
        }
        /* allocate the memory worker send/recv buffer */
        comm_buf_size = num_slots_per_thread*(message_size*2)*num_server_threads*(num_comm_buff+num_workers)+num_slots_per_thread*message_size*num_server_threads*2;
        ret = posix_memalign(reinterpret_cast<void**>(&comm_buf), cycle_buffer, comm_buf_size*buff_unit_size);
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
        qp_init_attr = (struct ibv_qp_init_attr *)malloc(num_server_threads*sizeof(struct ibv_qp_init_attr));
        memset(qp_init_attr, 0, num_server_threads*sizeof(ibv_qp_init_attr));
        for (size_t i=0; i<num_server_threads; i++)
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
        qp = (struct ibv_qp **)malloc(num_qps_per_thread*num_server_threads*sizeof(struct ibv_qp *));
        for (size_t i=0; i<num_qps_per_thread*num_server_threads; i++)
        {
            qp[i] = ibv_create_qp(pd, &qp_init_attr[i/num_qps_per_thread]);
            if (!qp[i])
            {
                std::cerr<<"failed to create QP"<<std::endl;
                exit(1);
            }
            qp_num_revert.insert(std::make_pair(qp[i]->qp_num, i));
        }
    }// init
}