/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#include <stdlib.h>
#include "v8-convert.hpp"
#include "DataTypes.h"
#include "Helpers.h"
#include "Session.h"
#include "Subscription.h"
#include "Replay.h"

using namespace v8;

enum ReplayEvent
{
  EVT_MESSAGE = 0,
  EVT_PAUSE,
  EVT_RESUME,
  EVT_STOP,
  EVT_FINISH,
  EVT_ERROR
};

Persistent<Function> Replay::constructor;
static bool _peInitialized = false;

/*-----------------------------------------------------------------------------
 * Initialize the Subscription module
 */
void Replay::Init(Handle<Object> target)
{
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(New);
  t->SetClassName(String::NewSymbol("Replay"));
  t->InstanceTemplate()->SetInternalFieldCount(1);

  t->PrototypeTemplate()->Set(String::NewSymbol("on"), FunctionTemplate::New(On)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("pause"), FunctionTemplate::New(Pause)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("resume"), FunctionTemplate::New(Resume)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("stop"), FunctionTemplate::New(Stop)->GetFunction());

  constructor = Persistent<Function>::New(t->GetFunction());
}

/*-----------------------------------------------------------------------------
 * Construct a new Replay object
 */
Handle<Value> Replay::New(const Arguments& args)
{
  HandleScope scope;
  Replay* replay = (Replay*)External::Unwrap(args[0]->ToObject());
  replay->Wrap(args.This());
  return args.This();
}

Handle<Value> Replay::NewInstance(Replay* replay)
{
  HandleScope scope;
  Handle<External> wrapper = External::New(replay);
  Handle<Value> argv[1] = { wrapper };
  Local<Object> instance = constructor->NewInstance(1, argv);

  return scope.Close(instance);
}

/*-----------------------------------------------------------------------------
 * Constructor & Destructor
 */
Replay::Replay(Session* session)
{
  _msgAsync.data = this;
  _notifyAsync.data = this;
  _session = session;
  _handle = TVA_INVALID_HANDLE;
  _isInUse = false;
  uv_mutex_init(&_messageEventLock);
  uv_mutex_init(&_notificationEventLock);

  EventEmitterConfiguration events[] = 
  {
    { EVT_MESSAGE,  "message" },
    { EVT_PAUSE,    "pause"   },
    { EVT_RESUME,   "resume"  },
    { EVT_STOP,     "stop"    },
    { EVT_FINISH,   "finish"  },
    { EVT_ERROR,    "error"   }
  };
  SetValidEvents(6, events);
}

Replay::~Replay()
{
  if (_handle != TVA_INVALID_HANDLE)
  {
    tvaReplayRelease(_handle);
  }

  uv_mutex_destroy(&_messageEventLock);
  uv_mutex_destroy(&_notificationEventLock);
}


/*****     On     *****/

/*-----------------------------------------------------------------------------
 * Register for replay events
 *
 * replay.on(event, listener);
 *
 * Events / Listeners:
 *   'message'              - Message received                        - function (message) { }
 *   'finish'               - Replay finished, all messages received  - function () { }
 *   'error'                - Replay error occurred                   - function (err) { }
 *   'pause'                - Replay paused                           - function (err) { }
 *   'resume'               - Replay resumed                          - function (err) { }
 *   'stop'                 - Replay stopped                          - function (err) { }
 *
 * message = {
 *     topic,                 (string : message topic)
 *     generationTime,        (Date : when the message was sent by the publisher)
 *     receiveTime,           (Date : when the message was received)
 *     fields                 (Array : message fields list ([name]=value))
 * }
 */
Handle<Value> Replay::On(const Arguments& args)
{
  HandleScope scope;
  Replay* replay = ObjectWrap::Unwrap<Replay>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(2, args.Length());
  PARAM_REQ_STRING(0, args);          // event
  PARAM_REQ_FUNCTION(1, args);        // handler

  String::AsciiValue evt(args[0]->ToString());
  Local<Function> handler = Local<Function>::Cast(args[1]);

  if (replay->AddListener(*evt, Persistent<Function>::New(handler)) == false)
  {
    THROW_INVALID_EVENT_LISTENER("replay", *evt);
  }

  return scope.Close(args.This());
}


