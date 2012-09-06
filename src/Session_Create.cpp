/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#include <vector>
#include <string>
#include <stdlib.h>
#include "v8-convert.hpp"
#include "Helpers.h"
#include "Session.h"
#include "Publication.h"
#include "Subscription.h"
#include "Replay.h"

using namespace v8;


/*****     CreatePublication     *****/

struct CreatePublicationRequest
{
  Session* session;
  Publication* publication;
  char* topic;
  TVA_STATUS result;
  Persistent<Function> complete;
};

/*-----------------------------------------------------------------------------
 * Create a new publication
 *
 * session.createPublication(topic, function (err, pub) {
 *     // Create publication complete
 * });
 */
Handle<Value> Session::CreatePublication(const Arguments& args)
{
  HandleScope scope;
  Session* session = ObjectWrap::Unwrap<Session>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(2, args.Length());
  PARAM_REQ_STRING(0, args);        // topic
  PARAM_REQ_FUNCTION(1, args);      // 'complete' callback

  String::AsciiValue topic(args[0]->ToString());
  Local<Function> complete = Local<Function>::Cast(args[1]);

  // Send data to worker thread
  CreatePublicationRequest* request = new CreatePublicationRequest;
  request->session = session;
  request->topic = strdup(*topic);
  request->complete = Persistent<Function>::New(complete);

  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, Session::CreatePublicationWorker, Session::CreatePublicationWorkerComplete);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
 * Create a new publication (synchronous)
 *
 * var pub = session.createPublication(topic);
 */
Handle<Value> Session::CreatePublicationSync(const Arguments& args)
{
  HandleScope scope;
  Session* session = ObjectWrap::Unwrap<Session>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(1, args.Length());
  PARAM_REQ_STRING(0, args);        // topic

  String::AsciiValue topic(args[0]->ToString());

  // Call CreatePublicationWorker synchronously
  CreatePublicationRequest request;
  request.session = session;
  request.topic = strdup(*topic);

  uv_work_t req;
  req.data = &request;

  CreatePublicationWorker(&req);

  free(request.topic);

  // Return result - Publication object if successful, else error string
  Handle<Value> result;
  if (request.result == TVA_OK)
  {
    result = Local<Value>::New(Publication::NewInstance(request.publication));
  }
  else
  {
    result = String::New(tvaErrToStr(request.result));
  }

  return scope.Close(result);
}

/*-----------------------------------------------------------------------------
 * Create a new publication
 */
void Session::CreatePublicationWorker(uv_work_t* req)
{
  CreatePublicationRequest* request = (CreatePublicationRequest*)req->data;

  TVA_PUBLISHER_HANDLE publisher = TVA_INVALID_HANDLE;
  TVA_STATUS rc = tvaCreatePublication(request->session->GetHandle(), request->topic, 0, false, false, &publisher);
  if (rc == TVA_OK)
  {
    Publication* publication = new Publication(request->session);
    publication->SetHandle(publisher);

    /* Tervela API versions 5.1.5 and above support retrieving the QoS of a publication
       after it was created.  This allows the node library to use the same set of functions
       for publishing to both BE/GC and GD topics, unlink the C/Java/C# APIs which have
       separate functions for GD.  

       For library versions prior to 5.1.5, the node library will need to be hardcoded
       to support only GC topics or only GD topics for publication. 
       Change the explicit SetQos below to TVA_QOS_GUARANTEED_DELIVERY for GD publish.
       Note that in either case you can't publish GD to a GC topic and vice versa. */
#ifdef TVA_PUBINFO_QOS
    int qos;
    if (tvaPubInfoGet(publisher, TVA_PUBINFO_QOS, &qos, sizeof(qos)) == TVA_OK)
    {
      publication->SetQos(qos);
    }
#else
    /* NOTE: this will fail if the underlying topic is GD, since you must publish
       GD to a topic that could have GD subscribers.  You can set it to
       TVA_QOS_GUARANTEED_DELIVERY instead, but then you will only be able to
       publish to GD topics.  */
    publication->SetQos(TVA_QOS_GUARANTEED_CONNECTED);
#endif

    request->publication = publication;
  }

  request->result = rc;
}

/*-----------------------------------------------------------------------------
 * Create publication complete
 */
