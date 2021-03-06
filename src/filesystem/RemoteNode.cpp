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

#include "MessageUtils.h"
#include "ObjectRouter.h"
#include "Protocol.h"
#include "RemoteNode.h"
#include "ZUtils.h"

#include <boost/chrono.hpp>
#include <boost/exception_ptr.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/make_shared.hpp>
#include <boost/thread/future.hpp>

#include <youtils/Assert.h>
#include <youtils/Catchers.h>
#include <youtils/SourceOfUncertainty.h>

#include <volumedriver/ClusterLocation.h>
// XXX: needed as we throw VolManager::VolumeDoesNotExistException.
// This needs to be redone, we don't want to know about VolManager
// at this level
#include <volumedriver/VolManager.h>

namespace volumedriverfs
{

namespace bc = boost::chrono;
namespace vd = volumedriver;
namespace yt = youtils;

#define LOCK()                                  \
    boost::lock_guard<decltype(work_lock_)> lwg__(work_lock_)

RemoteNode::RemoteNode(ObjectRouter& vrouter,
                       const NodeId& node_id,
                       const yt::Uri& uri,
                       zmq::context_t& ztx)
    : ClusterNode(vrouter, node_id, uri)
    , ztx_(ztx)
    , event_fd_(::eventfd(0, EFD_NONBLOCK))
    , stop_(false)
{
    VERIFY(event_fd_ >= 0);

    try
    {
        LOCK();
        init_zock_();
        thread_ = boost::thread(boost::bind(&RemoteNode::work_, this));
    }
    catch (...)
    {
        ::close(event_fd_);
        throw;
    }
}

RemoteNode::~RemoteNode()
{
    try
    {
        {
            LOCK();

            ASSERT(queued_work_.empty());
            ASSERT(submitted_work_.empty());

            stop_ = true;
            notify_();
        }
        thread_.join();
        ::close(event_fd_);
    }
    CATCH_STD_ALL_LOG_IGNORE(node_id() << ": exception shutting down");
}

void
RemoteNode::notify_()
{
    int ret;
    const eventfd_t val = 1;
    do {
        ret = eventfd_write(event_fd_,
                            val);
    } while (ret < 0 && errno == EINTR);
    VERIFY(ret == 0);
}

void
RemoteNode::init_zock_()
{
    zock_.reset(new zmq::socket_t(ztx_, ZMQ_DEALER));
    ZUtils::socket_no_linger(*zock_);

    LOG_INFO(node_id() << ": connecting to " << uri());
    zock_->connect(boost::lexical_cast<std::string>(uri()).c_str());
}

namespace
{

vfsprotocol::Tag
allocate_tag()
{
    static std::atomic<uint64_t> t(yt::SourceOfUncertainty()(static_cast<uint64_t>(0)));
    return vfsprotocol::Tag(t++);
}

}

struct RemoteNode::WorkItem
{
    const google::protobuf::Message& request;
    const vfsprotocol::Tag request_tag;
    vfsprotocol::RequestType request_type;
    const char* request_desc;

    ExtraSendFun* send_extra_fun;
    ExtraRecvFun* recv_extra_fun;

    boost::promise<vfsprotocol::ResponseType> promise;
    boost::unique_future<vfsprotocol::ResponseType> future;

