// Copyright 2009-2018 NTESS. Under the terms
// of Contract DE-NA0003525 with NTESS, the U.S.
// Government retains certain rights in this software.
//
// Copyright (c) 2009-2018, NTESS
// All rights reserved.
//
// Portions are copyright of other developers:
// See the file CONTRIBUTORS.TXT in the top level directory
// the distribution for more information.
//
// This file is part of the SST software package. For license
// information, see the LICENSE file in the top level directory of the
// distribution.

/*
 * File:   olb.h
 * Author: John Leidel: jleidel@tactcomplabs.com
 */

#ifndef _OLB_H_
#define _OLB_H_

// generic cxx headers
#include <queue>
#include <map>
#include <string>
#include <sstream>
#include <cstdlib>
#include <ctime>

// sst core headers
#include <sst/core/event.h>
#include <sst/core/sst_types.h>
#include <sst/core/component.h>
#include <sst/core/elementinfo.h>
#include <sst/core/link.h>
#include <sst/core/timeConverter.h>
#include <sst/core/output.h>
#include <sst/core/interfaces/simpleNetwork.h>      // required for network handlers

// sst elements headers
#include "sst/elements/memHierarchy/cache.h"        // required for local caches
#include "sst/elements/memHierarchy/memNIC.h"       // required for MemNICBase
#include "sst/elements/memHierarchy/memLinkBase.h"  // required for MemNICBase
#include "sst/elements/memHierarchy/util.h"

namespace SST {
namespace MemHierarchy {

using namespace SST::Interfaces::SimpleNetwork;
using namespace std;

/*
 * Component: memHierarchy.OLB
 *
 * Object Lookaside Buffer for PGAS hardware support
 *
 */


/*
 * OLB Request structure
 *
 */
class OLBRqst : public SST::Event {
public:
  typedef enum{
    OLB_UNK     = 0,
    OLB_WR      = 1,
    OLB_RD      = 2,
    OLB_CUSTOM  = 3
  }__OLB_Rqst;

  /* Constructor */
  OLBRqst(unsigned T,unsigned D,unsigned RS, MemEvent *ME, bool Local)
    : Tag(T), Dest(D), RqstSz(RS), Event(ME), isLocal(Local) {}

  /* Destructor */
  ~OLBRqst();

  unsigned getTag() { return Tag; }
  unsigned getDest() { return Dest; }
  unsigned getRqstSz() { return RqstSz; }

private:
  unsigned Tag;       // request tag
  unsigned Dest;      // destination logical id
  unsigned RqstSz;    // request size in bytes
  MemEvent *Event;    // memory request event
  bool isLocal;       // did this request originate locally?
};

/*
 * Top-Level OLB Component Infrastructure
 */
class OLB : public SST::Component, public MemNICBase {
public:
  /* Element Library Info */
  SST_ELI_REGISTER_COMPONENT(OLB, "memHierarchy", "OLB",
                             SST_ELI_ELEMENT_VERSION(1,0,0),
                             "Object lookaside buffer for BGAS systems",
                             COMPONENT_CATEGORY_MEMORY)

  SST_ELI_DOCUMENT_PARAMS(
    /* required */
    {"mapping",     "(string) Mapping for CPU ID to OLB IB: \"cyclic\", \"random\"", "cyclic"},
    {"frequency",   "(string) Clock frequency or period with units (Hz or s; SI Units OK). This is usually the CPU's frequency.", NULL },
    /* not required */
    {"debug",       "(uint) Where to send output. Options: 0[no output], 1[stdout], 2[stderr], 3[file]", "0"},
    {"debug_level", "(uint) Debugging level: 0 to 10. Must configure sst-core with '--enable-debug'. 1=info, 2-10=debug output", "0"}
    )

  SST_ELI_DOCUMENT_PORTS(
    {"low_network_0",   "Port connected to L1 local cache",     {"memHierarchy.MemEventBase"}},
    {"high_network_0",  "Port connected to the local CPU",      {"memHierarchy.MemEventBase"}},
    {"bgas_network_0",  "Port connected to the memory network", {"memHierarchy.MemRtrEvent"}}
    )

  SST_ELI_DOCUMENT_STATISTICS(
    {"Ext_Read",      "External read requests",   "count", 1},
    {"Ext_Write",     "External write requests",  "count", 1},
    {"Local_Read",    "External read requests",   "count", 1},
    {"Local_Write",   "External write requests",  "count", 1}
    )

  // public functions
  /* Constructor */
  OLB(Component *comp, Params &params);

  /* Destructor */
  ~OLB();

  // Event handler functions

  /* clock the OLB component */
  bool clock(Cycle_t time);

  /* send/recv functions for memory requests */
  void send(MemEventbase *ev);
  MemEventBase *recv();
  void processIncomingEvent(SST::Event* ev);  // this needs to be defined

  /* Callback for receive messages */
  bool recvNotify(int);

  /* init the component */
  void init(unsigned int);
  void setup(void);
  void finish(void);

  /* Debug interfaces */
  void printStatus(Output &out);
  void emergencyShutdownDebug(Output &out);

private:

  // OLB directory types
  typedef enum{
    OLB_UNK     = 0,
    OLB_CYCLIC  = 1,
    OLB_RANDOM  = 2
  }__OLB_DIR;

  // private functions
  bool initMappingTable();
  bool initCyclicMappingTable();
  bool initRandomMappingTable();
  unsigned LogicalToPhysical(unsigned);
  unsigned PhysicalToLogical(unsigned);
  void registerStatData();
  void createClock(Params &params);
  bool configureLinks( Params &params );
  void send(MemEventBase *ev);
  unsigned getTag();
  void replaceTag(unsigned T);


  // Internal Config State
  unsigned debug;         // debug output destination
  unsigned debug_level;   // debug level
  unsigned distro;        // logical to physical distribution config
  size_t packetHeader;    // packet header
  bool clockOn;           // determines whether the clock is enabled
  bool clockLink;         // determines whether the links need to be clocked
  Output *dbg;            // debug output

  Clock::Handler<Cache>*  clockHandler;
  TimeConverter*          defaultTimeBase;

  std::map<unsigned,unsigned> LToPMap;  // logical to physical PE mapping

  // Memory link structures
  MemLinkBase*            linkUp;
  MemLinkBase*            linkDown;

  // Handlers for the network
  SST::Interfaces::SimpleNetwork *link_control;

  // Event Queues
  std::queue<SST::Interfaces::SimpleNetwork::Request*> sendQueue; // events waiting to be sent
  std::queue<OLBRqst*> memQueue;                                  // local memory events waiting to be processed
  std::vector<<OLBRqst*> netQueue;                                // remote memory requests in flight

  // Tag Cache
  std::deque<unsigned> tagCache;                                  // cache of memory tags

  // Statistics API values
  Statistic<uint64_t>* statTotalOps;
  Statistic<uint64_t>* statTotalRead;
  Statistic<uint64_t>* statTotalWrite;
  Statistic<uint64_t>* statExtRead;
  Statistic<uint64_t>* statExtWrite;
  Statistic<uint64_t>* statLocalRead;
  Statistic<uint64_t>* statLocalWrite;
};  // class OLB

}} // end SST namespace

#endif

// EOF