void Session::CreatePublicationWorkerComplete(uv_work_t* req)
{
  HandleScope scope;

  CreatePublicationRequest* request = (CreatePublicationRequest*)req->data;
  delete req;

  Handle<Value> argv[2];
  if (request->result == TVA_OK)
  {
    argv[0] = Undefined();
    argv[1] = Local<Value>::New(Publication::NewInstance(request->publication));
  }
  else
  {
    argv[0] = String::New(tvaErrToStr(request->result));
    argv[1] = Undefined();
  }

  TryCatch tryCatch;

  request->complete->Call(Context::GetCurrent()->Global(), 2, argv);
  request->complete.Dispose();
  if (tryCatch.HasCaught())
  {
    node::FatalException(tryCatch);
  }

  free(request->topic);
  delete request;
}


/*****     CreateSubscription     *****/

struct CreateSubscriptionRequest
{
  Session* session;
  Subscription* subscription;
  char* topic;
  char* name;
  TVA_UINT32 qos;
  TVA_STATUS result;
  Subscription::GdSubscriptionAckMode gdAckMode;
  Persistent<Function> complete;

  CreateSubscriptionRequest()
  {
    topic = NULL;
    name = NULL;
    qos = TVA_QOS_GUARANTEED_CONNECTED;
    gdAckMode = Subscription::GdSubscriptionAckModeAuto;
  }

  ~CreateSubscriptionRequest()
  {
    if (topic) free(topic);
    if (name) free(name);
    if (!complete.IsEmpty()) complete.Dispose();
  }
};

bool CreateSubscriptionParseOptions(Local<Object> options, CreateSubscriptionRequest* request);

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
Handle<Value> Session::CreateSubscription(const Arguments& args)
{
  HandleScope scope;
  Session* session = ObjectWrap::Unwrap<Session>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(2, args.Length());
  PARAM_REQ_STRING(0, args);        // topic

  // Ready arguments
  String::AsciiValue topic(args[0]->ToString());
  Local<Function> complete;

  CreateSubscriptionRequest* request = new CreateSubscriptionRequest();
  request->topic = strdup(*topic);
  request->session = session;

  if (args.Length() > 2)
  {
    PARAM_REQ_OBJECT(1, args);      // options
    PARAM_REQ_FUNCTION(2, args);    // complete

    Local<Object> options = Local<Object>::Cast(args[1]);
    complete = Local<Function>::Cast(args[2]);

    if (!CreateSubscriptionParseOptions(options, request))
    {
      ThrowException(Exception::TypeError(String::New("Incomplete options")));
      return scope.Close(Undefined());
    }
  }
  else
  {
    PARAM_REQ_FUNCTION(1, args);    // complete
    complete = Local<Function>::Cast(args[1]);
  }

  request->complete = Persistent<Function>::New(complete);

  // Post work request
  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, Session::CreateSubscriptionWorker, Session::CreateSubscriptionWorkerComplete);

  return scope.Close(args.This());
}

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
Handle<Value> Session::CreateSubscriptionSync(const Arguments& args)
{
  HandleScope scope;
  Session* session = ObjectWrap::Unwrap<Session>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(1, args.Length());
  PARAM_REQ_STRING(0, args);        // topic

  // Ready arguments
  String::AsciiValue topic(args[0]->ToString());

  CreateSubscriptionRequest request;
  request.topic = strdup(*topic);
  request.session = session;

  if (args.Length() > 1)
  {
    PARAM_REQ_OBJECT(1, args);      // options

    Local<Object> options = Local<Object>::Cast(args[1]);

    if (!CreateSubscriptionParseOptions(options, &request))
    {
      ThrowException(Exception::TypeError(String::New("Incomplete options")));
      return scope.Close(Undefined());
    }
  }

  // Call CreateSubscriptionWorker synchronously
  uv_work_t req;
  req.data = &request;

  CreateSubscriptionWorker(&req);

  // Return result - Subscription object if successful, else error string
  Handle<Value> result;
  if (request.result == TVA_OK)
  {
    result = Local<Value>::New(Subscription::NewInstance(request.subscription));
    request.subscription->MarkInUse(true);
  }
  else
  {
    result = String::New(tvaErrToStr(request.result));
  }

  return scope.Close(result);
}

/*-----------------------------------------------------------------------------
 * Parse options
 */
