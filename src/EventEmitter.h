/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#pragma once

#include <vector>
#include <map>
#include <string>
#include <v8.h>
#include <node.h>

struct EventEmitterConfiguration
{
  int eventId;
  const char* eventName;
};

struct EventEmitterListener
{
  v8::Persistent<v8::Function> handler;
  bool onetime;
};

class EventEmitter
{
public:
  EventEmitter();
  virtual ~EventEmitter();

  void SetValidEvents(int numEvents, EventEmitterConfiguration events[]);
  bool AddListener(char* eventName, v8::Persistent<v8::Function> handler);
  bool AddOnceListener(char* eventName, v8::Persistent<v8::Function> handler);
  bool AddOnceListener(int eventId, v8::Persistent<v8::Function> handler);
  bool RemoveListener(char* eventName, v8::Persistent<v8::Function> handler);
  bool RemoveAllListeners(char* eventName);
  bool RemoveAllListeners(int eventId);
  int Emit(char* eventName, int argc, v8::Handle<v8::Value> argv[]);
  int Emit(int eventId, int argc, v8::Handle<v8::Value> argv[]);

private:
  uv_mutex_t _eventLock;
  std::map<std::string, int> _eventMap;
  std::vector<EventEmitterListener> * _listenerMap;
  int _maxEventId;
};
