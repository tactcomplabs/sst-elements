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

#include <sst_config.h>
#include <algorithm>
#include "sst/elements/memHierarchy/olb.h"

#include <sst/core/simulation.h>

using namespace SST;
using namespace SST::MemHierarchy;
using SST::Interfaces::SimpleNetwork;
//using SST::BaseComponent;
using namespace SST::Statistics;

OLB::OLB(SST::ComponentId_t id, SST::Params &params)
  : SST::Component(id), MemNICBase(parent, params){
  //: SST::Component(comp->getId()), MemNICBase(parent, params){
//OLB::OLB(SST::Component *comp, SST::Params &params)
  //: SST::Component(comp,params), MemNICBase(parent, params){

  // setup initial values
  clockOn = false;

  // read all the params
  debug_level = params.find<int>("debug_level", 0);
  dbg = new Output( "OLB[@p:@l]: ", debug_level, 0,
            (Output::output_location_t)params.find<int>("debug", 0));

  std::string mapping = params.find<std::string>("mapping", "" );
  std::transform(mapping.begin(),mapping.end(),mapping.begin(),::tolower);
  if( mapping == "cyclic" ){
    distro = OLB_CYCLIC;
  }else if( mapping == "random" ){
    distro = OLB_RANDOM;
  }else{
    distro = OLB_UNK;
    dbg->fatal(CALL_INFO,-1,"Unknown mapping parameter type.\n");
              //mapping.c_str() );
  }

  dbg->verbose(CALL_INFO, 1, 0, "Initializing mapping table...\n" );
  if( !initMappingTable() ){
    dbg->fatal(CALL_INFO,-1,"Failed to initialize to logical to physical PE maps\n");
  }

  dbg->verbose(CALL_INFO, 1, 0, "Initializing the link infrastructure...\n" );
  if( !configureLinks(params) ){
    dbg->fatal(CALL_INFO,-1,"Failed to initialize to link infrastructure\n");
  }

  // init the tag cache
  // 16-bit tags
  for( unsigned i=0; i<65536; i++ ){
    tagCache.push_back(i);
  }

  // setup the clock
  dbg->verbose(CALL_INFO, 1, 0, "Initializing the OLB clock...\n" );
  createClock(params);

  dbg->verbose(CALL_INFO, 1, 0, "Registering the OLB statistics data...\n" );
  registerStatData();
}

OLB::~OLB(){
  dbg->verbose(CALL_INFO, 1, 0, "Completed OLB lifetime; freeing resources...\n");
  delete dbg;
}

void OLB::registerStatData(){
  statTotalOps    = Component::registerStatistic<uint64_t>("TotalOps");
  statTotalRead   = Component::registerStatistic<uint64_t>("TotalRead");
  statTotalWrite  = Component::registerStatistic<uint64_t>("TotalWrite");
  statExtRead     = Component::registerStatistic<uint64_t>("ExtRead");
  statExtWrite    = Component::registerStatistic<uint64_t>("ExtWrite");
  statLocalRead   = Component::registerStatistic<uint64_t>("LocalRead");
  statLocalWrite  = Component::registerStatistic<uint64_t>("LocalWrite");
}

