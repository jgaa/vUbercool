#pragma once

#include <numeric>
#include <arpa/inet.h>
#include <boost/concept_check.hpp>
#include <boost/regex.hpp>
#include <boost/utility/string_ref.hpp>
#include <warlib/error_handling.h>

#include "Zone.h"

std::ostream& operator << (std::ostream& out, const std::vector<boost::string_ref>& v);

namespace vUbercool {
namespace dns {
namespace impl {

// Split "domainName" into it's fragments (items separated with dots)
std::vector<boost::string_ref> ToFraments(const std::string& domainName)  {
    std::vector<boost::string_ref> frags;

    const char *p = domainName.c_str();
    const auto end = p + domainName.size();

    while(p != end) {
        const auto dot = std::find(p, end, '.');

        frags.push_back(boost::string_ref(p, dot - p));

        p = dot;
        if (p != end)
            ++p;
    }

    return frags;
}

// For debugging - no sanity checks!
std::string GetNameFromOffset(uint16_t offset, const char *start) {
    std::string name;

    for(;;) {
        uint16_t my_offset = offset & ~0xC000;
        const char *pos = reinterpret_cast<const char *>(start + my_offset);
        uint8_t len = *reinterpret_cast<const uint8_t *>(pos);
        if (!len)
            break;

        if (!name.empty())
            name += '.';

        if ((len & 0xC0) == 0xC0) {
            const uint16_t ofs = ntohs(*reinterpret_cast<const uint16_t *>(pos)) & ~0xC000;
            return name + GetNameFromOffset(ofs, start);
        } else if (len && (len <= 64)) {
            const boost::string_ref label(pos + 1, len);
            name += std::string(label);
            offset = my_offset + (len + 1);
        } else {
            break; // error
        }
    }

    return name;
}

// See RFC1035 4.1.1 Header section format
class MessageHeader
{
public:
    struct MessageHeaderException : public war::ExceptionBase {};
    enum class Rcode : uint8_t {
        OK = 0,
        FORMAT_ERROR = 1,
        SERVER_FAILURE = 2,
        NAME_ERROR = 3,
        NOT_IMPLEMENTED = 4,
        REFUSED = 5
    };

    MessageHeader(const char *buffer, std::size_t size)
    {
        if (size < GetSize()) {
            WAR_THROW_T(MessageHeaderException, "Header-length underflow");
        }

        const uint16_t *p = reinterpret_cast<const uint16_t *>(buffer);

        id_ = ntohs(*p++);
        flags_ = ntohs(*p++);
        qdcount_ = ntohs(*p++);
        ancount_ = ntohs(*p++);
        nscount_ = ntohs(*p++);
        arcount_ = ntohs(*p++);
    }

    MessageHeader(const MessageHeader& v)
    : id_{v.id_}, flags_{v.flags_}, qdcount_{v.qdcount_},
    ancount_{v.ancount_}, nscount_{v.nscount_},
    arcount_{v.arcount_}
    {
    }

    /*! Get the length of the header */
    size_t GetSize() const { return 2 * 6; }

    /*! Get the ID in native byte order */
    uint16_t GetId() const { return id_; }

    bool GetQr() const { return (flags_ & (static_cast<uint16_t>(1) << 15)) != 0; }
    uint8_t GetOpcode() const { return (flags_ >> 11) & 0xF; }
    bool GetAa() const { return (flags_ & (1 << 10)) != 0; }
    bool GetTc() const { return (flags_ & (1 << 9)) != 0; }
    bool GetRd() const { return (flags_ & (1 << 8)) != 0; }
    bool GetRa() const { return (flags_ & (1 << 7)) != 0; }
    uint8_t GetZ() const { return (flags_ >> 4) & 0xF; }
    uint8_t GetRcode() const { return flags_ & 0xF; }
    uint16_t GetQdCount() const { return qdcount_; }
    uint16_t GetAnCount() const { return ancount_; }
    uint16_t GetNsCount() const { return nscount_; }
    uint16_t GetArCount() const { return arcount_; }

    void SetQr(bool qr) {
        flags_ &= ~(1 << 15);
        if (qr)
            flags_ |= static_cast<uint16_t>(1) << 15;
    }

    void SetTc(bool tc) {
        flags_ &= ~(1 << 9);
        if (tc)
            flags_ |= static_cast<uint16_t>(1) << 9;
    }