/*****     MessageEvent     *****/

/*-----------------------------------------------------------------------------
 * Replay message received callback
 */
void Replay::MessageReceivedEvent(TVA_MESSAGE* message, void* context)
{
  Replay* replay = (Replay*)context;
  MessageEvent messageEvent;

  TVA_STATUS rc = Subscription::ProcessRecievedMessage(message, messageEvent);
  if (rc == TVA_OK)
  {
    if (!replay->PostMessageEvent(messageEvent))
    {
      // Post failed, need to release the message
      tvaReleaseMessageData(message);
    }
  }
}

/*-----------------------------------------------------------------------------
 * Post async message received event to JavaScript
 */
void Replay::MessageAsyncEvent(uv_async_t* async, int status)
{
  HandleScope scope;
  Replay* replay = (Replay*)async->data;
  MessageEvent messageEvent;

  Local<Object> context = Context::GetCurrent()->Global();
  while (replay->GetNextMessageEvent(messageEvent))
  {
    replay->InvokeJsMessageEvent(context, messageEvent);
  }
}

/*-----------------------------------------------------------------------------
 * Post async message received event to JavaScript
 */
void Replay::InvokeJsMessageEvent(Local<Object> context, MessageEvent& messageEvent)
{
  Local<Object> message = Subscription::CreateJsMessageObject(messageEvent);
  Handle<Value> argv[] = { message };

  TryCatch tryCatch;

  Emit(EVT_MESSAGE, 1, argv);
  if (tryCatch.HasCaught())
  {
    node::FatalException(tryCatch);
  }

  if (messageEvent.isLastMessage)
  {
    Emit(EVT_FINISH, 1, argv);

    MarkInUse(false);
  }

  // All messages must be released.
  tvaReleaseMessageData(messageEvent.tvaMessage);
}


/*****     ReplayEvent     *****/

/*-----------------------------------------------------------------------------
 * Replay message received callback
 */
void Replay::ReplayNotificationEvent(TVA_REPLAY_HANDLE replayHndl, void* context,
                                     TVA_STATUS replayStatus, TVA_BOOLEAN replayHndlValid)
{
  Replay* replay = (Replay*)context;
  replay->PostNotificationEvent(replayStatus);
}

/*-----------------------------------------------------------------------------
 * Post async notification event to JavaScript
 */
void Replay::NotificationAsyncEvent(uv_async_t* async, int status)
{
  HandleScope scope;
  Replay* replay = (Replay*)async->data;
  TVA_STATUS rc;

  Local<Object> context = Context::GetCurrent()->Global();
  while (replay->GetNextNotificationEvent(rc))
  {
    replay->InvokeJsNotificationEvent(context, rc);
    replay->MarkInUse(false);
  }
}

/*-----------------------------------------------------------------------------
 * Post async notification event to JavaScript
 */
void Replay::InvokeJsNotificationEvent(Local<Object> context, TVA_STATUS rc)
{
  Handle<Value> argv[1];
  argv[0] = String::New(tvaErrToStr(rc));

  TryCatch tryCatch;

  Emit(EVT_ERROR, 1, argv);
  if (tryCatch.HasCaught())
  {
    node::FatalException(tryCatch);
  }
}


/*****     Pause/Resume     *****/

struct ReplayPauseResumeRequest
{
  Replay* replay;
  bool isPause;
  TVA_STATUS result;
};

/*-----------------------------------------------------------------------------
 * Pause the replay
 *
 * replay.pause([callback]);
 */
Handle<Value> Replay::Pause(const Arguments& args)
{
  HandleScope scope;
  Replay* replay = ObjectWrap::Unwrap<Replay>(args.This());

  if (args.Length() > 0)
  {
    // Read arguments
    Local<Function> complete = Local<Function>::Cast(args[0]);
    replay->AddOnceListener(EVT_PAUSE, Persistent<Function>::New(complete));
  }

  // Send data to worker thread
  ReplayPauseResumeRequest* request = new ReplayPauseResumeRequest;
  request->replay = replay;
  request->isPause = true;

  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, Replay::PauseResumeWorker, Replay::PauseResumeWorkerComplete);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
 * Resume the replay
 *
 * replay.resume([callback]);
 */
