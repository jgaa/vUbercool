
#include <unistd.h>
#include <signal.h>

#include <boost/program_options.hpp>
#include <boost/property_tree/info_parser.hpp>
#include <boost/filesystem.hpp>

#include "vUbercool.h"
#include "Statistics.h"

#include "log/WarLog.h"
#include "war_error_handling.h"
#include "war_boost_ptree_helper.h"

using namespace std;
using namespace war;
using namespace vUbercool;

struct Configuration
{
    int num_io_threads {0};
    int max_io_thread_queue_capacity {1024 * 64};
    string http_config_file {"/etc/vubercool/http.conf"};
    string dns_config_file {"/etc/vubercool/dns.conf"};
    bool daemon  {false};
    bool recreate_mmap_file {false};
    int stats_update_seconds {60};
};

bool ParseCommandLine(int argc, char *argv[], log::LogEngine& logger, Configuration& conf)
{
    namespace po = boost::program_options;

    po::options_description general("General Options");

    general.add_options()
        ("help,h", "Print help and exit")
        ("console-log,C", po::value<string>()->default_value("NOTICE"),
            "Log-level for the console-log")
        ("log-level,L", po::value<string>()->default_value("NOTICE"),
            "Log-level for the log-file")
        ("log-file", po::value<string>()->default_value("vubercool.log"),
            "Name of the log-file")
        ("truncate-log", po::value<bool>()->default_value(true),
            "Truncate the log-file if it already exists")
        ("daemon", po::value<bool>(&conf.daemon), "Run as a system daemon")
        ("recreate-mmap-file", po::value<bool>(&conf.recreate_mmap_file)->default_value(
            conf.recreate_mmap_file), "Re-create the memmap file that backs the data for the sites")
        ("stats-update-seconds", po::value<int>(&conf.stats_update_seconds)->default_value(
            conf.stats_update_seconds), "Interval between statistics summaries in the log. 0 to disable.")
        ;

    po::options_description performance("Performance Options");
    performance.add_options()
        ("io-threads", po::value<int>(&conf.num_io_threads)->default_value(
            conf.num_io_threads),
            "Number of IO threads. If 0, a reasonable value will be used")
        ("io-queue-size", po::value<int>(&conf.max_io_thread_queue_capacity)->default_value(
            conf.max_io_thread_queue_capacity),
            "Capacity of the IO thread queues (max number of pending tasks per thread)")
        ;

    po::options_description conffiles("Configuration Files");
    conffiles.add_options()
        ("http-config",  po::value<string>(&conf.http_config_file)->default_value(
            conf.http_config_file),
            "Full path to the http configuration-file")
        ("dns-config",  po::value<string>(&conf.dns_config_file)->default_value(
            conf.dns_config_file),
            "Full path to the dns configuration-file")
        ;

    po::options_description cmdline_options;
    cmdline_options.add(general).add(performance).add(conffiles);

    po::variables_map vm;
    po::store(po::parse_command_line(argc, argv, cmdline_options), vm);
    po::notify(vm);

    if (vm.count("help")) {
        cout << cmdline_options << endl
            << "Log-levels are:" << endl
            << "   FATAL ERROR WARNING INFO NOTICE DEBUG " << endl
            << "   TRACE1 TRACE2 TRACE3 TRACE4" << endl;

        return false;
    }

    if (!conf.daemon && vm.count("console-log")) {
        logger.AddHandler(make_shared<log::LogToStream>(cout, "console",
            log::LogEngine::GetLevelFromName(vm["console-log"].as<string>())));
    }

    if (vm.count("log-level")) {
        logger.AddHandler(make_shared<log::LogToFile>(
            vm["log-file"].as<string>(),
            vm["truncate-log"].as<bool>(),
            "file",
            log::LogEngine::GetLevelFromName(vm["log-level"].as<string>())));
    }

    return true;
}

using ip_list_t = vector<pair<string /* host */, string /* port */>>;


