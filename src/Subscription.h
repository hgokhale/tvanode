/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#pragma once

#include <vector>
#include <v8.h>
#include <node.h>
#include "tvaClientAPI.h"
#include "tvaClientAPIInterface.h"
#include "DataTypes.h"
#include "EventEmitter.h"

class Subscription: node::ObjectWrap, EventEmitter
{
public:
  enum GdSubscriptionAckMode
  {
    GdSubscriptionAckModeAuto,
    GdSubscriptionAckModeManual
  };

  /*-----------------------------------------------------------------------------
   * Register for subscription events
   *
   * subscription.on(event, listener);
   *
   * Events / Listeners:
   *   'message'              - Message received                        - function (message) { }
   *   'ack'                  - Message ack complete                    - function (err, message) { }
   *   'stop'                 - Subscription stopped                    - function (err) { }
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
   * subscription.ackMessage(message, [callback]);
   */
  static v8::Handle<v8::Value> AckMessage(const v8::Arguments& args);

  /*-----------------------------------------------------------------------------
   * Stop the subscription
   *
   * subscription.stop([callback]);
   */
  static v8::Handle<v8::Value> Stop(const v8::Arguments& args);


  /* Internal methods */
  Subscription(Session* session);
  ~Subscription();

  static void Init(v8::Handle<v8::Object> target);
  static v8::Handle<v8::Value> New(const v8::Arguments& args);
  static v8::Handle<v8::Value> NewInstance(Subscription* subscription);

  static TVA_STATUS ProcessRecievedMessage(TVA_MESSAGE* message, MessageEvent& messageEvent);
  static v8::Local<v8::Object> CreateJsMessageObject(MessageEvent& messageEvent);

  inline Session* GetSession() { return _session; };
  inline uv_async_t* GetAsyncObj() { return &_async; }

  inline TVA_SUBSCRIPTION_HANDLE GetHandle() { return _handle; }
  inline char* GetTopic() { return _topic; }
  inline TVA_UINT32 GetQos() { return _qos; }
  inline GdSubscriptionAckMode GetAckMode() { return _ackMode; }

  inline bool PostMessageEvent(MessageEvent& messageEvent)
  {
    bool posted = false;
    uv_mutex_lock(&_messageEventLock);
    if (_isInUse)
    {
      _messageEventQueue.push(messageEvent);
      uv_async_send(GetAsyncObj());
      posted = true;
    }
    uv_mutex_unlock(&_messageEventLock);
    return posted;
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

  inline bool IsInUse() { return _isInUse; }
  inline void MarkInUse(bool inUse)
  {
    uv_mutex_lock(&_messageEventLock);
    _isInUse = inUse;

    if (inUse)
    {
      Ref();
      uv_async_init(uv_default_loop(), GetAsyncObj(), Subscription::MessageAsyncEvent);
    }
    else
    {
      uv_close((uv_handle_t*)GetAsyncObj(), Subscription::SubscriptionHandleCloseComplete);

      Unref();
      MakeWeak();
    }

    uv_mutex_unlock(&_messageEventLock);
  }

  TVA_STATUS Start(char* topic, uint8_t qos, char* name, GdSubscriptionAckMode gdAckMode);
  TVA_STATUS Stop(bool sessionClosing);

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
  std::queue<MessageEvent> _messageEventQueue;
  uv_mutex_t _messageEventLock;
  char* _topic;
  TVA_UINT32 _qos;
  GdSubscriptionAckMode _ackMode;
  bool _isInUse;
};