    void SetRa(bool ra) {
        flags_ &= ~(1 << 7);
        if (ra)
            flags_ |= static_cast<uint16_t>(1) << 7;
    }

    void SetAa(bool aa) {
        flags_ &= ~(1 << 10);
        if (aa)
            flags_ |= static_cast<uint16_t>(1) << 10;
    }

    void SetOpcode(uint8_t opcode) {
        flags_ &= ~(0xF << 11);
        flags_ |= (opcode & 0xF) << 11;
    }

    void SetRcode(Rcode opcode) {
        flags_ &= ~0xF;
        flags_ |= (static_cast<uint16_t>(opcode) & 0xF);
    }

    void SetQdCount(uint16_t val) { qdcount_ = val; }
    void SetAnCount(uint16_t val) { ancount_ = val; }
    void SetNsCount(uint16_t val) { nscount_ = val; }
    void SetArCount(uint16_t val) { arcount_ = val; }

    void ResetAllCounters() {
        qdcount_ = ancount_ = nscount_ = arcount_ = 0;
    }

    void Write(char *buffer) const {
        uint16_t *p = reinterpret_cast<uint16_t *>(buffer);

        *p++ = htons(id_);
        *p++ = htons(flags_);
        *p++ = htons(qdcount_);
        *p++ = htons(ancount_);
        *p++ = htons(nscount_);
        *p++ = htons(arcount_);
    }

private:
    std::uint16_t id_ = 0;
    std::uint16_t flags_ = 0;
    std::uint16_t qdcount_ = 0;
    std::uint16_t ancount_ = 0;
    std::uint16_t nscount_ = 0;
    std::uint16_t arcount_ = 0;
};

class LabelHeader
{
public:
    struct LabelHeaderException : public war::ExceptionBase {};
    struct NoLabelsException : public LabelHeaderException {};
    struct IllegalLabelException : public LabelHeaderException {};
    struct IllegalPointereException: public LabelHeaderException {};

    using labels_t = std::vector<boost::string_ref>;

    LabelHeader(const char *buffer, // Buffer for this segment
        std::size_t size, // Buffer-size for this segment (and may be more)
        labels_t& knownLabels, const char *messageBuffer)
    : message_buffer_{messageBuffer}
    {
        if (size < 1) {
            WAR_THROW_T(LabelHeaderException, "Label-length underflow");
        }

        const char *end = buffer + size;
        const char *p = buffer;
        bool is_traversing_pointer = false;
        while(true) {
            // *p is the length-field in the next label.
            if (*p == 0) {
                ++size_;
                if (!is_traversing_pointer) {
                    ++buffer_size_;
                }
                ValidateSize();

                // Root label. We are basically done
                if (names_.empty()) {
                    WAR_THROW_T(NoLabelsException, "No labels found in header");
                }
                break;
            }
            const size_t len = static_cast<uint8_t>(*p);
            if (len < 64) {
                // Normal label with the name as bytes

                const char *label_end = ++p + len;
                if (label_end > end) {
                    WAR_THROW_T(LabelHeaderException,
                        "Buffer underflow in label (potential hostile request)");
                }

                size_ += len;
                ValidateSize();

                if (!is_traversing_pointer) {
                    buffer_size_ += len + 1;
                }

                boost::string_ref label{p, len};

                if (!is_traversing_pointer) {
                    // If we are traversing a pointer, the label is already known.
                    knownLabels.push_back(label);
                }
                names_.push_back(label);

                // Validate the name
                static const boost::regex pat{ R"([a-z0-9\.\-]+)",
                    boost::regex::icase | boost::regex::optimize};
                if (!boost::regex_match(p, p + len, pat)) {
                    WAR_THROW_T(IllegalLabelException, "Invalid char in label");
                }

                WAR_ASSERT(p < label_end);
                p = label_end; // Points to the first byte in the next label
            } else if ((len & 0xC0) == 0xC0) {
                // Compressed label 2 byte header
                if ((p + 1) >= end) {
                    WAR_THROW_T(LabelHeaderException,
                       "Buffer underflow in label (potential hostile request");
                }

                size_ += 2;
                ValidateSize();

                if (!is_traversing_pointer) {
                    buffer_size_ += 2;
                }

                const size_t pointer = *reinterpret_cast<const uint16_t *>(p) & ~0xC000;
                names_.push_back(FindPointer(pointer, knownLabels));

                // Point to the item after the resolved pointer.
                is_traversing_pointer = true;
                p = names_.back().end();

            } else {
                WAR_THROW_T(LabelHeaderException, "Invalid label length!");
            }
        }
    }

