#pragma once

#include <v8.h>
#include <node.h>
#include "tvaClientAPI.h"
#include "tvaClientAPIInterface.h"
#include "tvaPEAPI.h"
#include "DataTypes.h"
#include "Session.h"

class Replay: node::ObjectWrap
{
public:
  /*-----------------------------------------------------------------------------
   * Register for replay events
   *
   * replay.on(event, listener);
   *
   * Events / Listeners:
   *   'message'              - Message received                        - function (message) { }
   *   'finish'               - Replay finished, all messages received  - function () { }
   *   'error'                - Replay error occurred                   - function (err) { }
   *
   * message = {
   *     topic,                 (string : message topic)
   *     generationTime,        (Date : when the message was sent by the publisher)
   *     receiveTime,           (Date : when the message was received)
   *     fields                 (Array : message fields list ([name]=value))
   * }
   */
  static v8::Handle<v8::Value> On(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Pause the replay
   *
   * replay.pause(function (err) {
   *     // Replay pause complete
   * });
   */
  static v8::Handle<v8::Value> Pause(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Resume the replay
   *
   * replay.resume(function (err) {
   *     // Replay resume complete
   * });
   */
  static v8::Handle<v8::Value> Resume(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Stop the replay
   *
   * replay.stop(function (err) {
   *     // Replay stop complete
   * });
   */
  static v8::Handle<v8::Value> Stop(const v8::Arguments& args);


  /* Internal methods */
  Replay(Session* session);
  ~Replay();

  static void Init(v8::Handle<v8::Object> target);
  static v8::Handle<v8::Value> New(const v8::Arguments& args);
  static v8::Handle<v8::Value> NewInstance(Replay* replay);
  static void MessageReceivedEvent(TVA_MESSAGE* message, void* context);
  static void ReplayNotificationEvent(TVA_REPLAY_HANDLE replayHndl, void* context,
                                      TVA_STATUS replayStatus, TVA_BOOLEAN replayHndlValid);

  void SetEventHandler(char* evt, v8::Local<v8::Function> handler);

  inline Session* GetSession() { return _session; };
  inline uv_async_t* GetMessageAsyncObj() { return &_msgAsync; }
  inline uv_async_t* GetNotifyAsyncObj() { return &_notifyAsync; }

  inline void SetHandle(TVA_REPLAY_HANDLE handle) { _handle = handle; }
  inline TVA_REPLAY_HANDLE GetHandle() { return _handle; }

  inline void PostMessageEvent(MessageEvent& messageEvent)
  {
    uv_mutex_lock(&_messageEventLock);
    _messageEventQueue.push(messageEvent);
    uv_mutex_unlock(&_messageEventLock);
    uv_async_send(GetMessageAsyncObj());
  }

  inline bool GetNextMessageEvent(MessageEvent& messageEvent)
  {
    bool result = false;

    uv_mutex_lock(&_messageEventLock);
    if (!_messageEventQueue.empty())
    {
      messageEvent = _messageEventQueue.front();
      _messageEventQueue.pop();
      result = true;
    }
    uv_mutex_unlock(&_messageEventLock);

    return result;
  }

  inline void PostNotificationEvent(TVA_STATUS rc)
  {
    uv_mutex_lock(&_notificationEventLock);
    _notificationEventQueue.push(rc);
    uv_mutex_unlock(&_notificationEventLock);
    uv_async_send(GetNotifyAsyncObj());
  }

  inline bool GetNextNotificationEvent(TVA_STATUS& rc)
  {
    bool result = false;

    uv_mutex_lock(&_notificationEventLock);
    if (!_notificationEventQueue.empty())
    {
      rc = _notificationEventQueue.front();
      _notificationEventQueue.pop();
      result = true;
    }
    uv_mutex_unlock(&_notificationEventLock);

    return result;
  }

  inline bool IsInUse() { return _isInUse; }
  inline void MarkInUse(bool inUse)
  {
    _isInUse = inUse;
    if (inUse)
    {
      uv_async_init(uv_default_loop(), GetMessageAsyncObj(), Replay::MessageAsyncEvent);
      uv_async_init(uv_default_loop(), GetNotifyAsyncObj(), Replay::NotificationAsyncEvent);

      Ref();
    }
    else
    {
      uv_close((uv_handle_t*)GetMessageAsyncObj(), Replay::SubscriptionHandleCloseComplete);
      uv_close((uv_handle_t*)GetNotifyAsyncObj(), Replay::SubscriptionHandleCloseComplete);

      Unref();
      MakeWeak();
    }
  }

  void InvokeJsFinishEvent(v8::Local<v8::Object> context, TVA_STATUS rc);
  void InvokeJsErrorEvent(v8::Local<v8::Object> context, TVA_STATUS rc);

private:
  static void PauseResumeWorker(uv_work_t* req);
  static void PauseResumeWorkerComplete(uv_work_t* req);
  static void StopWorker(uv_work_t* req);
  static void StopWorkerComplete(uv_work_t* req);
  static void SubscriptionHandleCloseComplete(uv_handle_t* handle);
  static void MessageAsyncEvent(uv_async_t* async, int status);
  void InvokeJsMessageEvent(v8::Local<v8::Object> context, MessageEvent& messageEvent);
  static void NotificationAsyncEvent(uv_async_t* async, int status);
  void InvokeJsNotificationEvent(v8::Local<v8::Object> context, TVA_STATUS rc);

  static v8::Persistent<v8::Function> constructor;

  Session* _session;
  TVA_REPLAY_HANDLE _handle;
  uv_async_t _msgAsync;
  uv_async_t _notifyAsync;
  std::vector< v8::Persistent<v8::Function> > _messageHandler;
  std::vector< v8::Persistent<v8::Function> > _finishHandler;
  std::vector< v8::Persistent<v8::Function> > _errorHandler;
  std::queue<MessageEvent> _messageEventQueue;
  uv_mutex_t _messageEventLock;
  std::queue<TVA_STATUS> _notificationEventQueue;
  uv_mutex_t _notificationEventLock;
  bool _isInUse;
};
