#include "params.hpp"
#include <boost/program_options.hpp>

namespace po = boost::program_options;

namespace omnireduce {
    std::unordered_map<uint32_t, uint32_t> qp_num_revert {};
    std::unordered_map<uint32_t, uint32_t> qp_num_to_peerid {};
    omnireduce_params omnireduce_par;

    void parse_parameters()
    {
        std::string config_file;
        std::ifstream ifs;
        uint32_t num_workers, num_aggregators, num_threads, message_size, block_size;
        int ib_port, gid_idx, sl;
        std::string worker_ip_str, aggregator_ips_str;
        po::options_description omnireduce_options("OmniReduce options");
        po::options_description config_file_options;
        omnireduce_options.add_options()
            ("omnireduce.num_workers", po::value<uint32_t>(&num_workers)->default_value(1), "Number of workers")
            ("omnireduce.num_aggregators", po::value<uint32_t>(&num_aggregators)->default_value(1), "Number of workers")
            ("omnireduce.num_threads", po::value<uint32_t>(&num_threads)->default_value(1), "Number of threads")
            ("omnireduce.message_size", po::value<uint32_t>(&message_size)->default_value(1024), "Message size")
            ("omnireduce.block_size", po::value<uint32_t>(&block_size)->default_value(1024), "Block size")
            ("omnireduce.ib_port", po::value<int>(&ib_port)->default_value(1), "IB port")
            ("omnireduce.gid_idx", po::value<int>(&gid_idx)->default_value(2), "GID")
            ("omnireduce.sl", po::value<int>(&sl)->default_value(2), "Service level")
            ("omnireduce.worker_ips", po::value<std::string>(&worker_ip_str)->default_value("10.0.0.1"), "Ip addresses of workers")
            ("omnireduce.aggregator_ips", po::value<std::string>(&aggregator_ips_str)->default_value("10.0.0.1"), "Ip addresses of aggregators");
        config_file_options.add(omnireduce_options);
        config_file = "/etc/omnireduce.cfg";
        ifs.open(config_file.c_str());
        if(!ifs.good()){
            ifs.close();
            config_file = "omnireduce.cfg";
            ifs.open(config_file.c_str());
            if(!ifs.good()){
                ifs.close();
                std::cerr<<"No config file found!"<<std::endl;
                exit(1); 
            }
        }
        po::variables_map vm;
        po::store(po::parse_config_file(ifs, config_file_options), vm);
        po::notify(vm);
        //std::cout<<"num workers: "<<num_workers<<"; num aggregators: "<<num_aggregators<<std::endl;
        //std::cout<<"num threads: "<<num_threads<<"; message size: "<<message_size<<"; block size: "<<block_size<<std::endl;
        //std::cout<<"worker ips: "<<worker_ip_str<<std::endl;
        //std::cout<<"aggregator ips: "<<aggregator_ips_str<<std::endl;
        omnireduce_par.setNumWorkerThreads(num_threads);
        omnireduce_par.setNumWorkers(num_workers);
        omnireduce_par.setNumAggregators(num_aggregators);
        omnireduce_par.setMessageSize(message_size);
        omnireduce_par.setBlockSize(block_size);
        omnireduce_par.setWorkerIps(worker_ip_str);
        omnireduce_par.setAggregatorIps(aggregator_ips_str);
        omnireduce_par.setNumSlotsPerThread();
        uint32_t num_blocks_per_thread = omnireduce_par.getNumSlotsPerTh()*(message_size/block_size);
        omnireduce_par.setPrepostRecvNum(QUEUE_DEPTH_DEFAULT/omnireduce_par.getNumSlotsPerTh());
        omnireduce_par.setInfOffset(num_blocks_per_thread);
        omnireduce_par.setIbPort(ib_port);
        omnireduce_par.setGidIdx(gid_idx);
        omnireduce_par.setServiceLevel(sl);
    }
    omnireduce_params::omnireduce_params() {
        buff_unit_size = 4;
        num_worker_threads = 8;
        num_workers = 2;
        num_aggregators = 2;
        num_qps_per_aggregator_per_thread = 1;
        message_size = 1024;
        block_size = 256;
        num_comm_buff = 256;
        ib_port = 1;
        gid_idx = 2;
        sl = 2;
    }
    omnireduce_params::~omnireduce_params() {}
}