/* Copyright 2019 Stanford University, NVIDIA Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

// Realm inter-node networking abstractions

#ifndef REALM_NETWORK_H
#define REALM_NETWORK_H

#include "realm/realm_config.h"
#include "realm/module.h"
#include "realm/nodeset.h"
#include "realm/memory.h"
#include "realm/bytearray.h"

#include <map>

namespace Realm {

  typedef int NodeID;

  class NetworkModule;
  class MemoryImpl;
  class ByteArray;
  class ActiveMessageImpl;

  namespace Network {
    // a few globals for efficiency
    extern NodeID my_node_id;
    extern NodeID max_node_id;
    extern NodeSet all_peers;

    // in most cases, there will be a single network module - if so, we set
    //  this so we don't have to do a per-node lookup
    extern NetworkModule *single_network;

    // gets the network for a given node
    NetworkModule *get_network(NodeID node);

    // and a few "global" operations that abstract over any/all networks
    void barrier(void);

    // collective communication across all nodes (TODO: subcommunicators?)
    template <typename T>
    T broadcast(NodeID root, T val);

    template <typename T>
    void gather(NodeID root, T val, std::vector<T>& result);
    template <typename T>
    void gather(NodeID root, T val);  // for non-root participants

    // untyped versions
    void broadcast(NodeID root, const void *val_in, void *val_out, size_t bytes);
    void gather(NodeID root, const void *val_in, void *vals_out, size_t bytes);

    // for sending active messages
    ActiveMessageImpl *create_active_message_impl(NodeID target,
						  unsigned short msgid,
						  size_t header_size,
						  size_t max_payload_size,
						  void *dest_payload_addr,
						  void *storage_base,
						  size_t storage_size);

    ActiveMessageImpl *create_active_message_impl(const NodeSet& targets,
						  unsigned short msgid,
						  size_t header_size,
						  size_t max_payload_size,
						  void *storage_base,
						  size_t storage_size);
    
  };

  class NetworkSegment;
  
  // a network module provides additional functionality on top of a normal Realm
  //  module
  class NetworkModule : public Module {
  protected:
    NetworkModule(const std::string& _name);

  public:
    // all subclasses should define this (static) method - its responsibilities
    // are:
    // 1) determine if the network module should even be loaded
    // 2) fix the command line if the spawning system hijacked it
    //static NetworkModule *create_network_module(RuntimeImpl *runtime,
    //                                            int *argc, const char ***argv);

    // actual parsing of the command line should wait until here if at all
    //  possible
    virtual void parse_command_line(RuntimeImpl *runtime,
				    std::vector<std::string>& cmdline);

    // "attaches" to the network, if that is meaningful - attempts to
    //  bind/register/(pick your network-specific verb) the requested memory
    //  segments with the network
    virtual void attach(RuntimeImpl *runtime,
			std::vector<NetworkSegment *>& segments) = 0;

    // detaches from the network
    virtual void detach(RuntimeImpl *runtime,
			std::vector<NetworkSegment *>& segments) = 0;

    // collective communication within this network
    virtual void barrier(void) = 0;
    virtual void broadcast(NodeID root,
			   const void *val_in, void *val_out, size_t bytes) = 0;
    virtual void gather(NodeID root,
			const void *val_in, void *vals_out, size_t bytes) = 0;

    // used to create a remote proxy for a memory
    virtual MemoryImpl *create_remote_memory(Memory m, size_t size, Memory::Kind kind,
					     const ByteArray& rdma_info) = 0;

    virtual ActiveMessageImpl *create_active_message_impl(NodeID target,
							  unsigned short msgid,
							  size_t header_size,
							  size_t max_payload_size,
							  void *dest_payload_addr,
							  void *storage_base,
							  size_t storage_size) = 0;

    virtual ActiveMessageImpl *create_active_message_impl(const NodeSet& targets,
							  unsigned short msgid,
							  size_t header_size,
							  size_t max_payload_size,
							  void *storage_base,
							  size_t storage_size) = 0;
  };

  class NetworkSegment {
  public:
    NetworkSegment();
    
    // normally a request will just be for a particular size
    NetworkSegment(size_t _bytes, size_t _alignment);

    // but it can also be for a pre-allocated chunk of memory with a fixed address
    NetworkSegment(void *_base, size_t _bytes);

    void request(size_t _bytes, size_t _alignment);

    void assign(void *_base, size_t _bytes);

    void *base;  // once this is non-null, it cannot be changed
    size_t bytes, alignment;

    // again, a single network puts itself here in addition to adding to the map
    NetworkModule *single_network;
    ByteArray *single_network_data;

    // a map from each of the networks that successfully bound the segment to
    //  whatever data (if any) that network needs to track the binding
    std::map<NetworkModule *, ByteArray> networks;

    void add_rdma_info(NetworkModule *network,
		       const void *data, size_t len);
    const ByteArray *get_rdma_info(NetworkModule *network) const;
  };
  
}; // namespace Realm

#include "realm/network.inl"

#endif
