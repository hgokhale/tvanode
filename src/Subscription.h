#pragma once

#include <vector>
#include <v8.h>
#include <node.h>
#include "tvaClientAPI.h"
#include "tvaClientAPIInterface.h"
#include "DataTypes.h"

class Subscription: node::ObjectWrap
{
public:
  enum GdSubscriptionAckMode
  {
    GdSubscriptionAckModeAuto,
    GdSubscriptionAckModeManual
  };

  /*-----------------------------------------------------------------------------
   * Start the subscription, get ready to receive messages
   *
   * subscription.start(topic, {options});
   * options = {
   *    qos           : [quality of service: "BE"|"GC"|"GD"],   (string, optional (default: "GC"))
   *    name          : [subscription name],                    (string, only required when using GD)
   *    ackMode       : [message ack mode: "auto"|"manual"],    (string, only required when using GD)
   * };
   */
  static v8::Handle<v8::Value> Start(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Register for subscription events
   *
   * session.on(event, listener);
   *
   * Events / Listeners:
   *   'started'              - Subscription started                    - function (err) { }
   *   'message'              - Message received                        - function (message) { }
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
   * Acknowledge a received message when using "manual" ack mode with GD
   *
   * subscription.ackMessage(message, function (err) {
   *     // Message acknowledge complete
   * });
   */
  static v8::Handle<v8::Value> AckMessage(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Stop the subscription
   *
   * subscription.stop(function (err) {
   *     // Subscription stop complete
   * });
   */
  static v8::Handle<v8::Value> Stop(const v8::Arguments& args);


  /* Internal methods */
  Subscription(Session* session);
  ~Subscription();

  static void Init(v8::Handle<v8::Object> target);
  static v8::Handle<v8::Value> New(const v8::Arguments& args);
  static v8::Handle<v8::Value> NewInstance(const v8::Local<v8::Object> session);

  void SetEventHandler(char* evt, v8::Local<v8::Function> handler);

  inline Session* GetSession() { return _session; };
  inline uv_async_t* GetAsyncObj() { return &_async; }

  inline void SetHandle(TVA_SUBSCRIPTION_HANDLE handle) { _handle = handle; }
  inline TVA_SUBSCRIPTION_HANDLE GetHandle() { return _handle; }

  inline void SetQos(TVA_UINT32 qos) { _qos = qos; }
  inline TVA_UINT32 GetQos() { return _qos; }

  inline void SetAckMode(GdSubscriptionAckMode ackMode) { _ackMode = ackMode; }
  inline GdSubscriptionAckMode GetAckMode() { return _ackMode; }

  inline void PostMessageEvent(MessageEvent& messageEvent)
  {
    uv_mutex_lock(&_messageEventLock);
    _messageEventQueue.push(messageEvent);
    uv_mutex_unlock(&_messageEventLock);
    uv_async_send(GetAsyncObj());
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

  void InvokeJsStartEvent(v8::Local<v8::Object> context, TVA_STATUS rc);

private:
  static void StartWorker(uv_work_t* req);
  static void StartWorkerComplete(uv_work_t* req);
  static void AckWorker(uv_work_t* req);
  static void AckWorkerComplete(uv_work_t* req);
  static void StopWorker(uv_work_t* req);
  static void StopWorkerComplete(uv_work_t* req);
  static void SubscriptionHandleCloseComplete(uv_handle_t* handle);
  static void MessageReceivedEvent(TVA_MESSAGE* message, void* context);
  static void MessageAsyncEvent(uv_async_t* async, int status);
  void InvokeJsMessageEvent(v8::Local<v8::Object> context, MessageEvent& messageEvent);

  static v8::Persistent<v8::Function> constructor;

  Session* _session;
  TVA_SUBSCRIPTION_HANDLE _handle;
  uv_async_t _async;
  std::vector< v8::Persistent<v8::Function> > _startHandler;
  std::vector< v8::Persistent<v8::Function> > _messageHandler;
  std::queue<MessageEvent> _messageEventQueue;
  uv_mutex_t _messageEventLock;
  TVA_UINT32 _qos;
  GdSubscriptionAckMode _ackMode;
};
