
#include <atomic>
#include <unordered_map>

#include "vUbercool.h"
#include "Hosts.h"
#include <boost/asio/spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/regex.hpp>
#include <boost/utility/string_ref.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/filesystem.hpp>

#include <warlib/WarLog.h>
#include <warlib/error_handling.h>
#include <unordered_map>

// Switch for mmap or in-memory only data storage.
// Useful for performance testing in order to measure the
// (eventual) performance impact of using mmap.
#define USE_MMAP_DATA 1

using namespace war;
using namespace std;

namespace vUbercool {
namespace impl {

#if USE_MMAP_DATA
    struct PreAllocateMmapFile
    {
        PreAllocateMmapFile(const string &path, const size_t size, bool force = false)
        {
            bool do_create = force;

            if (!do_create && !boost::filesystem::exists(path)) {
                do_create = true;
            }
            if (!do_create && (boost::filesystem::file_size(path) != size)) {
                do_create = true;
            }

            if (do_create) {
                LOG_NOTICE_FN << "Creating mmap file " << log::Esc(path)
                    << " of size " << size << " bytes.";
                ofstream file(path, ios_base::out | ios_base::trunc | ios_base::binary);
                file.seekp(size -1);
                file << '\0';
            } else {
                LOG_NOTICE_FN << "Using the existing mmap file " << log::Esc(path);
            }
        }
    };
#endif

    /*! Memory optimized host manager with all the hosts in
     * memory.
     */
    class VectorVhostManager : public VhostManager
#if USE_MMAP_DATA
    , public PreAllocateMmapFile
#endif
    {
    public:
        VectorVhostManager(Threadpool& /*tp*/, const HttpConfig& config)
#if USE_MMAP_DATA
        : PreAllocateMmapFile(config.data_mmap_file.c_str(),
                              config.num_sites * sizeof(PackedHostData),
                              config.recreate_mmap_file),
        file_mapping_(config.data_mmap_file.c_str(), boost::interprocess::read_write),
        mm_region_(file_mapping_,
                   boost::interprocess::read_write,
                   0, config.num_sites * sizeof(PackedHostData)),
#else
        : data_container_(config.num_sites),
#endif
        config_(config),
        host_config_(config_.root_path)
        {
            LOG_INFO_FN << "The size of the common data-object is "
            << sizeof(PackedHostData) << " bytes.\n"
                << "I will use " << config_.num_sites
                << " hosts, which will take up "
                << (((config_.num_sites * sizeof(PackedHostData)) / 1024) / 1024)
                << " megabytes of memory";

#if USE_MMAP_DATA
            // Use a memory-mapped file for data
            data_ = (data_t)mm_region_.get_address();
#else
            data_ = &data_container_[0];
#endif

            // Host type #0 The default (lame) one
            host_factories_.push_back(host_factory_t{
                new HostFactoryT<DefaultHost>()
            });

            // Host type #1 Static pages loaded from disk.
            host_factories_.push_back(host_factory_t{
                new HostFactoryT<StaticPageHost>()
            });

        }

        Host::ptr_t GetHost(const std::string& name) override {

            LOG_TRACE1_FN << "Resolving host: " << log::Esc(name);

            // First, strip off any ':port#' segment from the host-name.
            size_t pos = name.find(":");
            const boost::string_ref host_name(&name[0], (pos == name.npos ? name.size() : pos));
            const auto ix = GetHostIndex(host_name);

            if (ix >= config_.num_sites) {
                WAR_THROW_T(ExceptionUnknownHost, name);
            }

            PackedHostData& host_data = data_[ix];

            LOG_TRACE1_FN << "Processing request using site-template "
                "(generator)# " << host_data.host_template
                << " on site index " << ix;


            // TODO: Optimize the memory model for the Hosts (cache)
            return host_factories_[host_data.host_template]->Create(
                ix, host_data, host_config_);
        }

        const HttpConfig& GetConfig() const override {
            return config_;
        }

    private:
        using data_t = PackedHostData *;

        HttpConfig::site_id_t GetHostIndex(const boost::string_ref& name) {

            const auto dot = name.find('.');
            if (dot != name.npos) {

                const boost::string_ref digit(&name[0], dot);

                try {
                    // TODO: Add check to see that the rest of the name
                    //  matches the domain-name for the number-indexed
                    //  sites.
                    return static_cast<int>(
                        boost::lexical_cast<std::uint32_t>(digit));
                } catch(const boost::bad_lexical_cast&) {
                    ; // Do nothing
                }
            }

            return GetHostIndexFromName(name);
        }

        HttpConfig::site_id_t GetHostIndexFromName(const boost::string_ref& name) {
            // Try name lookup
            const std::string str_name(&name[0], name.size());

            {
                auto const val = config_.site_aliases.find(str_name);
                if (val != config_.site_aliases.end()) {
                    LOG_TRACE1_FN << "Resolved alias: " << log::Esc(str_name)
                        << " --> #" << val->second;
                    return val->second;
                }
            }

            {
                auto const val = config_.http_redirects.find(str_name);
                if (val != config_.http_redirects.end()) {
                    LOG_TRACE1_FN << "Redirecting: " << log::Esc(str_name)
                        << " --> " << log::Esc(val->second);
                    throw ExceptionRedirect(val->second);
                }
            }

            LOG_TRACE1_FN << "Failed to resolve name " << log::Esc(str_name);
            WAR_THROW_T(ExceptionUnknownHost, str_name);
        }

        // Container for the hosts data.
        // Since we are likely to have /many/ hosts, the
        // obligatory data footprint is a minimal as
        // possible.
        data_t data_;

        using host_factory_t = std::unique_ptr<HostFactory>;
        std::vector<host_factory_t> host_factories_;

#if USE_MMAP_DATA
        boost::interprocess::file_mapping file_mapping_;
        boost::interprocess::mapped_region mm_region_;
#else
        std::vector<PackedHostData> data_container_;;
#endif
        const HttpConfig config_;
        const Host::Config host_config_;
    };

} // impl

VhostManager::ptr_t VhostManager::Create(Threadpool& tp, HttpConfig& config)
{
    vUbercool::VhostManager::ptr_t manager{ new vUbercool::impl::VectorVhostManager(tp,
        config) };
    return manager;
}

} // vUbercool