    size_t GetSize() const { return buffer_size_; }

    /*! Return the calculated size of the labels. Max 255.

        TODO: Check if this includes the size header of each label.
     */
    size_t GetLabelSize() const { return size_; }

    /*! Return a normal domain name as a string, starting with the first label */
    std::string GetDomainName() const {
        static const std::string empty;
        static const std::string dot{"."};
        std::ostringstream out;
        int cnt = 0;
        for(auto & label : names_) {
            out << (++cnt == 1 ? empty : dot) << label;
        }

        return out.str();
    }

    const labels_t& GetLabels() const { return names_; }

private:
    boost::string_ref FindPointer(uint16_t pointer, const labels_t& labels)
    {
        // The pointer is an offset into the message buffer
        const char *key = message_buffer_ + pointer;
        for(const auto& v : labels) {
            if (v.begin() == key) {
                return v;
            }
        }

        WAR_THROW_T(IllegalPointereException,
            "Invalid label pointer (potential hostile request)!");
    }

    void ValidateSize() {
        if (size_ > 255) {
            WAR_THROW_T(LabelHeaderException, "The labels exeeds the 255 bytes limit");
        }
    }

    labels_t names_;
    std::size_t size_ = 0;
    std::size_t buffer_size_ = 0; // Bytes used by the labes in /this/ buffer
    const char *message_buffer_;
};

// See RFC1035 4.1.2. Question section format
class Question : public LabelHeader {
public:
    struct QuestionHeaderException : public war::ExceptionBase {};

//     Question(const Question& v)
//     : qtype_{v.qtype_}, qclass{v.qclass}
//     {}

    Question(const char *buffer,
        std::size_t size,
        labels_t& knownLabels,
        const char *messageBuffer,
        uint16_t offsetIntoReplyBuffer)
    : LabelHeader(buffer, size, knownLabels, messageBuffer),
        offset_(offsetIntoReplyBuffer)
    {
        const char *end = buffer + size;
        const char *start = buffer + LabelHeader::GetSize();
        const uint16_t *p = reinterpret_cast<const uint16_t *>(start);

        if ((start + 4) > end) {
            WAR_THROW_T(QuestionHeaderException, "Buffer underflow");
        }

        qtype_ = ntohs(*p++);
        qclass = ntohs(*p++);
    }

    std::size_t GetSize() const {
        return LabelHeader::GetSize() + 4;
    }

    std::uint16_t GetQtype() const { return qtype_; }
    std::uint16_t GetQclass() const { return qclass; }
    std::uint16_t GetOffset() const { return offset_; }


private:
    std::uint16_t qtype_ = 0;
    std::uint16_t qclass = 0;
    std::uint16_t offset_ = 0;
};

/*! Common part of a RR entry
 */
class AnswerBase
{
public:
    using buffer_t = std::vector<char>;
    using existing_labels_t = std::vector<uint16_t>;

    AnswerBase(const std::string& name,
               uint16_t rType,
               uint16_t rDlength,
               existing_labels_t& existingLabels)
    : name_{name}, type_{rType}, rdlength_{rDlength},
    label_start_points_{existingLabels}
    {
    }

    AnswerBase(uint16_t namePtr,
               uint16_t rType,
               uint16_t rDlength,
               existing_labels_t& existingLabels)
    : name_ptr_(namePtr | 0xC000), type_{rType}, rdlength_{rDlength},
    label_start_points_{existingLabels}
    {
    }

    virtual ~AnswerBase() {}

    /*! Undo changes done by tle latest Write
     * \param buffer The buffer the write was applied to
    */
    void Revert(buffer_t& buffer) {
        buffer.resize(orig_len_);
    }

