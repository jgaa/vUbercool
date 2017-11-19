
#include <boost/utility/string_ref.hpp>
#include <boost/filesystem.hpp>

#include "Hosts.h"
#include "../httpd/HttpRequest.h"
#include <warlib/WarLog.h>
#include <warlib/error_handling.h>

using namespace war;
using namespace std;

namespace vUbercool {
namespace impl {

StaticPageHost::StaticPageHost(unsigned int id, PackedHostData& data,
                                const Config& config
)
: DefaultHost(id, data, config),
path_{config.root_path + "/static/" + to_string(id)}
{
}

void StaticPageHost::ProcessRequest(http::HttpRequest& request,
                                    HttpReply& reply) {

    static const char default_page[] = {"index.html"};
    std::string page = request.uri;
    static const string badbad("..");

    WAR_ASSERT(!page.empty());
    if (page.back() == '/')
        page += default_page;

    if (page.find(badbad)) {
        LOG_WARN << "Possible cracking attempt at URL: " << log::Esc(page)
            << " for site #" << id_
            << request;
        WAR_THROW_T(ExceptionInvalidResource, page);
    }

    IncHitCount(); // Site hit count shared by all uri's.

    if (!boost::filesystem::is_regular_file(page)) {
        LOG_DEBUG_FN << "File not found" << log::Esc(page)
            << ' ' << request;
        WAR_THROW_T(ExceptionInvalidResource, page);
    }

    PageData page_data(page);
    const auto content = page_data.GetData();

    // We will in almost all cases use compression, so it does not make
    // sense to keep the mapped file in memory, unless we implement
    // caching. However, Linux probably does a reasonable good job
    // in that area anyway.
    reply.Out().write(&content[0], content.size());
}

StaticPageHost::PageData::PageData(const std::string& path)
: file_mapping_(path.c_str(), boost::interprocess::read_only),
mm_region_(file_mapping_,
           boost::interprocess::read_only, 0,
           boost::filesystem::file_size(path)) {}

const boost::string_ref StaticPageHost::PageData::GetData() const {
    return boost::string_ref(static_cast<const char *>(mm_region_.get_address()),
                             mm_region_.get_size());
}

}} // namespaces