    template<typename Request>
    WorkItem(const Request& req,
             ExtraSendFun* send_extra,
             ExtraRecvFun* recv_extra)
        : request(req)
        , request_tag(allocate_tag())
        , request_type(vfsprotocol::RequestTraits<Request>::request_type)
        , request_desc(vfsprotocol::request_type_to_string(request_type))
        , send_extra_fun(send_extra)
        , recv_extra_fun(recv_extra)
          // clang++ (3.8.0-2ubuntu3~trusty4) complains otherwise about
          // 'promise' being used unitialized when initializing 'future'
        , promise()
        , future(promise.get_future())
    {}
};

void
RemoteNode::drop_request_(const WorkItem& work)
{
    LOCK();
    queued_work_.remove_if([&](const WorkItemPtr& w)
                           {
                               VERIFY(w);
                               return w->request_tag == work.request_tag;
                           });
    submitted_work_.erase(work.request_tag);
}

template<typename Request>
void
RemoteNode::handle_(const Request& req,
                    const bc::milliseconds& timeout_ms,
                    ExtraSendFun* send_extra,
                    ExtraRecvFun* recv_extra)
{
    auto work = boost::make_shared<WorkItem>(req,
                                             send_extra,
                                             recv_extra);

    {
        LOCK();
        queued_work_.push_back(work);
        notify_();
    }

    switch (work->future.wait_for(timeout_ms))
    {
    case boost::future_status::deferred:
        {
            LOG_ERROR(node_id() << ": " << work->request_desc << ", tag " <<
                      work->request_tag << ": future unexpectedly returned 'deferred' status");
            drop_request_(*work);
            VERIFY(0 == "unexpected future_status 'deferred'");
        }
    case boost::future_status::timeout:
        {
            LOG_INFO(node_id() << ": remote did not respond within " << timeout_ms <<
                     " milliseconds - giving up");
            drop_request_(*work);
            throw RequestTimeoutException("request to remote node timed out");
        }
    case boost::future_status::ready:
        {
            handle_response_(*work);
            break;
        }
    }
}

void
RemoteNode::handle_response_(WorkItem& work)
{
    const vfsprotocol::ResponseType rsp_type = work.future.get();
    switch (rsp_type)
    {
    case vfsprotocol::ResponseType::Ok:
        // all is well
        break;
    case vfsprotocol::ResponseType::ObjectNotRunningHere:
        LOG_INFO(node_id() << ": volume not present on that node");
        throw vd::VolManager::VolumeDoesNotExistException("volume not present on node",
                                                          work.request_desc);
        break;
    case vfsprotocol::ResponseType::UnknownRequest:
        LOG_WARN(node_id() << ": got an UnknownRequest response status in response to " <<
                 work.request_desc);
        // handle differently once we need to take care of backward compatibility.
        throw ProtocolError("Remote sent UnknownRequest response status",
                            work.request_desc);
        break;
    case vfsprotocol::ResponseType::Timeout:
        LOG_ERROR(node_id() << ": got a Timeout response status in response to " <<
                  work.request_desc);
        throw RemoteTimeoutException("Remote sent timeout status",
                                     work.request_desc);
        break;
    case vfsprotocol::ResponseType::AccessBeyondEndOfVolume:
        LOG_ERROR(node_id() << ": got an access beyond volume boundaries in response to " <<
                  work.request_desc);
        throw vd::AccessBeyondEndOfVolumeException("Access beyond volume boundaries");
        break;
    case vfsprotocol::ResponseType::CannotShrinkVolume:
        LOG_ERROR(node_id() << ": cannot shrink volume in response to " <<
                  work.request_desc);
        throw vd::CannotShrinkVolumeException("Cannot shrink volume");
        break;
    case vfsprotocol::ResponseType::CannotGrowVolumeBeyondLimit:
        LOG_ERROR(node_id() << ": cannot grow volume beyond limit in response to " <<
                  work.request_desc);
        throw vd::CannotGrowVolumeBeyondLimitException("Cannot grow volume beyond limit");
        break;
    default:
        LOG_ERROR(node_id() << ": " << work.request_desc <<
                  " failed, remote returned status " << vfsprotocol::response_type_to_string(rsp_type) <<
                  " (" << static_cast<uint32_t>(rsp_type) << ")");
        throw fungi::IOException("Remote operation failed",
                                 work.request_desc);
        break;
    }
}

void
RemoteNode::work_()
{
    {
        LOCK();
        // the socket was created by another thread, per ZMQs documentation ownership
        // transfer requires a full fence memory barrier which this lock should offer
        LOG_INFO(node_id() << ": starting worker");
    }

    zmq::pollitem_t items[2];
    bool wait_for_write = false;

    while (true)
    {
        try
        {
            ASSERT(zock_ != nullptr);

            items[0].socket = *zock_;
            items[0].fd = -1;
            items[0].events = ZMQ_POLLIN;
            if (wait_for_write)
            {
                items[0].events |= ZMQ_POLLOUT;
            }

            items[1].socket = nullptr;
            items[1].fd = event_fd_;
            items[1].events = ZMQ_POLLIN;

            const int ret = zmq::poll(items,
                                      2,
                                      -1);
            THROW_WHEN(ret <= 0);

            if (stop_)
            {
                LOCK();
                ASSERT(queued_work_.empty());
                ASSERT(submitted_work_.empty());
                break;
            }
            else
            {
                if ((items[0].revents bitand ZMQ_POLLIN))
                {
                    recv_responses_();
                }

                if ((items[0].revents bitand ZMQ_POLLOUT))
                {
                    wait_for_write = not send_requests_();
                }

                if (items[1].revents bitand ZMQ_POLLIN)
                {
                    wait_for_write = not send_requests_();
                }
            }
        }
        catch (zmq::error_t& e)
        {
            if (e.num() == ETERM)
            {
                LOG_INFO(node_id() << ": stop requested, breaking out of the loop");
                return;
            }
            else
            {
                LOG_ERROR(node_id() << ": caugh ZMQ exception in event loop: " <<
                          e.what() << ". Resetting");
                init_zock_();
            }
        }
        CATCH_STD_ALL_EWHAT({
                LOG_ERROR(node_id() << ": caught exception in event loop: " <<
                          EWHAT << ". Resetting.");
                init_zock_();
            });
    }
}

bool
RemoteNode::send_requests_()
{
    int ret;
    eventfd_t val = 0;

    do {
        ret = eventfd_read(event_fd_,
                           &val);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0)
    {
        VERIFY(errno == EAGAIN);
    }
    else
    {
        VERIFY(ret == 0);
        VERIFY(val > 0);
    }

    while (true)
    {
        LOCK();

        if (queued_work_.empty())
        {
            return true;
        }

        if (not ZUtils::writable(*zock_))
        {
            LOG_WARN(node_id() << ": queuing limit reached");
            return false;
        }

        WorkItemPtr work = queued_work_.front();
        VERIFY(work);

        queued_work_.pop_front();
        const auto res(submitted_work_.emplace(work->request_tag,
                                               work));
        VERIFY(res.second);

        ZUtils::send_delimiter(*zock_, MoreMessageParts::T);
        ZUtils::serialize_to_socket(*zock_, work->request_type, MoreMessageParts::T);
        ZUtils::serialize_to_socket(*zock_, work->request_tag, MoreMessageParts::T);
        ZUtils::serialize_to_socket(*zock_,
                                    work->request,
                                    (work->send_extra_fun != nullptr) ?
                                    MoreMessageParts::T :
                                    MoreMessageParts::F);

        if (work->send_extra_fun != nullptr)
        {
            (*work->send_extra_fun)();
        }

        LOG_TRACE(node_id() << ": sent " << work->request_desc << ", tag " << work->request_tag <<
                  ", extra: " << (work->send_extra_fun != nullptr));
    }
}

void
RemoteNode::recv_responses_()
{
    while (ZUtils::readable(*zock_))
    {
        ZUtils::recv_delimiter(*zock_);
        ZEXPECT_MORE(*zock_, "response type");

        vfsprotocol::ResponseType rsp_type;
        ZUtils::deserialize_from_socket(*zock_, rsp_type);
        ZEXPECT_MORE(*zock_, "response tag");

        vfsprotocol::Tag rsp_tag;
        ZUtils::deserialize_from_socket(*zock_, rsp_tag);

        LOG_TRACE(node_id() << ": recv'd " << static_cast<uint32_t>(rsp_type) << ", tag " << rsp_tag);

        LOCK();

        auto it = submitted_work_.find(rsp_tag);
        if (it == submitted_work_.end())
        {
            LOG_WARN(node_id() << ": received orphan response with tag " <<
                     rsp_tag << " - did we time out?");
            ZUtils::drop_remaining_message_parts(*zock_);
        }
        else
        {
            WorkItemPtr w = it->second;
            submitted_work_.erase(it);

            try
            {
                if (rsp_type == vfsprotocol::ResponseType::Ok and
                    w->recv_extra_fun != nullptr)
                {
                    (*w->recv_extra_fun)();
                }

                ZEXPECT_NOTHING_MORE(*zock_);
                w->promise.set_value(rsp_type);
            }
            catch (...)
            {
                ZUtils::drop_remaining_message_parts(*zock_);
                w->promise.set_exception(boost::current_exception());
            }
        }
    }
}

void
RemoteNode::read(const Object& obj,
                 uint8_t* buf,
                 size_t* size,
                 off_t off)
{
    ASSERT(size);

    LOG_TRACE(node_id() << ": obj " << obj.id << ", size " << *size << ", off " << off);

    const auto req(vfsprotocol::MessageUtils::create_read_request(obj, *size, off));

    ExtraRecvFun recv_data([&]
                           {
                               ZEXPECT_MORE(*zock_, "read data");

                               zmq::message_t msg;
                               zock_->recv(&msg);

                               if (msg.size() > *size)
                               {
                                   LOG_ERROR(node_id() << ": read " << msg.size() <<
                                             " > expected " << *size << " from " << obj.id);
                                   throw fungi::IOException("Read size mismatch",
                                                            obj.id.str().c_str());
                               }

                               *size = msg.size();
                               memcpy(buf, msg.data(), *size);
                           });

    handle_(req,
            vrouter_.redirect_timeout(),
            nullptr,
            &recv_data);
}

void
RemoteNode::write(const Object& obj,
                  const uint8_t* buf,
                  size_t* size,
                  off_t off,
                  vd::DtlInSync& dtl_in_sync)
{
    ASSERT(size);

    LOG_TRACE(node_id() << ": obj " << obj.id << ", size " << *size << ", off " << off);

    const auto req(vfsprotocol::MessageUtils::create_write_request(obj, *size, off));

    ExtraSendFun send_data([&]
                           {
                               zmq::message_t msg(*size);
                               memcpy(msg.data(), buf, *size);
                               zock_->send(msg, 0);
                           });

    ExtraRecvFun get_rsp([&]
                         {
                             ZEXPECT_MORE(*zock_, "WriteResponse");

                             vfsprotocol::WriteResponse rsp;
                             ZUtils::deserialize_from_socket(*zock_, rsp);

                             rsp.CheckInitialized();
                             *size = rsp.size();
                             dtl_in_sync = rsp.dtl_in_sync() ? vd::DtlInSync::T : vd::DtlInSync::F;
                         });

    handle_(req,
            vrouter_.redirect_timeout(),
            &send_data,
            &get_rsp);
}

void
RemoteNode::sync(const Object& obj,
                 vd::DtlInSync& dtl_in_sync)
{
    LOG_TRACE(node_id() << ": obj " << obj.id);

    const auto req(vfsprotocol::MessageUtils::create_sync_request(obj));

    ExtraRecvFun get_rsp([&]
                         {
                             // the SyncResponse data was introduced at a later point
                             // so older versions might not send it.
                             if (ZUtils::more_message_parts(*zock_))
                             {
                                 vfsprotocol::SyncResponse rsp;
                                 ZUtils::deserialize_from_socket(*zock_, rsp);
                                 rsp.CheckInitialized();
                                 dtl_in_sync = rsp.dtl_in_sync() ? vd::DtlInSync::T : vd::DtlInSync::F;
                             }
                             else
                             {
                                 dtl_in_sync = vd::DtlInSync::F;
                             }
                         });

    handle_(req,
            vrouter_.redirect_timeout(),
            nullptr,
            &get_rsp);
}

uint64_t
RemoteNode::get_size(const Object& obj)
{
    LOG_TRACE(node_id() << ": obj " << obj.id);

    uint64_t size = 0;
    ExtraRecvFun get_size([&]
                          {
                              ZEXPECT_MORE(*zock_, "GetSizeResponse");

                              vfsprotocol::GetSizeResponse rsp;
                              ZUtils::deserialize_from_socket(*zock_, rsp);

                              rsp.CheckInitialized();
                              size = rsp.size();
                          });

    const auto req(vfsprotocol::MessageUtils::create_get_size_request(obj));

    handle_(req,
            vrouter_.redirect_timeout(),
            nullptr,
            &get_size);

    return size;
}

vd::ClusterMultiplier
RemoteNode::get_cluster_multiplier(const Object& obj)
{
    LOG_TRACE(node_id() << ": obj " << obj.id);

    uint32_t size = 0;
    ExtraRecvFun get_cluster_multiplier([&]
                          {
                              ZEXPECT_MORE(*zock_,
                                           "GetClusterMultiplierResponse");

                              vfsprotocol::GetClusterMultiplierResponse rsp;
                              ZUtils::deserialize_from_socket(*zock_, rsp);

                              rsp.CheckInitialized();
                              size = rsp.size();
                          });

    const auto req(vfsprotocol::MessageUtils::create_get_cluster_multiplier_request(obj));

    handle_(req,
            vrouter_.redirect_timeout(),
            nullptr,
            &get_cluster_multiplier);

    return vd::ClusterMultiplier(size);
}

vd::CloneNamespaceMap
RemoteNode::get_clone_namespace_map(const Object& obj)
{
    LOG_TRACE(node_id() << ": obj " << obj.id);

    vd::CloneNamespaceMap cnmap;
    ExtraRecvFun get_clone_namespace_map([&]
                          {
                            ZEXPECT_MORE(*zock_,
                                         "GetCloneNamespaceMapResponse");

                            vfsprotocol::GetCloneNamespaceMapResponse rsp;
                            ZUtils::deserialize_from_socket(*zock_, rsp);

                            rsp.CheckInitialized();
                            for (int i = 0; i < rsp.map_entry_size(); i++)
                            {
                                const auto& e = rsp.map_entry(i);
                                cnmap.emplace(vd::SCOCloneID(e.clone_id()),
                                               backend::Namespace(e.ns()));
                            }
                          });

    const auto req(vfsprotocol::MessageUtils::create_get_clone_namespace_map_request(obj));

    handle_(req,
            vrouter_.redirect_timeout(),
            nullptr,
            &get_clone_namespace_map);

    return cnmap;
}

std::vector<vd::ClusterLocation>
RemoteNode::get_page(const Object& obj,
                     const vd::ClusterAddress ca)
{
    LOG_TRACE(node_id() << ": obj " << obj.id << ", ca " << ca);

    std::vector<vd::ClusterLocation> cloc;
    ExtraRecvFun get_page([&]
    {
      ZEXPECT_MORE(*zock_,
                   "GetPageResponse");

      vfsprotocol::GetPageResponse rsp;
      ZUtils::deserialize_from_socket(*zock_, rsp);

      rsp.CheckInitialized();
      for (int i = 0; i < rsp.cluster_location_size(); i++)
      {
           vd::ClusterLocation cl;
           *reinterpret_cast<uint64_t*>(&cl) = rsp.cluster_location(i);
           cloc.emplace_back(cl);
      }
    });

    const auto req(vfsprotocol::MessageUtils::create_get_page_request(obj,
                                                                      ca));

    handle_(req,
            vrouter_.redirect_timeout(),
            nullptr,
            &get_page);

    return cloc;
}

void
RemoteNode::resize(const Object& obj,
                   uint64_t newsize)
{
    LOG_TRACE(node_id() << ": obj " << obj.id << ", new size " << newsize);

    const auto req(vfsprotocol::MessageUtils::create_resize_request(obj, newsize));

    handle_(req,
            vrouter_.redirect_timeout());
}

void
RemoteNode::unlink(const Object& obj)
{
    LOG_TRACE(node_id() << ": obj " << obj.id);

    const auto req(vfsprotocol::MessageUtils::create_delete_request(obj));

    handle_(req,
            vrouter_.redirect_timeout());
}

void
RemoteNode::transfer(const Object& obj)
{
    LOG_TRACE(node_id() << ": obj " << obj.id);

    const auto req(vfsprotocol::MessageUtils::create_transfer_request(obj,
                                                                      vrouter_.node_id(),
                                                                      vrouter_.backend_sync_timeout()));

    const bc::milliseconds req_timeout(vrouter_.backend_sync_timeout() + vrouter_.migrate_timeout());
    handle_(req,
            req_timeout);
}

void
RemoteNode::ping()
{
    LOG_TRACE(node_id());

    const auto req(vfsprotocol::MessageUtils::create_ping_message(vrouter_.node_id()));

    ExtraRecvFun handle_pong([&]
                             {
                                 ZEXPECT_MORE(*zock_, "PingMessage");
                                 vfsprotocol::PingMessage rsp;
                                 ZUtils::deserialize_from_socket(*zock_, rsp);

                                 rsp.CheckInitialized();
                                 LOG_TRACE("got pong from " << rsp.sender_id());
                             });

    handle_(req,
            vrouter_.redirect_timeout(),
            nullptr,
            &handle_pong);
}

}