bool CreateSubscriptionParseOptions(Local<Object> options, CreateSubscriptionRequest* request)
{
  std::vector<std::string> optionNames = cvv8::CastFromJS<std::vector<std::string> >(options->GetPropertyNames());
  for (size_t i = 0; i < optionNames.size(); i++)
  {
    char* optionName = (char*)(optionNames[i].c_str());
    Local<Value> optionValue = options->Get(String::NewSymbol(optionName));

    if (optionValue->IsUndefined())
    {
      continue;
    }

    else if (tva_str_casecmp(optionName, "name") == 0)
    {
      String::AsciiValue val(optionValue->ToString());
      request->name = strdup(*val);
    }
    else if (tva_str_casecmp(optionName, "qos") == 0)
    {
      String::AsciiValue val(optionValue->ToString());
      if (tva_str_casecmp(*val, "BE") == 0)
      {
        request->qos = TVA_QOS_BEST_EFFORT;
      }
      else if (tva_str_casecmp(*val, "GD") == 0)
      {
        request->qos = TVA_QOS_GUARANTEED_DELIVERY;
      }
    }
    else if (tva_str_casecmp(optionName, "ackMode") == 0)
    {
      String::AsciiValue val(optionValue->ToString());
      if (tva_str_casecmp(*val, "manual") == 0)
      {
        request->gdAckMode = Subscription::GdSubscriptionAckModeManual;
      }
    }
  }

  if ((request->qos == TVA_QOS_GUARANTEED_DELIVERY) && !(request->name))
  {
    return false;
  }

  return true;
}

/*-----------------------------------------------------------------------------
 * Perform create subscription
 */
void Session::CreateSubscriptionWorker(uv_work_t* req)
{
  CreateSubscriptionRequest* request = (CreateSubscriptionRequest*)req->data;

  Subscription* subscription = new Subscription(request->session);
  TVA_STATUS rc = subscription->Start(request->topic, request->qos, request->name, request->gdAckMode);
  if (rc == TVA_OK)
  {
    request->subscription = subscription;
  }
  else
  {
    delete subscription;
  }

  request->result = rc;
}

/*-----------------------------------------------------------------------------
 * Create subscription complete
 */
void Session::CreateSubscriptionWorkerComplete(uv_work_t* req)
{
  HandleScope scope;

  CreateSubscriptionRequest* request = (CreateSubscriptionRequest*)req->data;
  delete req;

  Handle<Value> argv[2];
  if (request->result == TVA_OK)
  {
    argv[0] = Undefined();
    argv[1] = Local<Value>::New(Subscription::NewInstance(request->subscription));
    request->subscription->MarkInUse(true);
  }
  else
  {
    argv[0] = String::New(tvaErrToStr(request->result));
    argv[1] = Undefined();
  }

  TryCatch tryCatch;

  request->complete->Call(Context::GetCurrent()->Global(), 2, argv);
  if (tryCatch.HasCaught())
  {
    node::FatalException(tryCatch);
  }

  delete request;
}


/*****     CreateReplay     *****/

struct CreateReplayRequest
{
  Session* session;
  Replay* replay;
  char* topic;
  TVA_UINT64 startTime;
  TVA_UINT64 endTime;
  TVA_STATUS result;
  Persistent<Function> complete;

  CreateReplayRequest()
  {
    topic = NULL;
    startTime = 0;
    endTime = 0;
  }

  ~CreateReplayRequest()
  {
    if (topic) free(topic);
    if (!complete.IsEmpty()) complete.Dispose();
  }
};

