// Copyright (C) 2016 iNuron NV
//
// This file is part of Open vStorage Open Source Edition (OSE),
// as available from
//
//      http://www.openvstorage.org and
//      http://www.openvstorage.com.
//
// This file is free software; you can redistribute it and/or modify it
// under the terms of the GNU Affero General Public License v3 (GNU AGPLv3)
// as published by the Free Software Foundation, in version 3 as it comes in
// the LICENSE.txt file of the Open vStorage OSE distribution.
// Open vStorage is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY of any kind.

#ifndef __NETWORK_XIO_PROTOCOL_H_
#define __NETWORK_XIO_PROTOCOL_H_

#include "NetworkXioCommon.h"

#include <youtils/Assert.h>

PRAGMA_IGNORE_WARNING_BEGIN("-Wctor-dtor-privacy")
#include <msgpack.hpp>
PRAGMA_IGNORE_WARNING_END;

class NetworkXioMsg
{
public:
    explicit NetworkXioMsg(NetworkXioMsgOpcode opcode =
                           NetworkXioMsgOpcode::Noop,
                           const std::string& volname = "",
                           const std::string& snapname = "",
                           const size_t size = 0,
                           const uint64_t offset = 0,
                           const ssize_t retval = 0,
                           const int errval = 0,
                           const uintptr_t opaque = 0,
                           const int64_t timeout = 0,
                           const bool dtl_in_sync = true)
    : opcode_(opcode)
    , volname_(volname)
    , snapname_(snapname)
    , size_(size)
    , retval_(retval)
    , errval_(errval)
    , opaque_(opaque)
    , timeout_(timeout)
    , dtl_in_sync_(dtl_in_sync)
    , offset_(offset)
    {}

public:
    NetworkXioMsgOpcode opcode_;
    std::string volname_;
    std::string snapname_;
    size_t size_;
    ssize_t retval_;
    int errval_;
    uintptr_t opaque_;
    int64_t timeout_;
    bool dtl_in_sync_;
    union
    {
        uint64_t offset_;
        uint64_t u64_;
        int64_t i64_;
    };

public:
    const NetworkXioMsgOpcode&
    opcode() const
    {
        return opcode_;
    }

    void
    opcode(const NetworkXioMsgOpcode& op)
    {
        opcode_ = op;
    }

    const std::string&
    volume_name() const
    {
        return volname_;
    }

    void
    volume_name(const std::string& volume)
    {
        volname_ = volume;
    }

    const std::string&
    snap_name() const
    {
        return snapname_;
    }

    void
    snap_name(const std::string& snap)
    {
        snapname_ = snap;
    }

    const uintptr_t&
    opaque() const
    {
        return opaque_;
    }

    void
    opaque(const uintptr_t& opq)
    {
        opaque_ = opq;
    }

    const size_t&
    size() const
    {
        return size_;
    }

    void
    size(const size_t& size)
    {
        size_ = size;
    }

    const ssize_t&
    retval() const
    {
        return retval_;
    }

    void
    retval(const ssize_t& retval)
    {
        retval_ = retval;
    }

    const int&
    errval() const
    {
        return errval_;
    }

    void
    errval(const int& errval)
    {
        errval_ = errval;
    }

    const uint64_t&
    offset() const
    {
        return offset_;
    }

    void
    offset(const uint64_t& offset)
    {
        offset_ = offset;
    }

    const uint64_t&
    u64() const
    {
        return u64_;
    }

    void
    u64(const uint64_t& u64)
    {
        u64_ = u64;
    }

    const int64_t&
    i64() const
    {
        return i64_;
    }

    void
    i64(const uint64_t& i64)
    {
        i64_ = i64;
    }

    const int64_t&
    timeout() const
    {
        return timeout_;
    }

    void
    timeout(const int64_t& timeout)
    {
        timeout_ = timeout;
    }

    const bool&
    dtl_in_sync() const
    {
        return dtl_in_sync_;
    }

    void
    dtl_in_sync(const bool& dtlinsync)
    {
        dtl_in_sync_ = dtlinsync;
    }

    const std::string
    pack_msg() const
    {
        msgpack::sbuffer sbuf;
        msgpack::pack(sbuf, *this);
        return std::string(sbuf.data(), sbuf.size());
    }

    void
    unpack_msg(const char *sbuf,
               const size_t size)
    {
        msgpack::object_handle oh = msgpack::unpack(sbuf, size);
        msgpack::object obj = oh.get();
        obj.convert(*this);
    }

    void
    unpack_msg(const std::string& sbuf)
    {
        msgpack::object_handle oh = msgpack::unpack(sbuf.data(), sbuf.size());
        msgpack::object obj = oh.get();
        obj.convert(*this);
    }

    void
    clear()
    {
        opcode_ = NetworkXioMsgOpcode::Noop;
        volname_.clear();
        snapname_.clear();
        size_ = 0;
        offset_ = 0;
        retval_ = 0;
        errval_ = 0;
        opaque_ = 0;
        timeout_ = 0;
        dtl_in_sync_ = true;
    }
public:
    MSGPACK_DEFINE(opcode_,
                   volname_,
                   snapname_,
                   size_,
                   retval_,
                   errval_,
                   opaque_,
                   timeout_,
                   dtl_in_sync_,
                   offset_);
};

MSGPACK_ADD_ENUM(NetworkXioMsgOpcode);

#endif //__NETWORK_XIO_PROTOCOL_H_
