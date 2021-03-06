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

#ifndef BACKEND_SOURCE_INTERFACE_H_
#define BACKEND_SOURCE_INTERFACE_H_

#include <iosfwd>
#include <boost/iostreams/positioning.hpp> // stream_offset

#include "BackendConnectionInterface.h"

namespace backend
{

namespace ios = boost::iostreams;

class BackendSourceInterface
{
public:
    virtual ~BackendSourceInterface() {};

    // add open(BackendConnPtr) to enforce passing conn ownership at this level
    // isof at the implementation level only?

    virtual std::streamsize
    read(char* s,
         std::streamsize n) = 0;

    virtual std::streampos
    seek(ios::stream_offset off,
         std::ios_base::seekdir way) = 0;

    virtual void
    close() = 0;
};

}

#endif // !BACKEND_SOURCE_INTERFACE_H_

// Local Variables: **
// mode: c++ **
// End: **