bool CreateReplayParseOptions(Local<Object> options, CreateReplayRequest* request);

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
Handle<Value> Session::CreateReplay(const Arguments& args)
{
  HandleScope scope;
  Session* session = ObjectWrap::Unwrap<Session>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(3, args.Length());
  PARAM_REQ_STRING(0, args);        // topic
  PARAM_REQ_OBJECT(1, args);        // options
  PARAM_REQ_FUNCTION(2, args);      // complete

  // Ready arguments
  String::AsciiValue topic(args[0]->ToString());
  Local<Object> options = Local<Object>::Cast(args[1]);
  Local<Function> complete = Local<Function>::Cast(args[2]);

  CreateReplayRequest* request = new CreateReplayRequest();
  request->session = session;
  request->topic = strdup(*topic);
  request->complete = Persistent<Function>::New(complete);

  if (!CreateReplayParseOptions(options, request))
  {
    delete request;
    ThrowException(Exception::TypeError(String::New("Incomplete options")));
    return scope.Close(Undefined());
  }

  // Post work request
  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, Session::CreateReplayWorker, Session::CreateReplayWorkerComplete);

  return scope.Close(args.This());
}

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
Handle<Value> Session::CreateReplaySync(const Arguments& args)
{
  HandleScope scope;
  Session* session = ObjectWrap::Unwrap<Session>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(2, args.Length());
  PARAM_REQ_STRING(0, args);        // topic
  PARAM_REQ_OBJECT(1, args);        // options

  // Ready arguments
  String::AsciiValue topic(args[0]->ToString());
  Local<Object> options = Local<Object>::Cast(args[1]);

  CreateReplayRequest request;
  request.session = session;
  request.topic = strdup(*topic);

  if (!CreateReplayParseOptions(options, &request))
  {
    ThrowException(Exception::TypeError(String::New("Incomplete options")));
    return scope.Close(Undefined());
  }

  // Call CreateSubscriptionWorker synchronously
  uv_work_t req;
  req.data = &request;

  CreateReplayWorker(&req);

  // Return result - Subscription object if successful, else error string
  Handle<Value> result;
  if (request.result == TVA_OK)
  {
    result = Local<Value>::New(Replay::NewInstance(request.replay));
    request.replay->MarkInUse(true);
  }
  else
  {
    result = String::New(tvaErrToStr(request.result));
  }

  return scope.Close(result);
}

/*-----------------------------------------------------------------------------
 * Parse options
 */
bool CreateReplayParseOptions(Local<Object> options, CreateReplayRequest* request)
{
  std::vector<std::string> optionNames = cvv8::CastFromJS<std::vector<std::string> >(options->GetPropertyNames());
  for (size_t i = 0; i < optionNames.size(); i++)
  {
    char* optionName = (char*)(optionNames[i].c_str());
    Local<Value> optionValue = options->Get(String::NewSymbol(optionName));

    if (optionValue->IsUndefined())
    {
      continue;
    }

    if (tva_str_casecmp(optionName, "startTime") == 0)
    {
      request->startTime = (TVA_UINT64)(optionValue->NumberValue() * 1000);
    }
    else if (tva_str_casecmp(optionName, "endTime") == 0)
    {
      request->endTime = (TVA_UINT64)(optionValue->NumberValue() * 1000);
    }
  }

  if ((request->startTime == 0) || (request->endTime == 0))
  {
    return false;
  }

  return true;
}

/*-----------------------------------------------------------------------------
 * Perform create replay
 */
void Session::CreateReplayWorker(uv_work_t* req)
{
  CreateReplayRequest* request = (CreateReplayRequest*)req->data;
  Session* session = request->session;
  Replay* replay = new Replay(session);

  TVA_REPLAY_HANDLE replayHandle;
  TVA_REPLAY_REQ replayReq;

  replayReq.pubId = TVA_REPLAY_PUBID_ANY;
  replayReq.sessionId = TVA_REPLAY_SESSIONID_ANY;
  replayReq.timeStart = request->startTime;
  replayReq.timeEnd = request->endTime;
  replayReq.tsnStart = TVA_REPLAY_TSN_ANY;
  replayReq.tsnEnd = TVA_REPLAY_TSN_ANY;
  tva_strncpy(replayReq.topic, request->topic, sizeof(replayReq.topic));

  TVA_STATUS rc = tvaReplayHistCbNew(session->GetHandle(), Replay::MessageReceivedEvent, replay,
                                     &replayReq, 0, &replayHandle);
  if (rc == TVA_OK)
  {
    request->replay = replay;
    replay->SetHandle(replayHandle);
  }
  else
  {
    delete replay;
  }

  request->result = rc;
}

/*-----------------------------------------------------------------------------
 * Create replay complete
 */
void Session::CreateReplayWorkerComplete(uv_work_t* req)
{
  HandleScope scope;

  CreateReplayRequest* request = (CreateReplayRequest*)req->data;
  delete req;

  Handle<Value> argv[2];
  if (request->result == TVA_OK)
  {
    argv[0] = Undefined();
    argv[1] = Local<Value>::New(Replay::NewInstance(request->replay));
    request->replay->MarkInUse(true);
  }
  else
  {
    argv[0] = String::New(tvaErrToStr(request->result));
    argv[1] = Undefined();
  }

  TryCatch tryCatch;

  request->complete->Call(Context::GetCurrent()->Global(), 2, argv);
  if (tryCatch.HasCaught())
  {
    node::FatalException(tryCatch);
  }

  delete request;
}
