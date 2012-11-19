/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#pragma once

#include <queue>
#include <vector>
#include <list>
#include <v8.h>
#include <node.h>
#include "DataTypes.h"
#include "tvaClientAPI.h"
#include "tvaClientAPIInterface.h"
#include "tvaGDAPI.h"
#include "EventEmitter.h"
#include "compat.h"

class Publication;
class Subscription;

struct GdAckWindowEntry
{
  Publication* publisher;
  v8::Persistent<v8::Object> origMessage;
  v8::Persistent<v8::Function> complete;
};

struct SessionNotificaton
{
  TVA_UINT32 code;
  union additonalData
  {
    TVA_UINT32 messageId;
    struct in_addr addresses[2];
  } data;
};

class Session: node::ObjectWrap, EventEmitter
{
public:
  /*-----------------------------------------------------------------------------
   * Register for session events
   *
   * session.on(event, listener);
   *
   * Events / Listeners:
   *   'connection-info'         - Initial connection info                 - function (activeTmx, standbyTmx) { }
   *   'connection-lost'      - Connection lost (will auto-reconnect)   - function () { }
   *   'connection-restored'  - Reconnected after connection lost       - function () { }
   *   'close'                - Session closed                          - function () { }
   *   'gds-lost'             - GDS communications lost                 - function () { }
   *   'gds-restored'         - GDS communications restored             - function () { }
   *   'notify'               - Misc. session notification event        - function (code, msg) { }
   */
  static v8::Handle<v8::Value> On(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Disconnect from the Tervela fabric
   *
   * session.close(function (err) {
   *     // Disconnect complete
   * });
   */
  static v8::Handle<v8::Value> Close(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Create a new publication
   *
   * session.createPublication(topic, function (err, pub) {
   *     // Create publication complete
   * });
   */
  static v8::Handle<v8::Value> CreatePublication(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Create a new publication (synchronous)
   *
   * var pub = session.createPublication(topic);
   */
  static v8::Handle<v8::Value> CreatePublicationSync(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Create a new subscription
   *
   * session.createSubscription(topic, {options}, function (err, sub) {
   *     // Create subscription complete
   * });
   *
   * options = {
   *    qos           : [quality of service: 'BE'|'GC'|'GD'],   (string, optional (default: 'GC'))
   *    name          : [subscription name],                    (string, only required when using GD)
   *    ackMode       : [message ack mode: 'auto'|'manual'],    (string, only required when using GD)
   * };
   */
  static v8::Handle<v8::Value> CreateSubscription(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Create a new subscription (synchronous)
   *
   * var sub = session.createSubscription(topic, {options});
   *
   * options = {
   *    qos           : [quality of service: 'BE'|'GC'|'GD'],   (string, optional (default: 'GC'))
   *    name          : [subscription name],                    (string, only required when using GD)
   *    ackMode       : [message ack mode: 'auto'|'manual'],    (string, only required when using GD)
   * };
   */
  static v8::Handle<v8::Value> CreateSubscriptionSync(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Create a new replay
   *
   * session.createReplay(topic, {options}, function (err, replay) {
   *     // Create replay complete
   * });
   *
   * options = {
   *    startTime     : [beginning of the time range]           (Date, required)
   *    endTime       : [end of the time range]                 (Date, required)
   * };
   */
  static v8::Handle<v8::Value> CreateReplay(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Create a new replay (synchronous)
   *
   * var replay = session.createReplaySync(topic, {options});
   *
   * options = {
   *    startTime     : [beginning of the time range]           (Date, required)
   *    endTime       : [end of the time range]                 (Date, required)
   * };
   */
  static v8::Handle<v8::Value> CreateReplaySync(const v8::Arguments& args);


  /* Internal methods */
  Session();
  ~Session();

  TVA_STATUS SendGdMessage(Publication* publisher, TVA_PUBLISH_MESSAGE_DATA_HANDLE messageData, 
                           v8::Persistent<v8::Object> origMessage, v8::Persistent<v8::Function> complete);
  TVA_STATUS Terminate();
  void TerminateComplete();

  inline uv_async_t* GetAsyncObj() { return &_async; }
  inline TVA_SESSION_HANDLE GetHandle() { return _handle; }
  inline void SetHandle(TVA_SESSION_HANDLE handle) { _handle = handle; }
  inline TVAGD_CONTEXT_HANDLE GetGdHandle() { return _gdHandle; }
  inline void SetGdHandle(TVAGD_CONTEXT_HANDLE handle) { _gdHandle = handle; }
  inline void SetGdMaxOut(int maxOut)
  {
    _gdAckWindow = new GdAckWindowEntry[maxOut];
    memset(_gdAckWindow, 0, sizeof(GdAckWindowEntry) * maxOut);
    _gdAckWindowSize = maxOut;
    _gdAckWindowIdx = 0;
  }

  static void Init(v8::Handle<v8::Object> target);
  static v8::Handle<v8::Value> New(const v8::Arguments& args);
  static v8::Handle<v8::Value> NewInstance(Session* session);
  static void SessionNotificationCallback(void* context, TVA_STATUS code, void* data);
  static void SessionNotificationAsyncEvent(uv_async_t* async, int status);

  void AddSubscription(Subscription* subscription)
  {
    _subscriptionList.push_back(subscription);
  }
  void RemoveSubscription(Subscription* subscription)
  {
    _subscriptionList.remove(subscription);
  }

  v8::Local<v8::Object> CreateSubscriptionTable();

  inline void PostSessionEvent(SessionNotificaton& notificationEvent)
  {
    if (_isInUse)
    {
      uv_mutex_lock(&_sessionEventLock);
      _sessionEventQueue.push(notificationEvent);
      uv_mutex_unlock(&_sessionEventLock);
      uv_async_send(GetAsyncObj());
    }
  }

  inline bool GetNextSessionEvent(SessionNotificaton& notificationEvent)
  {
    bool result = false;

    uv_mutex_lock(&_sessionEventLock);
    if (!_sessionEventQueue.empty())
    {
      notificationEvent = _sessionEventQueue.front();
      _sessionEventQueue.pop();
      result = true;
    }
    uv_mutex_unlock(&_sessionEventLock);

    return result;
  }

  inline bool IsInUse() { return _isInUse; }
  inline void MarkInUse(bool inUse)
  {
    _isInUse = inUse;
    if (inUse)
    {
      Ref();
      uv_async_init(uv_default_loop(), GetAsyncObj(), Session::SessionNotificationAsyncEvent);
    }
    else
    {
      uv_close((uv_handle_t*)GetAsyncObj(), Session::SessionHandleCloseComplete);
      Unref();
      MakeWeak();
    }
  }

private:
  static void ConnectWorker(uv_work_t* req);
  static void ConnectWorkerComplete(uv_work_t* req);
  static void CloseWorker(uv_work_t* req);
  static void CloseWorkerComplete(uv_work_t* req);
  static void CreatePublicationWorker(uv_work_t* req);
  static void CreatePublicationWorkerComplete(uv_work_t* req);
  static void CreateSubscriptionWorker(uv_work_t* req);
  static void CreateSubscriptionWorkerComplete(uv_work_t* req);
  static void CreateReplayWorker(uv_work_t* req);
  static void CreateReplayWorkerComplete(uv_work_t* req);
  static void SessionHandleCloseComplete(uv_handle_t* handle);
  void InvokeJsSessionNotification(v8::Local<v8::Object> context, SessionNotificaton& notificationEvent);

  static v8::Persistent<v8::Function> constructor;

  TVA_SESSION_HANDLE _handle;
  TVAGD_CONTEXT_HANDLE _gdHandle;
  uv_async_t _async;
  std::queue<SessionNotificaton> _sessionEventQueue;
  uv_mutex_t _sessionEventLock;

  std::list<Subscription*> _subscriptionList;

  uv_mutex_t _gdSendLock;
  GdAckWindowEntry* _gdAckWindow;
  int _gdAckWindowSize;
  int _gdAckWindowIdx;
  bool _isInUse;
};