    void WriteHeader(buffer_t& buffer)
    {
        orig_len_ = buffer.size();

        // We can use name_ or name_ptr. Not both at the same time.
        WAR_ASSERT(name_.empty() || name_ptr_);
        WAR_ASSERT(!name_.empty() && !name_ptr_);

        if (!name_.empty()) {
            WriteDomainName(buffer, name_, nullptr);
        }

        size_t remaining = 10; // Bytes remaining to be written
        if (name_ptr_) {
            remaining += 2;
        }

        buffer.resize(buffer.size() + remaining);
        char *last_segment = &buffer[buffer.size() - remaining];

        // Write the numbers
        uint16_t *p = reinterpret_cast<uint16_t *>(last_segment);

        if (name_ptr_) {
            LOG_TRACE3_FN << "Writing name_ptr_: " << (name_ptr_ & ~0xC000);
            *p++ = htons(name_ptr_);
            AddLabelForCompression(name_ptr_, &buffer[0]);
        }

        *p++ = htons(type_);
        *p++ = htons(class_);
        *reinterpret_cast<uint32_t *>(p) = htonl(ttl_);

        rdlength_hdr_ = buffer.size() - 2;
        WriteRdlength(buffer);

        // Update size_ with the fixed bytes at the end of this section;
        size_ = buffer.size() - orig_len_;
    }

    /*! Can only be called after Write(), as the size is dynamic
     *
     * \return The bytes used by the answer RR, including the RR header.
     */
    std::size_t GetSize() const { return size_; }

    /*! Write the reply into the buffer
        This will write both the standard header from the base-class
        and the overridden specific data (like an A or SOA record).
     */
    virtual void Write(buffer_t& buffer) = 0;

protected:
    void WriteRdlength(buffer_t& buffer) {
        LOG_TRACE3_FN << "Writing rdlength_ = " << rdlength_ << " at position " << rdlength_hdr_;
        char *write_pos = &buffer[rdlength_hdr_];
        *reinterpret_cast<uint16_t *>(write_pos) = htons(rdlength_);
    }


private:
    const uint16_t name_ptr_ {0}; // Optional 'pointer' to an offset to a name in the reply message
    const std::string name_; // Optional name (if not using pointer).
    const uint16_t type_ {0};
    const uint16_t class_ {1}; /* Internet */
    const uint16_t ttl_ {300}; /* Common value */
    int rdlength_hdr_ {0};

protected:
    uint16_t rdlength_ = 0; /* Length of the data segment */
    size_t size_ = 0; /* Write will update this to the size of the buffer it consumes */
    size_t orig_len_ = 0;

    existing_labels_t& label_start_points_;


    /*! High-level write label method.
     *
     * It appends the domain-name from Zone to the name (unless the name ends
     * with a '.'), and appends the newly written name to label_start_points_
     */
    std::size_t WriteDomainName(buffer_t& buffer, const std::string& domainName, const Zone *zone)
    {
        std::size_t start_offset = buffer.size();
        std::string my_name {domainName};

        if (!domainName.empty() && (domainName.back() != '.')) {
            for(const Zone *z = zone; z ; z = z->parent) {
                my_name += '.';
                my_name += z->label;
            }
        }

        WriteDomainName(buffer, my_name);
        AddLabelForCompression(start_offset, &buffer[0]);
        return buffer.size() - start_offset;
    }

    /*! Add a label in the reply-buffer to the list of known labels.
     *
     * If the label-location is already in the known-list,
     * just return.
     */
    void AddLabelForCompression(uint16_t offset, const char * start)
    {
        const uint16_t my_offset = offset | 0xC000;
        const char *ofs = (start + (my_offset & ~0xC000));
        const uint16_t dst_offset = ((*ofs & 0xC0) == 0xC0) ?
            ntohs(*reinterpret_cast<const uint16_t *>(ofs)) : my_offset;

        if (std::find(label_start_points_.begin(), label_start_points_.end(),
            dst_offset) != label_start_points_.end()) {

            LOG_TRACE3_FN << "Already have offset " << (dst_offset & ~0xC000);
            return;
        }

        LOG_TRACE3_FN << "Adding offset " << (my_offset & ~0xC000)
            << ' ' << GetNameFromOffset(my_offset, start);

        label_start_points_.push_back(my_offset);
    }