Handle<Value> Replay::Resume(const Arguments& args)
{
  HandleScope scope;
  Replay* replay = ObjectWrap::Unwrap<Replay>(args.This());

  if (args.Length() > 0)
  {
    // Read arguments
    Local<Function> complete = Local<Function>::Cast(args[0]);
    replay->AddOnceListener(EVT_RESUME, Persistent<Function>::New(complete));
  }

  // Send data to worker thread
  ReplayPauseResumeRequest* request = new ReplayPauseResumeRequest;
  request->replay = replay;
  request->isPause = false;

  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, Replay::PauseResumeWorker, Replay::PauseResumeWorkerComplete);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
 * Perform replay pause
 */
void Replay::PauseResumeWorker(uv_work_t* req)
{
  ReplayPauseResumeRequest* request = (ReplayPauseResumeRequest*)req->data;
  if (request->isPause)
  {
    request->result = tvaReplayPause(request->replay->GetHandle());
  }
  else
  {
    request->result = tvaReplayResume(request->replay->GetHandle());
  }
}

/*-----------------------------------------------------------------------------
 * Replay pause complete
 */
void Replay::PauseResumeWorkerComplete(uv_work_t* req)
{
  HandleScope scope;

  ReplayPauseResumeRequest* request = (ReplayPauseResumeRequest*)req->data;
  delete req;

  Handle<Value> argv[1];
  if (request->result == TVA_OK)
  {
    argv[0] = Undefined();
  }
  else
  {
    argv[0] = String::New(tvaErrToStr(request->result));
  }

  TryCatch tryCatch;

  if (request->isPause)
  {
    request->replay->Emit(EVT_PAUSE, 1, argv);
  }
  else
  {
    request->replay->Emit(EVT_RESUME, 1, argv);
  }

  if (tryCatch.HasCaught())
  {
    node::FatalException(tryCatch);
  }

  delete request;
}


/*****     Stop     *****/

struct ReplayStopRequest
{
  Replay* replay;
  TVA_STATUS result;
};

/*-----------------------------------------------------------------------------
 * Stop the replay
 *
 * replay.stop([callback]);
 */
Handle<Value> Replay::Stop(const Arguments& args)
{
  HandleScope scope;
  Replay* replay = ObjectWrap::Unwrap<Replay>(args.This());

  if (args.Length() > 0)
  {
    // Read arguments
    Local<Function> complete = Local<Function>::Cast(args[0]);
    replay->AddOnceListener(EVT_STOP, Persistent<Function>::New(complete));
  }

  // Send data to worker thread
  ReplayStopRequest* request = new ReplayStopRequest;
  request->replay = replay;

  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, Replay::StopWorker, Replay::StopWorkerComplete);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
 * Perform replay stop
 */
void Replay::StopWorker(uv_work_t* req)
{
  ReplayStopRequest* request = (ReplayStopRequest*)req->data;
  request->result = tvaReplayRelease(request->replay->GetHandle());
  request->replay->SetHandle(TVA_INVALID_HANDLE);
}

/*-----------------------------------------------------------------------------
 * Replay stop complete
 */
void Replay::StopWorkerComplete(uv_work_t* req)
{
  HandleScope scope;

  ReplayStopRequest* request = (ReplayStopRequest*)req->data;
  delete req;

  Handle<Value> argv[1];
  if (request->result == TVA_OK)
  {
    argv[0] = Undefined();
  }
  else
  {
    argv[0] = String::New(tvaErrToStr(request->result));
  }

  TryCatch tryCatch;

  request->replay->Emit(EVT_STOP, 1, argv);

  if (tryCatch.HasCaught())
  {
    node::FatalException(tryCatch);
  }

  request->replay->MarkInUse(false);

  delete request;
}

void Replay::SubscriptionHandleCloseComplete(uv_handle_t* handle)
{
}
