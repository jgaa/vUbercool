#pragma once

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>

#include "vUbercool.h"

#define WITH_PAGE_COUNTER_SYNCRONIZATION 1

namespace vUbercool {

#pragma pack(push)
#pragma pack(1)
/*! POD data structure for the hosts data */
struct PackedHostData {
    PackedHostData() = default;
    PackedHostData(const PackedHostData&) = default;
    PackedHostData& operator = (const PackedHostData&) = default;

    // Data bits for the hosts. Typically a page counter
    // or index into a data-stoore.
    uint32_t hostbits : 23;

    /*! The C++ class to use. Index into host_factories_
     *   0 = Default (lame)
     *   1 = Static pages
     */
    uint32_t host_template : 5; // 31 alternatives

    // Spare bits
    uint32_t reserved : 4;

};
#pragma pack(pop)

class HostFactory
{
public:
    HostFactory() = default;
    virtual ~HostFactory() = default;

    virtual Host::ptr_t Create(unsigned int id, PackedHostData& data, const Host::Config& config) = 0;
};

namespace impl {

class DefaultHost : public Host
{
public:
    DefaultHost(unsigned int id, PackedHostData& data, const Config& config);

    void ProcessRequest(http::HttpRequest& request,
                        HttpReply& reply) override;

    const Config& GetConfig() const override { return config_; }
protected:
    unsigned IncHitCount() {
#if WITH_PAGE_COUNTER_SYNCRONIZATION
        std::unique_lock<std::mutex> lock(mutex_[id_ % mutex_.size()]);
        return ++data_.hostbits;
#else
        return 0;
#endif
    }

    const unsigned int id_;
    PackedHostData& data_;
    const Config& config_;

#if WITH_PAGE_COUNTER_SYNCRONIZATION
private:
    // Reduce lock-congestion by distributing the locks on sites to multiple mutexes.
    using mutex_array_t = std::array<std::mutex, 129>;
    static mutex_array_t mutex_;
#endif
};

/*! Simple host to serve static pages.
 * The rooot-folder is under "www/static/hostid/
 */
class StaticPageHost : public DefaultHost
{
public:
    StaticPageHost(unsigned int id, PackedHostData& data, const Config& config);

    void ProcessRequest(http::HttpRequest& request,
                        HttpReply& reply) override;

    const Config& GetConfig() const override { return config_; }
private:
    const std::string path_;

    class PageData {
    public:
        PageData(const std::string& path);
        const boost::string_ref GetData() const;
    private:
        boost::interprocess::file_mapping file_mapping_;
        boost::interprocess::mapped_region mm_region_;
        //const std::size_t len_;
    };

    //std::unique_ptr<PageData> page_data_;
};


/*! Template factory implementation */
template <typename T>
class HostFactoryT : public HostFactory
{
public:
    HostFactoryT() = default;

    Host::ptr_t Create(unsigned int id, PackedHostData& data, const Host::Config& config) override {
        Host::ptr_t host { new T(id, data, config) };
        return host;
    }
};

} // impl
} // vUbercool
