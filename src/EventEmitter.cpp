/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#include <stdarg.h>
#include <math.h>
#include "EventEmitter.h"

using namespace v8;
using namespace std;


/*-----------------------------------------------------------------------------
 * Constructor
 */
EventEmitter::EventEmitter()
{
  uv_mutex_init(&_eventLock);
}

/*-----------------------------------------------------------------------------
 * Destructor
 */
EventEmitter::~EventEmitter()
{
  for (int i = 1; i < _maxEventId; i++)
  {
    RemoveAllListeners(i);
  }

  uv_mutex_destroy(&_eventLock);
}

/*-----------------------------------------------------------------------------
 * Set the list of valid events
 */
void EventEmitter::SetValidEvents(int numEvents, EventEmitterConfiguration events[])
{
  int maxEventId = 0;
  uv_mutex_lock(&_eventLock);
  for (int i = 0; i < numEvents; i++)
  {
    std::string evtstr(events[i].eventName);
    _eventMap[evtstr] = events[i].eventId + 1;
    maxEventId = max(maxEventId, events[i].eventId + 1);
  }

  _listenerMap = new vector<EventEmitterListener>[maxEventId];
  _maxEventId = maxEventId;
  uv_mutex_unlock(&_eventLock);
}

/*-----------------------------------------------------------------------------
 * Add a new listener for the event
 */
bool EventEmitter::AddListener(char* eventName, Persistent<Function> handler)
{
  std::string evtstr(eventName);
  bool validEvent = false;

  uv_mutex_lock(&_eventLock);
  int eventId = _eventMap[evtstr];
  if (eventId)
  {
    eventId--;
    EventEmitterListener listener = { handler, false };
    _listenerMap[eventId].push_back(listener);
    validEvent = true;
  }
  uv_mutex_unlock(&_eventLock);

  return validEvent;
}

/*-----------------------------------------------------------------------------
 * Add a one-time listener for the event
 */
bool EventEmitter::AddOnceListener(char* eventName, Persistent<Function> handler)
{
  std::string evtstr(eventName);
  bool validEvent = false;

  int eventId = _eventMap[evtstr];
  if (eventId)
  {
    eventId--;
    validEvent = AddOnceListener(eventId, handler);
  }

  return validEvent;
}

/*-----------------------------------------------------------------------------
 * Add a one-time listener for the event
 */
bool EventEmitter::AddOnceListener(int eventId, Persistent<Function> handler)
{
  bool validEvent = false;

  uv_mutex_lock(&_eventLock);

  if (eventId < this->_maxEventId)
  {
    EventEmitterListener listener = { handler, true };
    _listenerMap[eventId].push_back(listener);
    validEvent = true;
  }

  uv_mutex_unlock(&_eventLock);

  return validEvent;
}


/*-----------------------------------------------------------------------------
 * Remove the given listener
 */
bool EventEmitter::RemoveListener(char* eventName, Persistent<Function> handler)
{
  std::string evtstr(eventName);
  bool validEvent = false;

  uv_mutex_lock(&_eventLock);

  int eventId = _eventMap[evtstr];
  if (eventId)
  {
    eventId--;
    for (int i = 0; i < _listenerMap[eventId].size(); i++)
    {
      if (_listenerMap[eventId][i].handler == handler)
      {
        _listenerMap[eventId][i].handler.Dispose();
        _listenerMap[eventId].erase(_listenerMap[eventId].begin() + i);
        break;
      }
    }

    validEvent = true;
  }

  uv_mutex_unlock(&_eventLock);

  return validEvent;
}

/*-----------------------------------------------------------------------------
 * Remove all listeners for an event
 */
bool EventEmitter::RemoveAllListeners(char* eventName)
{
  bool validEvent = false;
  std::string evtstr(eventName);
  int eventId = _eventMap[evtstr];
  if (eventId)
  {
    eventId--;
    validEvent = RemoveAllListeners(eventId);
  }

  return validEvent;
}

/*-----------------------------------------------------------------------------
 * Remove all listeners for an event
 */
bool EventEmitter::RemoveAllListeners(int eventId)
{
  bool validEvent = false;

  uv_mutex_lock(&_eventLock);

  if (eventId < _maxEventId)
  {
    while (_listenerMap[eventId].size() > 0)
    {
      _listenerMap[eventId].back().handler.Dispose();
      _listenerMap[eventId].pop_back();
    }

    validEvent = true;
  }

  uv_mutex_unlock(&_eventLock);

  return validEvent;
}

/*-----------------------------------------------------------------------------
 * Emit the event, calling all listeners
 */
int EventEmitter::Emit(char* eventName, int argc, Handle<Value> argv[])
{
  int emitCount = 0;
  std::string evtstr(eventName);
  int eventId = _eventMap[evtstr];
  if (eventId)
  {
    eventId--;
    emitCount = Emit(eventId, argc, argv);
  }

  return emitCount;
}

/*-----------------------------------------------------------------------------
 * Emit the event, calling all listeners
 */
int EventEmitter::Emit(int eventId, int argc, Handle<Value> argv[])
{
  HandleScope scope;
  Local<Object> context = Context::GetCurrent()->Global();

  int emitCount = 0;

  uv_mutex_lock(&_eventLock);

  if (eventId < _maxEventId)
  {
    std::vector<size_t> onetimeEvents;

    emitCount = (int)_listenerMap[eventId].size();
    for (size_t i = 0; i < _listenerMap[eventId].size(); i++)
    {
      _listenerMap[eventId][i].handler->Call(context, argc, argv);
      if (_listenerMap[eventId][i].onetime)
      {
        onetimeEvents.push_back(i);
      }
    }

    while (onetimeEvents.size() > 0)
    {
      size_t idx = onetimeEvents.back();
      onetimeEvents.pop_back();

      _listenerMap[eventId][idx].handler.Dispose();
      _listenerMap[eventId].erase(_listenerMap[eventId].begin() + idx);
    }
  }

  uv_mutex_unlock(&_eventLock);

  return emitCount;
}
