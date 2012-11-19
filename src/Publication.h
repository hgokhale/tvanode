/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#pragma once

#include <v8.h>
#include <node.h>
#include "tvaClientAPI.h"
#include "tvaClientAPIInterface.h"
#include "EventEmitter.h"

class Publication: node::ObjectWrap, EventEmitter
{
public:
  /*-----------------------------------------------------------------------------
   * Register for publication events
   *
   * publication.on(event, listener);
   *
   * Events / Listeners:
   *   'message'              - Message sent                            - function (err, message) { }
   *   'stop'                 - Publication stopped                     - function (err) { }
   */
  static v8::Handle<v8::Value> On(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Send a message
   *
   * publication.sendMessage(topic, message, [options], [callback]);
   *
   * // None of the members of the options object are required
   * options = {
   *    selfdescribe  : [ignore topic schema],                  (boolean, default: false)
   * });
   */
  static v8::Handle<v8::Value> SendMessage(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Stop the publication
   *
   * publication.stop([callback]);
   */
  static v8::Handle<v8::Value> Stop(const v8::Arguments& args);


  /* Internal methods */
  Publication(Session* session);
  ~Publication();

  static void Init(v8::Handle<v8::Object> target);
  static v8::Handle<v8::Value> New(const v8::Arguments& args);
  static v8::Handle<v8::Value> NewInstance(Publication* publication);
  void SendMessageComplete(int argc, v8::Handle<v8::Value> argv[]);

  inline Session* GetSession() { return _session; }

  inline void SetHandle(TVA_PUBLISHER_HANDLE handle) { _handle = handle; }
  inline TVA_PUBLISHER_HANDLE GetHandle() { return _handle; }
  inline void SetTopic(char* topic) { _topic = strdup(topic); }
  inline char* GetTopic() { return _topic; }
  inline void SetQos(int qos) { _qos = qos; }
  inline int GetQos() { return _qos; }

  inline void Lock()
  {
    uv_mutex_lock(&_sendLock);
  }
  inline void Unlock()
  {
    uv_mutex_unlock(&_sendLock);
  }

private:
  static void SendMessageWorker(uv_work_t* req);
  static void SendMessageWorkerComplete(uv_work_t* req);
  static void StopWorker(uv_work_t* req);
  static void StopWorkerComplete(uv_work_t* req);

  static v8::Persistent<v8::Function> constructor;

  Session* _session;
  TVA_PUBLISHER_HANDLE _handle;
  char* _topic;
  int _qos;
  uv_mutex_t _sendLock;
};
