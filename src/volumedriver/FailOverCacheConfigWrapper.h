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

#ifndef VD_FAILOVER_CACHE_CONFIG_WRAPPER_H_
#define VD_FAILOVER_CACHE_CONFIG_WRAPPER_H_

#include "FailOverCacheConfig.h"

#include <boost/optional.hpp>

#include <boost/serialization/string.hpp>
#include <boost/serialization/utility.hpp>
#include <boost/archive/text_oarchive.hpp>
#include <boost/archive/text_iarchive.hpp>

namespace volumedriver
{

// This one exists purely for historical reasons - to keep the serialization backward
// compatible. Get rid of it eventually.
class FailOverCacheConfigWrapper
{
public:
    typedef boost::archive::text_iarchive iarchive_type;
    typedef boost::archive::text_oarchive oarchive_type;
    static const std::string config_backend_name;

    enum class CacheType
    {
        None,
        Remote
    };

    FailOverCacheConfigWrapper() = default;

    ~FailOverCacheConfigWrapper() = default;

    FailOverCacheConfigWrapper(const FailOverCacheConfigWrapper&) = default;

    FailOverCacheConfigWrapper&
    operator=(const FailOverCacheConfigWrapper&) = default;

    FailOverCacheConfigWrapper(FailOverCacheConfigWrapper&&) = default;

    FailOverCacheConfigWrapper&
    operator=(FailOverCacheConfigWrapper&&) = default;

    CacheType
    getCacheType() const
    {
        return config_ ?
            CacheType::Remote :
            CacheType::None;
    }

    const std::string&
    getCacheHost() const
    {
        static const std::string empty;
        return config_ ?
            config_->host :
            empty;
    }

    uint16_t
    getCachePort() const
    {
        return config_ ?
            config_->port :
            0;
    }

    const boost::optional<FailOverCacheConfig>&
    config() const
    {
        return config_;
    }

    void
    set(const boost::optional<FailOverCacheConfig>& config)
    {
        config_ = config;
    }

    bool
    operator==(const FailOverCacheConfigWrapper& other) const
    {
        return config_ == other.config_;
    }

    bool
    operator!=(const FailOverCacheConfigWrapper& other) const
    {
        return not operator==(other);
    }

private:
    boost::optional<FailOverCacheConfig> config_;

    friend class boost::serialization::access;
    BOOST_SERIALIZATION_SPLIT_MEMBER();

    template<class Archive>
    void
    load(Archive& ar, const unsigned int version)
    {
        if (version < 1)
        {
            CacheType t;
            ar & t;

            std::string h;
            ar & h;

            uint16_t p;
            ar & p;

            if (t == CacheType::Remote)
            {
                config_ = FailOverCacheConfig(h,
                                              p,
                                              FailOverCacheMode::Asynchronous);
            }
            else
            {
                config_ = boost::none;
            }
        }
        else
        {
            ar & config_;
        }
    }

    template<class Archive>
    void
    save(Archive& ar, const unsigned int /* version */) const
    {
        ar & config_;
    }
};

inline std::ostream&
operator<<(std::ostream& os,
           const FailOverCacheConfigWrapper::CacheType& type)
{
    switch(type)
    {
    case FailOverCacheConfigWrapper::CacheType::None:
        return os << "None";
    case FailOverCacheConfigWrapper::CacheType::Remote:
        return os << "Remote";
    default:
        return os << "huh, you must be insane";
    }
}

}

BOOST_CLASS_VERSION(volumedriver::FailOverCacheConfigWrapper, 1);

#endif // !VD_FAILOVER_CACHE_CONFIG_WRAPPER_H_