    /* Write a domain-name into the end of the buffer, using header name-
     * compression if possible
     */
    std::size_t WriteDomainName(buffer_t& buffer, const std::string& domainName)
    {
        auto frags = ToFraments(domainName);
        const char *start = &buffer[0];
        uint16_t label {0};
        std::size_t frag_index {0};
        bool use_compression = true;
        if (!SearchForDomainName(frags, label, frag_index, start)) {
            frag_index = frags.size();
            use_compression = false;
        }

        const std::size_t start_size = buffer.size();
        for(std::size_t i = 0; i < frag_index; i++) {
            LOG_TRACE3_FN << "Writing name with " << frags[i].size() << " bytes: "
                << war::log::Esc(frags[i]);
            buffer.push_back(static_cast<uint8_t>(frags[i].size())); // one byte with the string-lenght
            std::copy(frags[i].begin(), frags[i].end(), std::back_inserter(buffer));
        }

        if (use_compression) {
            LOG_TRACE3_FN << "Append compressed data: " << (label & ~0xC000);
            buffer.resize(buffer.size() + 2);
            uint16_t *plabel = reinterpret_cast<uint16_t *>(&buffer[buffer.size() -2]);
            *plabel = htons(label | 0xC000);
        } else {
            LOG_TRACE3_FN << "Adding terminating zero";
            buffer.push_back(0); // end mark
        }

        return buffer.size() - start_size;
    }

    /*! Search all the existing labels in the reply for the domain name, or last part(s) of it
     *
     * \param labelStart Offset position for the best match.
     * \param pos Offset into the fragments array of where labelStart was matched
     * .
     */
    bool SearchForDomainName(const std::vector<boost::string_ref>& frags,
                             uint16_t& labelStart, std::size_t& pos,
                             const char *start) const
    {
        bool have_match = false;

        // Reverse-compare the labels and the frags to find the best match
        for(auto label : label_start_points_) {

            LOG_TRACE3_FN << "Searching label: " << (label & ~0xC000)
            << ' ' << war::log::Esc(GetNameFromOffset(label, start))
            << " for "
            << frags;

            int curr_frag {-1};
            uint16_t last_matched_offset {0};
            ReverseCompare(label, frags, curr_frag, start, last_matched_offset);
            if ((curr_frag >= 0) && (curr_frag < static_cast<int>(frags.size()))) {
                LOG_TRACE3_FN << "Possible match: curr_frag=" << curr_frag
                << ", pos=" << pos
                << ", have_match=" << have_match;

                if (!have_match || (curr_frag < static_cast<int>(pos))) {
                    have_match = true;
                    pos = curr_frag;
                    labelStart = last_matched_offset;

                    LOG_TRACE3_FN << "Best match so far at offset: " << labelStart;

                    if (pos == 0) {
                        LOG_TRACE3_FN << "Full match!";
                        return true; // No need to search further
                    }
                }
            }
        }

        return have_match;
    }

    // NB: This method is only supposed to resolve strings we ourself have
    // put in the buffer (or verified). Therefore, there are no bounds or
    // sanity checks.
    bool ReverseCompare(uint16_t label, const std::vector<boost::string_ref>& frags,
                        int &currFrag, const char *start, uint16_t& bestMatchedOffset) const {

        const int label_offset = (label & ~0xC000);
        const char *buffer = start + label_offset;
        const size_t len = static_cast<uint8_t>(*buffer );
        if (len == 0) {
            // We are at the bottom. Start comparsion
            currFrag = frags.size();
            return true;
        }
        if (len < 64) { // Normal string
            const boost::string_ref my_name(buffer + 1, len);
            if (ReverseCompare(label + len + 1, frags, currFrag, start, bestMatchedOffset)) {
                if (--currFrag < 0) {
                    return true; // Success
                }

                if (frags[currFrag] == my_name) {
                    bestMatchedOffset = label_offset;
                    return true;
                }

                ++currFrag; // Roll back
                return false;
            }
        } else if ((len & 0xC0) == 0xC0) {
            // Compressed label 2 byte header
            const uint16_t pointer = (ntohs(*reinterpret_cast<const uint16_t *>(buffer))) & ~0xC000;

            LOG_TRACE3_FN << "Re-trying at offset : " << pointer;

            // Just try again and may be we resolve the real string then.
            return ReverseCompare(pointer, frags, currFrag, start, bestMatchedOffset);
        }

        return false; // We missed
    }
};

class RdataA : public AnswerBase
{
public:
    RdataA(uint16_t namePtr,
           const boost::asio::ip::address_v4& ip, existing_labels_t& existingLabels)
    : AnswerBase(namePtr, 1 /* A type */, 4 /* IPv4 length */, existingLabels),
      ip_(ip)
    {
    }