// OLB::configureLinks
// Configure all the link connectivity
//
// The OLB module currently supports the following link connectivity
//     HIGH_NETWORK    :    LOW_NETWORK
// -      cpu          :       cache
// -      cpu          :       MemNIC
//
// Note that the BGAS_NETWORK_0 IS REQUIRED!!
//
bool OLB::configureLinks(Params &params){
  bool isHighNet  = false;        // high_network_0 is connected directly to the CPU
  bool isLowNet   = false;        // low_network_0 is connected to the local cache/memory hierarchy
  bool isBgasNet  = false;        // bgas_network_0 is connected to the external network interface
  bool isCache    = false;        // cache is connected
  bool found = false;

  // check for the network definitions
  isHighNet = Component::isPortConnected("high_network_0");
  isLowNet  = Component::isPortConnected("low_network_0");
  isCache   = Component::isPortConnected("cache");

  // check for the valid port combos
  if( isHighNet ){
    if( !isLowNet && !isCache ){
      dbg->fatal(CALL_INFO,-1,
                 "%s, Error: no connected low ports detected. Please connect one of 'cache' or connect N components to 'low_network_n' where n is in the range 0 to N-1\n",
                 Component::getName().c_str());
    }
  }else{
    // no high network connected
    dbg->fatal(CALL_INFO,-1,
               "%s, Error: no high network connected to the CPU: 'high_network_0'\n",
               Component::getName().c_str() );
  }

  // configure the bgas network
  std::string linkName = params.find<std::string>("bgas_network_0", "" );
  if( linkName.length() == 0 ){
    dbg->fatal(CALL_INFO,-1,
               "%s, Error: no BGAS network connected to OLB: 'bgas_network_0'\n",
               Component::getName().c_str() );
  }
  std::string linkBW = params.find<std::string>("network_bw", "80GiB/s");
  int num_vcs = 1;    // only one virtual channel for now
  std::string linkInbufSize = params.find<std::string>("network_input_buffer_size", "1KiB");
  std::string linkOutbufSize = params.find<std::string>("network_output_buffer_size", "1KiB");

  link_control = (SimpleNetwork*)parent->Component::loadSubComponent("merlin.linkcontrol", parent, params);
  if( link_control != nullptr ){
    dbg->debug(_INFO_, "Configuring bgas link_control\n" );
    isBgasNet = true;
    link_control->initialize(linkName, UnitAlgebra(linkBW), num_vcs,
                             UnitAlgebra(linkInbufSize),
                             UnitAlgebra(linkOutbufSize));

    // packet size
    UnitAlgebra packetSize = UnitAlgebra(params.find<std::string>("min_packet_size", "88" ));
    if( !packetSize.hasUnits("B") ){
      dbg->fatal(CALL_INFO,-1,
                 "%s, Error: Invalid param(%s): min_packet_size - must have units of bytes (B)\n",
                 Component::getName().c_str(), packetSize.toString().c_str());
    }
    packetHeader = packetSize.getRoundedValue();

    // set the link control to call recvNotify on event receive
    link_control->setNotifyOnReceive(new SimpleNetwork::Handler<OLB>(this, &OLB::recvNotify));
  }else{
    dbg->fatal(CALL_INFO,-1,
               "%s, Error: could not initialize the merlin linkcontrol for bgas_network_0\n",
               Component::getName().c_str() );
  }

  // derive all the link parameters
  std::string opalNode = params.find<std::string>("node", "0");
  std::string opalShMem = params.find<std::string>("shared_memory", "0");
  std::string opalSize = params.find<std::string>("local_memory_size", "0");

  Params memlink = params.find_prefix_params("memlink.");
  memlink.insert("port", "low_network_0");
  memlink.insert("node", opalNode);
  memlink.insert("shared_memory", opalShMem);
  memlink.insert("local_memory_size", opalSize);

  Params nicParams = params.find_prefix_params("memNIC." );
  nicParams.insert("node", opalNode);
  nicParams.insert("shared_memory", opalShMem);
  nicParams.insert("local_memory_size", opalSize);

  Params cpulink = params.find_prefix_params("cpulink.");
  cpulink.insert("port","high_network_0");
  cpulink.insert("node",opalNode);
  cpulink.insert("shared_memory", opalShMem);
  cpulink.insert("local_memory_size",opalSize);

  // configure all the links
  if( isHighNet && isCache ){
    dbg->debug(_INFO_, "Configuring cache with a direct link above and below\n" );

    linkDown = dynamic_cast<MemLinkBase*>(Component::loadSubComponent("memHierarchy.MemLink", this, memlink));
		//FIXME: this pointer points to the OLB object, but not the cache. So, we need a cache pointer initialized with the OLB to configure the links to the local cache
		//FIXME: Do we need to configure cache in the OLB?
    //linkDown->setRecvHandler(new Event::Handler<Cache>(this, &Cache::processIncomingEvent));
    linkDown->setRecvHandler(new Event::Handler<Cache>(local_cache, &Cache::processIncomingEvent));

    linkUp = dynamic_cast<MemLinkBase*>(Component::loadSubComponent("memHierarchy.MemLink", this, cpulink));
    linkUp->setRecvHandler(new Event::Handler<Cache>(local_cache, &Cache::processIncomingEvent));
    //linkUp->setRecvHandler(new Event::Handler<Cache>(this, &Cache::processIncomingEvent));

    clockLink = false;
  }else if( isHighNet && isLowNet ){
    dbg->debug(_INFO_, "Configuring cache with a direct link above and network link to a cache below\n" );

    // configure the low network (MemNIC)
    nicParams.find<std::string>("group", "", found);
    if (!found) nicParams.insert("group", "1");

    if (Component::isPortConnected("cache_ack") && Component::isPortConnected("cache_fwd") && Component::isPortConnected("cache_data")) {
      nicParams.find<std::string>("req.port", "", found);
      if (!found) nicParams.insert("req.port", "cache");
      nicParams.find<std::string>("ack.port", "", found);
      if (!found) nicParams.insert("ack.port", "cache_ack");
      nicParams.find<std::string>("fwd.port", "", found);
      if (!found) nicParams.insert("fwd.port", "cache_fwd");
      nicParams.find<std::string>("data.port", "", found);
      if (!found) nicParams.insert("data.port", "cache_data");
      linkDown = dynamic_cast<MemLinkBase*>(Component::loadSubComponent("memHierarchy.MemNICFour", this, nicParams));
    }else{
      nicParams.find<std::string>("port", "", found);
      if (!found) nicParams.insert("port", "cache");
      linkDown = dynamic_cast<MemLinkBase*>(Component::loadSubComponent("memHierarchy.MemNIC", this, nicParams));
    }

    linkDown->setRecvHandler(new Event::Handler<Cache>(local_cache, &Cache::processIncomingEvent));

    // cpu link
    linkUp = dynamic_cast<MemLinkBase*>(Component::loadSubComponent("memHierarchy.MemLink", this, cpulink));
    linkUp->setRecvHandler(new Event::Handler<Cache>(local_cache, &Cache::processIncomingEvent));
    clockLink = true;
  }

  return true;
}

