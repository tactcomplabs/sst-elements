// Copyright 2013-2018 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2013-2018, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

#ifndef _MEMHIERARCHY_MULTIPORTSHIM_H_
#define _MEMHIERARCHY_MULTIPORTSHIM_H_

#include <sst/core/event.h>
#include <sst/core/output.h>
#include <sst/core/link.h>
#include <sst/core/subcomponent.h>
#include <sst/core/warnmacros.h>
#include <sst/core/sst_types.h>
#include <sst/core/elementinfo.h>

#include "sst/elements/memHierarchy/util.h"
#include "sst/elements/memHierarchy/memEventBase.h"


namespace SST {
namespace MemHierarchy {

class CacheShim : public SST::SubComponent {

public:
/* Element Library Info */
#define CACHESHIM_ELI_PARAMS \
    { "debug",              "(int) Where to print debug output. Options: 0[no output], 1[stdout], 2[stderr], 3[file]", "0"},\
    { "debug_level",        "(int) Debug verbosity level. Between 0 and 10", "0"}

    CacheShim(Component* comp, Params &params) : SubComponent(comp) {
        out_.init("", 1, 0, Output::STDOUT);

        int debugLevel = params.find<int>("cacheShim.debug_level", 0);
        int debugLoc = params.find<int>("cacheShim.debug", 0);
        dbg_.init("--->  ", debugLevel, 0, (Output::output_location_t)debugLoc);
    }

    ~CacheShim() { }

//     /* Initialization functions for parent */
//     virtual void setRecvHandler(Event::HandlerBase * handler) { recvHandler = handler; }
//     virtual void init(unsigned int UNUSED(phase)) { }
//     virtual void finish() { }
//     virtual void setup() { }

//     /* Send and receive functions for MemLink */
//     virtual void sendInitData(MemEventInit * ev) =0;
//     virtual MemEventInit* recvInitData() =0;
//     virtual void send(MemEventBase * ev) =0;

protected:
    // IO Streams
    Output dbg_;
    Output out_;

    // Handlers
    SST::Event::HandlerBase * recvHandler; // Event handler to call when an event is received

};

class MultiPortShim : public CacheShim {
public:
/* Element Library Info */
#define MULTIPORTSHIM_ELI_PARAMS CACHESHIM_ELI_PARAMS, \
            {"num_ports",           "(uint) Number of ports.", "1"},\
            {"cache_link",          "(string) Set by parent component. Name of port connected to cache.", ""}, \
            {"line_size",           "(uint) Set by parent component. Size of cache line.", ""}

    SST_ELI_REGISTER_SUBCOMPONENT(MultiPortShim, "memHierarchy", "MultiPortShim", SST_ELI_ELEMENT_VERSION(1,0,0),
            "Used to provide a cache with multiple ports.", "SST::CacheShim")

    SST_ELI_DOCUMENT_PARAMS( MULTIPORTSHIM_ELI_PARAMS )

    SST_ELI_DOCUMENT_PORTS(
          {"cache_link", "Link to cache", {"memHierarchy.MemEventBase"} },
          {"port_%(port)d", "Links to network", {"memHierarchy.MemEventBase"} } )

    MultiPortShim(Component* comp, Params &params);
    ~MultiPortShim() { }

    // Init functions
    void init(unsigned int phase);

    void sendInitData(MemEventInit * ev);
    MemEventInit* recvInitData();
    void send(MemEventBase * ev);

private:
    uint64_t lineSize_;
    uint64_t numPorts_;

    SST::Link* cacheLink_;
    std::vector<SST::Link*> highNetPorts_;

    // Event handlers
    void handleResponse(SST::Event *event);
    void handleRequest(SST::Event *event);

    // Address & port handlers
    Addr toBaseAddr(Addr addr) { return (addr) & ~(lineSize_ - 1); }
    uint64_t getPortNum(Addr addr) { return (addr >> log2Of(lineSize_)) % numPorts_; }

};

} //namespace memHierarchy
} //namespace SST

#endif