    RdataA(const std::string& domain,
           const boost::asio::ip::address_v4& ip, existing_labels_t& existingLabels)
    : AnswerBase(domain, 1 /* A type */, 4 /* IPv4 length */, existingLabels),
    ip_(ip)
    {
    }

    void Write(buffer_t& buffer) override {
        WriteHeader(buffer);
        size_ += 4; //Finalized: GetSize() will now work

        // Copy IP address in network byte order
        const auto address = ip_.to_bytes();
        std::copy(address.begin(), address.end(), std::back_inserter(buffer));
    }
private:
    const boost::asio::ip::address_v4 ip_;
};

class RdataCname : public AnswerBase
{
public:
    RdataCname(uint16_t namePtr, const std::string& cname, existing_labels_t& existingLabels)
    : AnswerBase(namePtr, 5 /* CNAME type */, 0, existingLabels ),
    cname_(cname)
    {
    }

    void Write(buffer_t& buffer) override {
        WriteHeader(buffer);

        // Write CNAME
        rdlength_ = WriteDomainName(buffer, cname_, nullptr);
        size_ += rdlength_;
        WriteRdlength(buffer);
    }
private:
    const std::string& cname_;
};

class RdataMx : public AnswerBase
{
public:
    RdataMx(uint16_t namePtr, const std::string& fqdn, uint16_t pri,
            existing_labels_t& existingLabels)
    : AnswerBase(namePtr, 15 /* MX type */, 0, existingLabels ),
    fqdn_(fqdn), pri_{pri}
    {
    }

    void Write(buffer_t& buffer) override {
        WriteHeader(buffer);

        // Write priority
        buffer.resize(buffer.size() + 2);
        *reinterpret_cast<uint16_t *>(&buffer[buffer.size() -2]) = htons(pri_);
        // Write the naame of the mail-server

        rdlength_ = 2 + WriteDomainName(buffer, fqdn_, nullptr);
        size_ += rdlength_;
        WriteRdlength(buffer);
    }
private:
    const std::string fqdn_;
    const uint16_t pri_;

};

class RdataNs : public AnswerBase
{
public:
    RdataNs(const std::string& domain, const std::string& fdqn, existing_labels_t& existingLabels)
    : AnswerBase(domain, 2 /* CNAME type */, 0, existingLabels ),
    fdqn_(fdqn)
    {
    }

    void Write(buffer_t& buffer) override {
        WriteHeader(buffer);

        // Write the nameserver name
        rdlength_ = WriteDomainName(buffer, fdqn_, nullptr);
        size_ += rdlength_;
        WriteRdlength(buffer);
    }
private:
    const std::string fdqn_;
};

class RdataSoa : public AnswerBase
{
public:
    RdataSoa(const std::string& domain, const Zone& zone, existing_labels_t& existingLabels)
    : AnswerBase(domain, 6 /* SOA type */, 0, existingLabels ),
    zone_{zone}
    {
    }

    RdataSoa(uint16_t namePtr, const Zone& zone, existing_labels_t& existingLabels)
    : AnswerBase(namePtr, 6 /* SOA type */, 0, existingLabels ),
    zone_{zone}
    {
    }

    void Write(buffer_t& buffer) override {
        WriteHeader(buffer);
        const size_t orig_size = buffer.size();

        // Write MNAME
        WriteDomainName(buffer, zone_.soa->GetMname().fqdn, &zone_);

        // Write RNAME
        WriteDomainName(buffer, zone_.soa->rname, &zone_);

        // Write numerical fields
        const std::size_t num_segment_lenght = 4 * 5;
        buffer.resize(buffer.size() + num_segment_lenght);
        char *last_segment = &buffer[buffer.size() - num_segment_lenght];
        uint32_t *p = reinterpret_cast<uint32_t *>(last_segment);
        *p++ = htonl(zone_.soa->serial);
        *p++ = htonl(zone_.soa->refresh);
        *p++ = htonl(zone_.soa->retry);
        *p++ = htonl(zone_.soa->expire);
        *p++ = htonl(zone_.soa->minimum);

        rdlength_ = buffer.size() - orig_size;
        size_ += rdlength_;
        WriteRdlength(buffer);
    }

private:
    const Zone& zone_;
};

} // impl
} // dns
} // vUbercool