bool OLB::initMappingTable(){
  switch( distro ){
  case OLB_CYCLIC:
    return initCyclicMappingTable();
    break;
  case OLB_RANDOM:
    return initRandomMappingTable();
    break;
  case OLB_UNK:
  default:
    return false;
    break;
  }
}

// cyclic logical to physical mapping
// logical id=1 (first entry) = physical cpu 0
// logical id=N = physical cpu N-1
bool OLB::initCyclicMappingTable(){
  for( unsigned i=0; i<entries; i++ ){
    LToPMap.insert(std::pair<unsigned,unsigned>(i+1,i));
  }
}

// randomizes the starting point of the physical entries
// into the table.  all other entries are cyclic
bool OLB::initRandomMappingTable(){
  srand(time(NULL));
  unsigned start = (unsigned)(std::rand()%(entries-1));
  for( unsigned i=0; i<entries; i++ ){
    LToPMap.insert(std::pair<unsigned,unsigned>(i+1,start));
    start = start+1;
    if( start >= entries ){
      start = 0;
    }
  }
}

// convert the logical PE id to a physical cpu number
// call a fatal error if it fails
unsigned OLB::LogicalToPhysical(unsigned logical){
  std::map<unsigned,unsigned>::iterator itr;

  for(itr=LToPMap.begin(); itr!=LToPMap.end(); ++itr){
    if( itr->first == logical ){
      return itr->second;
    }
  }
  //dbg->fatal(CALL_INFO,-1,"Failed to decode logical id.\n", std::to_string(logical).c_str());
  dbg->fatal(CALL_INFO,-1,"Failed to decode logical id.\n");
}

// convert the physical cpu number to the logical id
// call a fatal error if it fails
unsigned OLB::PhysicalToLogical(unsigned physical){
  std::map<unsigned,unsigned>::iterator itr;

  for(itr=LToPMap.begin(); itr!=LToPMap.end(); ++itr){
    if( itr->second == physical){
      return itr->first;
    }
  }
  //dbg->fatal(CALL_INFO,-1,"Failed to decode physical id.\n", std::to_string(physical).c_str());
  dbg->fatal(CALL_INFO,-1,"Failed to decode physical id.\n");
}

void OLB::createClock(Params &params){
  // create the clock
  bool found = false;
  std::string frequency = params.find<std::string>("frequency", "", found );
  if( !found ){
    dbg->fatal(CALL_INFO,-1,
              "%s, Param not specified: frequency - OLB frequency\n",
              Component::getName().c_str() );
  }

  clockHandler    = new Clock::Handler<OLB>(this,&OLB::clock);
  defaultTimeBase = Component::registerClock(frequency, clockHandler);
}