void ParseHttpConfig(const string& path, HttpConfig& conf,
                     ip_list_t& iplist)
{
   boost::property_tree::ptree pt;
   boost::property_tree::read_info(path, pt);

   auto http = pt.get_child("http");

   conf.num_sites = http.get<HttpConfig::site_id_t>("num-sites");
   conf.root_path = http.get<string>("root-path");
   conf.data_mmap_file = http.get<string>("data-mmap-file");
   conf.connection_timeout = http.get<unsigned>("connection-timeout", conf.connection_timeout);
   conf.max_connections = http.get<unsigned>("max-connections", conf.max_connections);

   CopyToContainer<string>(http.get_child("network"), iplist);

   CopyToMap<HttpConfig::site_id_t>(http.get_child("aliases"),
                                          conf.site_aliases);

   CopyToMap<string>(http.get_child("redirects"), conf.http_redirects);

   if (auto compression = http.get_child_optional("compression")) {
        conf.allow_deflate = compression->get_child_optional("deflate");
        conf.allow_gzip = compression->get_child_optional("gzip");
   }
}



int main(int argc, char *argv[])
{
    /* Enable logging.
     */
    log::LogEngine logger;
    Configuration configuration;

    if (!ParseCommandLine(argc, argv, logger, configuration))
        return -1;

    LOG_INFO << "vUbercoold "
        << static_cast<int>(vUbercool::ApplicationVersion::MAJOR)
        << '.'
        << static_cast<int>(vUbercool::ApplicationVersion::MINOR)
        << " starting up";

    if (configuration.daemon) {
        LOG_INFO << "Switching to system daemon mode";
        daemon(1, 0);
    }

    try {

        Threadpool thread_pool(configuration.num_io_threads,
                               configuration.max_io_thread_queue_capacity);

        StatisticsManager::ptr_t stats_manager;
        VhostManager::ptr_t vhost_manager;
        HttpDaemon::ptr_t http_server;
        DnsDaemon::ptr_t dns_server;

        stats_manager = StatisticsManager::Create(thread_pool,
                                                  configuration.stats_update_seconds);
        {
            HttpConfig http_config;
            ip_list_t http_iplist, dns_iplist;

            http_config.recreate_mmap_file = configuration.recreate_mmap_file;
            ParseHttpConfig(configuration.http_config_file, http_config, http_iplist);
            vhost_manager = VhostManager::Create(thread_pool, http_config);

            http_server = HttpDaemon::Create(thread_pool, *vhost_manager);

            boost::property_tree::ptree dns_pt;
            boost::property_tree::read_info(configuration.dns_config_file, dns_pt);
            CopyToContainer<string>(dns_pt.get_child("network"), dns_iplist);

            dns_server = DnsDaemon::Create(thread_pool, *vhost_manager, dns_pt);

            for(const auto host : http_iplist) {
                http_server->StartAcceptingAt(host.first, host.second);
            }

            for(const auto host : dns_iplist) {
                dns_server->StartReceivingUdpAt(host.first, host.second);
            }
        }

        /* We now put the main-tread to sleep.
         *
         * It will remain sleeping until we receive one of the signals
         * below. Then we simply shut down the the thread-pool and wait for the
         * threads in the thread-pool to finish.
         */
        io_service_t main_thread_service;
        boost::asio::signal_set signals(main_thread_service, SIGINT, SIGTERM,
            SIGQUIT);
        signals.async_wait([&thread_pool](boost::system::error_code /*ec*/,
                                          int signo) {

            LOG_INFO << "Reiceived signal " << signo << ". Shutting down";
            thread_pool.Close();
        });
        main_thread_service.run();

        // Wait for the show to end
        thread_pool.WaitUntilClosed();

    } catch(const war::ExceptionBase& ex) {
        LOG_ERROR_FN << "Caught exception: " << ex;
        return -1;
    } catch(const boost::exception& ex) {
        LOG_ERROR_FN << "Caught boost exception: " << ex;
        return -1;
    } catch(const std::exception& ex) {
        LOG_ERROR_FN << "Caught standad exception: " << ex;
        return -1;
    } catch(...) {
        LOG_ERROR_FN << "Caught UNKNOWN exception!";
        return -1;
    };

    LOG_INFO << "So Long, and Thanks for All the Fish!";

    return 0;
}
