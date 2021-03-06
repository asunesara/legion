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

#include "realm/realm_config.h"
#include "realm/atomics.h"

#include "realm/activemsg.h"
#include "realm/mutex.h"
#include "realm/cmdline.h"
#include "realm/logging.h"

#include <math.h>

namespace Realm {

  Realm::Logger log_amhandler("amhandler");

  namespace Config {
    // if true, the number and min/max/avg/stddev duration of handler per
    //  message type is recorded and printed
    bool profile_activemsg_handlers = false;
  };


////////////////////////////////////////////////////////////////////////
//
// class ContiguousPayload
//

ContiguousPayload::ContiguousPayload(void *_srcptr, size_t _size, int _mode)
  : srcptr(_srcptr), size(_size), mode(_mode)
{}

void ContiguousPayload::copy_data(void *dest)
{
  //  log_sdp.info("contig copy %p <- %p (%zd bytes)", dest, srcptr, size);
  memcpy(dest, srcptr, size);
  if(mode == PAYLOAD_FREE)
    free(srcptr);
}

////////////////////////////////////////////////////////////////////////
//
// class TwoDPayload
//

TwoDPayload::TwoDPayload(const void *_srcptr, size_t _line_size,
			 size_t _line_count,
			 ptrdiff_t _line_stride, int _mode)
  : srcptr(_srcptr), line_size(_line_size), line_count(_line_count),
    line_stride(_line_stride), mode(_mode)
{}

void TwoDPayload::copy_data(void *dest)
{
  char *dst_c = (char *)dest;
  const char *src_c = (const char *)srcptr;

  for(size_t i = 0; i < line_count; i++) {
    memcpy(dst_c, src_c, line_size);
    dst_c += line_size;
    src_c += line_stride;
  }
}

////////////////////////////////////////////////////////////////////////
//
// class SpanPayload
//

SpanPayload::SpanPayload(const SpanList&_spans, size_t _size, int _mode)
  : spans(_spans), size(_size), mode(_mode)
{}

void SpanPayload::copy_data(void *dest)
{
  char *dst_c = (char *)dest;
  size_t bytes_left = size;
  for(SpanList::const_iterator it = spans.begin(); it != spans.end(); it++) {
    assert(it->second <= (size_t)bytes_left);
    memcpy(dst_c, it->first, it->second);
    dst_c += it->second;
    bytes_left -= it->second;
    assert(bytes_left >= 0);
  }
  assert(bytes_left == 0);
}

////////////////////////////////////////////////////////////////////////
//
// struct ActiveMessageHandlerStats
//

  ActiveMessageHandlerStats::ActiveMessageHandlerStats(void)
    : count(0), sum(0), sum2(0), minval(0), maxval(0)
  {}

  void ActiveMessageHandlerStats::record(long long t_start, long long t_end)
  {
    // TODO: thread safety?
    long long delta = t_end - t_start;
    if(delta < 0) delta = 0;
    size_t val = (delta > 0) ? delta : 0;
    if(!count || (val < minval)) minval = val;
    if(!count || (val > maxval)) maxval = val;
    count++;
    sum += val;
    sum2 += val * val; // TODO: smarter math to avoid overflow
  }


////////////////////////////////////////////////////////////////////////
//
// class ActiveMessageHandlerTable
//

ActiveMessageHandlerTable::ActiveMessageHandlerTable(void)
{}

ActiveMessageHandlerTable::~ActiveMessageHandlerTable(void)
{
  for(std::vector<HandlerEntry>::iterator it = handlers.begin();
      it != handlers.end();
      ++it)
    if(it->must_free)
      free(const_cast<char *>(it->name));
}

ActiveMessageHandlerTable::MessageHandler ActiveMessageHandlerTable::lookup_message_handler(ActiveMessageHandlerTable::MessageID id)
{
  assert(id < handlers.size());
  return handlers[id].handler;
}

const char *ActiveMessageHandlerTable::lookup_message_name(ActiveMessageHandlerTable::MessageID id)
{
  assert(id < handlers.size());
  return handlers[id].name;
}

void ActiveMessageHandlerTable::record_message_handler_call(MessageID id,
							    long long t_start,
							    long long t_end)
{
  assert(id < handlers.size());
  handlers[id].stats.record(t_start, t_end);
}

void ActiveMessageHandlerTable::report_message_handler_stats()
{
  if(Config::profile_activemsg_handlers) {
    for(size_t i = 0; i < handlers.size(); i++) {
      const ActiveMessageHandlerStats& stats = handlers[i].stats;
      if(stats.count == 0)
	continue;

      double avg = double(stats.sum) / double(stats.count);
      double stddev = sqrt((double(stats.sum2) / double(stats.count)) -
			   (avg * avg));
      log_amhandler.print() << "handler " << std::hex << i << std::dec << ": " << handlers[i].name
			    << " count=" << stats.count
			    << " avg=" << avg
			    << " dev=" << stddev
			    << " min=" << stats.minval
			    << " max=" << stats.maxval;
    }
  }
}

/*static*/ void ActiveMessageHandlerTable::append_handler_reg(ActiveMessageHandlerRegBase *new_reg)
{
  new_reg->next_handler = pending_handlers;
  pending_handlers = new_reg;
}

static inline bool hash_less(const ActiveMessageHandlerTable::HandlerEntry &a,
			     const ActiveMessageHandlerTable::HandlerEntry &b)
{
  return (a.hash < b.hash);
}

void ActiveMessageHandlerTable::construct_handler_table(void)
{
  for(ActiveMessageHandlerRegBase *nextreg = pending_handlers;
      nextreg;
      nextreg = nextreg->next_handler) {
    HandlerEntry e;
    e.hash = nextreg->hash;
    e.name = nextreg->name;
    e.must_free = nextreg->must_free;
    e.handler = nextreg->get_handler();
    handlers.push_back(e);
  }

  std::sort(handlers.begin(), handlers.end(), hash_less);

  // handler ids are the same everywhere, so only log on node 0
  if(Network::my_node_id == 0)
    for(size_t i = 0; i < handlers.size(); i++)
      log_amhandler.info() << "handler " << std::hex << i << std::dec << ": " << handlers[i].name;
}

/*static*/ ActiveMessageHandlerRegBase *ActiveMessageHandlerTable::pending_handlers = 0;

/*extern*/ ActiveMessageHandlerTable activemsg_handler_table;

}; // namespace Realm