/*
 * Called by parent on a clock cycle
 * Returns whether anything was handled this cycle
 *
 */
bool OLB::clock(Cycle_t time){
  // process local memory requests
  if( memQueue.size() > 0 ){
  }

  // process network memory requests
  if( sendQueue.size() > 0 ){
  }
}

/*
 * Send functions for BGAS network ops
 *
 */
void OLB::send(MemEventBase *ev){
  SimpleNetwork::Request *req = new SimpleNetwork::Request();
	//TODO: filled in the req data
	//ev = MemHierarchyInterface::createCustomEvent(req);
	//requests_[ev->getID()] = req;
	//link_->send(ev);
}

/*
 * Event handler for incoming network messages
 *
 */
bool OLB::recvNotify(int) {
  MemEventBase *me = recv();
	CustomCmdEvent* cme = static_cast<CustomCmdEvent*>(me);
  if( cme ){
    // call the receive handler
		uint32_t Opc = cme->getOpCode();
    //uint32_t Opc = me->getCustomOpc();
    unsigned Tag = getTag();
    unsigned Dest = ((unsigned)(Opc)>>1);
    //unsigned RqstSz = me->size;
		// Do we need payload size of event size?
    //unsigned RqstSz = me->getEventSize();
    unsigned RqstSz = cme->getSize();
    if( me->getCmd() == Command::CustomReq ){
      //
      // This is a BGAS request
      // We must pull the command code and decode the requested
      // destination.  If the deocoding is successful, fire off
      // a network request.  We initially decode the destination
      // here as the OLB::send function returns void
      //
			//FIXME: Do we need to change the me to the MemEvent or change the Send/Recv/RecvNotify... to receive the obj with MemEvent type?
      netQueue.push_back(new OLBRqst(Tag,Dest,RqstSz,(MemEvent*)me,false));
      this->send(me);
    }else{
      //
      // This is a normal memory request, process it as normal
      //
      //memQueue.push_back(new OLBRqst(Tag,Dest,RqstSz,me,true));
      memQueue.push(new OLBRqst(Tag,Dest,RqstSz,(MemEvent*)me,true));
    }
  }
  return true;
}


/*
 * Event handler for incoming memory requests
 *
 */
MemEventBase* OLB::recv(){
  SimpleNetwork::Request *req = link_control->recv(0);
  if( req != nullptr ){
    // decode the network requests and create a new local request
		    /*Addr baseAddr = (req->addrs[0]) & baseAddrMask_;
				CustomCmdEvent * cme = new CustomCmdEvent(Component::getName().c_str(), req->addrs[0], baseAddr, Command::CustomReq, req->getCustomOpc(), req->size);
				cme->setRqstr(rqstr_);
				cme->setDst(rqstr_);

				if(req->flags & SimpleMem::Request::F_NONCACHEABLE)
						cme->setFlag(MemEvent::F_NONCACHEABLE);
																	    							
				if (req->data.size() != 0) {
						cme->setPayload(req->data); // Note this updates cme->size to payload.size()...
						cme->setSize(req->size);    // Assume this is what we want, not the copied payload size
				}
				cme->setVirtualAddress(req->getVirtualAddress());
				cme->setInstructionPointer(req->getInstructionPointer());

				cme->setMemFlags(req->memFlags);
																																						    
				return cme;
				*/
				//MemEventBase *me = createCustomEvent(req);

  }
  return nullptr;
}

void OLB::init(unsigned int in){
  link_control->init(in);
  // exchange all the config info
  MemNICBase::nicInit(link_control,in);
}

void OLB::setup(void){
}

void OLB::finish(void){
}

void OLB::printStatus(Output &out){
  out.output( "  MemHierarchy::OLB\n" );
}

void OLB::emergencyShutdownDebug(Output &out){
}

unsigned OLB::getTag(){
	unsigned ret = tagCache.front();
	tagCache.pop_front();
	return ret;
  //return tagCache.pop_front();
}

void OLB::replaceTag(unsigned T){
  tagCache.push_back(T);
}

// EOF
