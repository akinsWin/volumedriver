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

#ifndef META_DATA_SERVER_SERVER_H_
#define META_DATA_SERVER_SERVER_H_

#include "Interface.h"
#include "Protocol.h"

#include <unordered_set>

#include <boost/thread.hpp>
#include <boost/variant.hpp>

#include <capnp/message.h>

#include <youtils/BooleanEnum.h>
#include <youtils/LocORemServer.h>
#include <youtils/Logging.h>

namespace metadata_server
{

VD_BOOLEAN_ENUM(DeferExecution);

class ServerNG
{
public:
    ServerNG(DataBaseInterfacePtr,
             const std::string& addr,
             const uint16_t port,
             const boost::optional<std::chrono::seconds>& timeout = boost::none,
             const uint32_t nthreads = boost::thread::hardware_concurrency());

    ~ServerNG();

    ServerNG(const ServerNG&) = delete;

    ServerNG&
    operator=(const ServerNG&) = delete;

    DataBaseInterfacePtr
    database()
    {
        return db_;
    }

private:
    DECLARE_LOGGER("MetaDataServerNG");

    struct ConnectionState;
    using ConnectionStatePtr = std::shared_ptr<ConnectionState>;

    const boost::optional<std::chrono::seconds> timeout_;
    DataBaseInterfacePtr db_;
    youtils::LocORemServer server_;

    // factor out into a class of its own
    boost::mutex lock_;
    boost::condition_variable cond_;
    bool stop_;
    using DelayedFun = std::function<void()>;
    std::deque<DelayedFun> delayed_work_;
    boost::thread_group threads_;

    void
    stop_work_();

    void
    work_();

    template<typename Connection>
    void
    recv_header_(const std::shared_ptr<Connection>&,
                 ConnectionStatePtr);

    using HeaderPtr = std::shared_ptr<metadata_server_protocol::RequestHeader>;
    using MessageReaderPtr = std::shared_ptr<capnp::MessageReader>;
    using MessageBuilderPtr = std::shared_ptr<capnp::MessageBuilder>;

    using DataSource = boost::variant<std::vector<uint8_t>,
                                      kj::ArrayPtr<const capnp::word>>;

    using DataSourcePtr = std::shared_ptr<DataSource>;

    template<typename Connection>
    void
    get_data_(const std::shared_ptr<Connection>&,
              ConnectionStatePtr,
              const HeaderPtr&);

    template<typename Connection>
    void
    recv_data_(const std::shared_ptr<Connection>&,
               ConnectionStatePtr,
               const HeaderPtr&);

    template<typename Connection>
    void
    dispatch_(const std::shared_ptr<Connection>&,
              ConnectionStatePtr,
              const HeaderPtr&,
              const DataSourcePtr&,
              const MessageReaderPtr&);

    template<typename Connection>
    void
    send_response_(const std::shared_ptr<Connection>&,
                   ConnectionStatePtr,
                   metadata_server_protocol::ResponseHeader::Type,
                   metadata_server_protocol::Tag,
                   const MessageBuilderPtr&);

    template<typename Connection>
    void
    send_response_inband_(const std::shared_ptr<Connection>&,
                          ConnectionStatePtr,
                          metadata_server_protocol::ResponseHeader::Type,
                          metadata_server_protocol::Tag,
                          const MessageBuilderPtr&);

    template<typename Connection>
    void
    send_response_shmem_(const std::shared_ptr<Connection>&,
                         ConnectionStatePtr,
                         metadata_server_protocol::ResponseHeader::Type,
                         metadata_server_protocol::Tag,
                         const std::shared_ptr<capnp::FlatMessageBuilder>&);

    template<typename Connection>
    void
    error_(const std::shared_ptr<Connection>&,
           ConnectionStatePtr,
           const metadata_server_protocol::ResponseHeader::Type,
           const metadata_server_protocol::Tag,
           const std::string&);

