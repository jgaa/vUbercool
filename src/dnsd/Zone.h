#pragma once

#include <numeric>
#include <arpa/inet.h>
#include <boost/concept_check.hpp>
#include <boost/regex.hpp>
#include <boost/utility/string_ref.hpp>
#include <boost/algorithm/string.hpp>

#include <war_asio.h>
#include "war_error_handling.h"
#include <log/WarLog.h>


namespace vUbercool {
namespace dns {
namespace impl {


/*! Very simple (for now) in-memory representation of a DNS zone
*/

struct Zone {

    struct ns {
        ns(const std::string& fqdnVal, Zone *z)
        : fqdn{fqdnVal}, zone{z} {}

        const std::string fqdn;
        const Zone *zone = nullptr;

        std::string GetFdqn() const {
            static const std::string dot(".");
            if (zone)
                return fqdn + dot + zone->GetDomainName();
            return fqdn;
        }
    };
    using ns_t = struct ns;

    struct mx : public ns
    {
        mx(const std::string& fqdnVal, uint16_t pri, Zone *z)
        : ns{fqdnVal, z}, priority(pri) {}

        uint16_t priority = 0;
    };
    using mx_t = struct mx;

    struct soa { // defaults from RFC 1035 and usual conventions
        soa(const Zone *z) : zone{z} {}

        const ns_t& GetMname() const { return zone->ns.at(0); }
        std::string rname {"hostmaster"};
        uint32_t serial = 1;
        uint32_t refresh = 7200;
        uint32_t retry = 600;
        uint32_t expire = 3600000;
        uint32_t minimum = 60;

    private:
        const Zone *zone;  // Origin zone for this SOA
    };
    using soa_t = struct soa;

    Zone *parent = nullptr;
    std::string label;
    std::vector<boost::asio::ip::address_v4> a;
    ///std::vector<boost::asio::ip::address_v6> aaaa;
    std::vector<ns_t> ns; // The first entry is assumed to be the primary DNS
    std::vector<mx_t> mx;
    std::string cname; //fqdn
    std::unique_ptr<soa_t> soa;
    std::vector<std::unique_ptr<Zone>> children;
    bool authorative = true;

    Zone() = default;
    Zone(const Zone&) = delete;
    Zone& operator = (const Zone&) = delete;

    Zone(std::string label, Zone *parentZone = nullptr, bool authorative = true)
    : parent{parentZone}, label{move(label)}, authorative{authorative} {}

    void AddChild(std::unique_ptr<Zone>&& child) {
        child->parent = this;
        children.push_back(move(child));
    }

    std::string GetDomainName() const
    {
        std::string name = label;
        for(Zone *z = this->parent; z; z = z->parent) {
            name += '.';
            name += z->label;
        }

        return name;
    }

    std::ostream& Print(std::ostream& out, int level = 0) const
    {
        out << std::setw(level * 2) << '{' << std::setw(0) << '\"' << label
            << '\"'<< std::endl;
        for(const auto& zone : children) {
            zone->Print(out, level + 1);
        }
        out << std::setw(level * 2) << '}' << std::setw(0) << std::endl;
        return out;
    }
};

class ZoneMgr {
public:
    using zones_t = std::vector<std::unique_ptr<Zone>>;
    using key_t = const std::vector<boost::string_ref>;
    using it_t = key_t::const_reverse_iterator;

    ZoneMgr() = default;

    /*! Lookup a zone recursively, starting from the back.
     *
     * \note An empty label in the zone-tree matches all names
     *      at that level. If there is an ambiugity, with
     *      a matching name /and/ an empty label,
     *      the full match is used.
     */
    Zone *Lookup(const key_t& key, bool *authorative = nullptr) {
        return Lookup(key.rbegin(), key.rend(), zones_, authorative);
    }

    void AddRootZone(std::unique_ptr<Zone> zone) {

        if (war::log::LogEngine::IsRelevant(war::log::LL_TRACE1,
                                            war::log::LA_GENERAL)) {
            std::ostringstream out;
            zone->Print(out, 0);

            LOG_TRACE1_FN << "Adding zone: \n" << out.str();
        }

        zones_.push_back(move(zone));
    }

private:
    zones_t zones_;

    Zone *Lookup(it_t label, it_t end, zones_t& zones, bool *authorative) {
        if(label == end) {
            LOG_TRACE4_FN << "Returning null. Called with end";
            return nullptr;
        }

        LOG_TRACE4_FN << "Searching for " << war::log::Esc(*label);

        Zone *wildchard = nullptr;
        Zone *match = nullptr;

        for(const auto & zone : zones) {
            if (zone->label.empty()) {
                wildchard = zone.get();
                LOG_TRACE4_FN << "Matched wildchard zone" << war::log::Esc(wildchard->label);
            }
            if (boost::iequals(*label, zone->label)) {
                match = zone.get();
                LOG_TRACE4_FN << "Matched [full] zone" << war::log::Esc(match->label);
                break;
            }
        }

        Zone *rval = match ? match : wildchard;

        if (authorative && rval) {
            *authorative = rval->authorative;
        }

        // If we found something, and have more labels to search, recurse further
        if (rval && (++label != end)) {
            return Lookup(label, end, rval->children, authorative);
        }

        LOG_TRACE4_FN << "Returning " << (rval ? war::log::Esc(rval->label) : std::string("[null]"));
        return rval; // Return whatever we found, if anything
    }
};

} // impl
} // dns
} // vUbercoo

