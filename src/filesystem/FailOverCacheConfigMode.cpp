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

#include "FailOverCacheConfigMode.h"

#include <iostream>

#include <boost/bimap.hpp>

#include <youtils/StreamUtils.h>

namespace volumedriverfs
{

namespace yt = youtils;

namespace
{

void
reminder(FailOverCacheConfigMode) __attribute__((unused));

void
reminder(FailOverCacheConfigMode m)
{
    switch (m)
    {
    case FailOverCacheConfigMode::Manual:
    case FailOverCacheConfigMode::Automatic:
        // If the compiler yells at you that you've forgotten dealing with an enum
        // value here chances are that it's also missing from the translations map
        // below. If so add it NOW.
        break;
    }
}

using TranslationsMap = boost::bimap<FailOverCacheConfigMode, std::string>;

TranslationsMap
init_translations()
{
    const std::vector<TranslationsMap::value_type> initv{
        { FailOverCacheConfigMode::Manual, "Manual" },
        { FailOverCacheConfigMode::Automatic, "Automatic" },
    };

    return TranslationsMap(initv.begin(),
                           initv.end());
}

}

std::ostream&
operator<<(std::ostream& os,
           const FailOverCacheConfigMode m)
{
    static const TranslationsMap translations(init_translations());
    return yt::StreamUtils::stream_out(translations.left,
                                       os,
                                       m);
}

std::istream&
operator>>(std::istream& is,
        FailOverCacheConfigMode& m)
{
    static const TranslationsMap translations(init_translations());
    return yt::StreamUtils::stream_in(translations.right,
                                      is,
                                      m);
}

}