    template<enum metadata_server_protocol::RequestHeader::Type r,
             typename Connection,
             typename Traits = metadata_server_protocol::RequestTraits<r>>
    void
    handle_(const std::shared_ptr<Connection>&,
            ConnectionStatePtr,
            const HeaderPtr&,
            const DataSourcePtr&,
            const DeferExecution,
            const MessageReaderPtr&,
            void (ServerNG::*mem_fn)(typename Traits::Params::Reader&,
                                     typename Traits::Results::Builder&));

    template<enum metadata_server_protocol::RequestHeader::Type r,
             typename Connection,
             typename Traits = metadata_server_protocol::RequestTraits<r>>
    void
    handle_shmem_(const std::shared_ptr<Connection>&,
                  ConnectionStatePtr,
                  const HeaderPtr&,
                  const DataSourcePtr&,
                  const DeferExecution,
                  const MessageReaderPtr&,
                  void (ServerNG::*mem_fn)(typename Traits::Params::Reader&,
                                           typename Traits::Results::Builder&));

    template<enum metadata_server_protocol::RequestHeader::Type r,
             typename Connection,
             typename Traits = metadata_server_protocol::RequestTraits<r>>
    void
    do_handle_(const std::shared_ptr<Connection>&,
               ConnectionStatePtr,
               const HeaderPtr&,
               const DataSourcePtr&,
               const DeferExecution,
               const MessageBuilderPtr&,
               const MessageReaderPtr&,
               void (ServerNG::*mem_fn)(typename Traits::Params::Reader&,
                                        typename Traits::Results::Builder&));

    template<enum metadata_server_protocol::RequestHeader::Type r,
             typename Connection,
             typename Traits = metadata_server_protocol::RequestTraits<r>>
    void
    do_handle_(const std::shared_ptr<Connection>&,
               ConnectionStatePtr,
               const HeaderPtr&,
               const DataSourcePtr&,
               const MessageBuilderPtr&,
               const MessageReaderPtr&,
               void (ServerNG::*mem_fn)(typename Traits::Params::Reader&,
                                        typename Traits::Results::Builder&));

    void
    open_(metadata_server_protocol::Methods::OpenParams::Reader&,
          metadata_server_protocol::Methods::OpenResults::Builder&);

    void
    drop_(metadata_server_protocol::Methods::DropParams::Reader&,
          metadata_server_protocol::Methods::DropResults::Builder&);

    void
    clear_(metadata_server_protocol::Methods::ClearParams::Reader&,
           metadata_server_protocol::Methods::ClearResults::Builder&);

    void
    multiget_(metadata_server_protocol::Methods::MultiGetParams::Reader&,
              metadata_server_protocol::Methods::MultiGetResults::Builder&);

    void
    multiset_(metadata_server_protocol::Methods::MultiSetParams::Reader&,
              metadata_server_protocol::Methods::MultiSetResults::Builder&);

    void
    set_role_(metadata_server_protocol::Methods::SetRoleParams::Reader&,
              metadata_server_protocol::Methods::SetRoleResults::Builder&);

    void
    get_role_(metadata_server_protocol::Methods::GetRoleParams::Reader&,
              metadata_server_protocol::Methods::GetRoleResults::Builder&);

    void
    list_namespaces_(metadata_server_protocol::Methods::ListParams::Reader&,
                     metadata_server_protocol::Methods::ListResults::Builder&);

    void
    ping_(metadata_server_protocol::Methods::PingParams::Reader&,
          metadata_server_protocol::Methods::PingResults::Builder&);

    void
    apply_relocation_logs_(metadata_server_protocol::Methods::ApplyRelocationLogsParams::Reader&,
                           metadata_server_protocol::Methods::ApplyRelocationLogsResults::Builder&);

    void
    catch_up_(metadata_server_protocol::Methods::CatchUpParams::Reader&,
              metadata_server_protocol::Methods::CatchUpResults::Builder&);

    void
    get_table_counters_(metadata_server_protocol::Methods::GetTableCountersParams::Reader&,
                        metadata_server_protocol::Methods::GetTableCountersResults::Builder&);

    void
    get_owner_tag_(metadata_server_protocol::Methods::GetOwnerTagParams::Reader&,
                   metadata_server_protocol::Methods::GetOwnerTagResults::Builder&);
};

}

#endif // !META_DATA_SERVER_SERVER_H_
