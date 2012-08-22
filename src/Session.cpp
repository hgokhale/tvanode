#include <vector>
#include <string>
#include <stdlib.h>
#include "v8-convert.hpp"
#include "Helpers.h"
#include "Session.h"
#include "Publication.h"
#include "Subscription.h"

using namespace v8;

Persistent<Function> Session::constructor;

/*-----------------------------------------------------------------------------
 * Initialize the Session module
 */
void Session::Init(Handle<Object> target)
{
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(New);
  t->SetClassName(String::NewSymbol("Session"));
  t->InstanceTemplate()->SetInternalFieldCount(1);

  t->PrototypeTemplate()->Set(String::NewSymbol("on"), FunctionTemplate::New(On)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("close"), FunctionTemplate::New(Close)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("createPublication"), FunctionTemplate::New(CreatePublication)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("createSubscription"), FunctionTemplate::New(CreateSubscription)->GetFunction());

  constructor = Persistent<Function>::New(t->GetFunction());
}

/*-----------------------------------------------------------------------------
 * Construct a new Session object
 */
Handle<Value> Session::New(const Arguments& args)
{
  HandleScope scope;
  Session* session = (Session*)External::Unwrap(args[0]->ToObject());
  session->Wrap(args.This());
  return args.This();
}

Handle<Value> Session::NewInstance(Session* session)
{
  HandleScope scope;
  Handle<External> wrapper = External::New(session);
  Handle<Value> argv[1] = { wrapper };
  Local<Object> instance = constructor->NewInstance(1, argv);

  return scope.Close(instance);
}

/*-----------------------------------------------------------------------------
 * Constructor & Destructor
 */
Session::Session()
{
  _handle = TVA_INVALID_HANDLE;
  _gdHandle = TVA_INVALID_HANDLE;
  _async.data = this;
  _gdAckWindow = NULL;
  uv_mutex_init(&_sessionEventLock);
  uv_mutex_init(&_gdSendLock);
}

Session::~Session()
{
  if (_gdHandle != TVA_INVALID_HANDLE)
  {
    tvagdContextTerm(_gdHandle);
  }
  if (_handle != TVA_INVALID_HANDLE)
  {
    tvaSessionTerm(_handle);
  }
  if (_gdAckWindow != NULL)
  {
    delete[] _gdAckWindow;
  }
  uv_mutex_destroy(&_sessionEventLock);
  uv_mutex_destroy(&_gdSendLock);
}


/*****     On     *****/

/*-----------------------------------------------------------------------------
 * Register for session events
 *
 * session.on(event, listener);
 *
 * Events / Listeners:
 *   'connection-lost'      - Connection lost (will auto-reconnect)   - function () { }
 *   'connection-restored'  - Reconnected after connection lost       - function () { }
 *   'close'                - Session closed                          - function () { }
 *   'connect-info'         - Initial connection info                 - function (activeTmx, standbyTmx) { }
 *   'gds-lost'             - GDS communications lost                 - function () { }
 *   'gds-restored'         - GDS communications restored             - function () { }
 *   'notify'               - Misc. session notification event        - function (code, msg) { }
 */
Handle<Value> Session::On(const Arguments& args)
{
  HandleScope scope;
  Session* session = ObjectWrap::Unwrap<Session>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(2, args.Length());
  PARAM_REQ_STRING(0, args);          // event
  PARAM_REQ_FUNCTION(1, args);        // handler

  String::AsciiValue evt(args[0]->ToString());
  Local<Function> handler = Local<Function>::Cast(args[1]);

  session->SetEventHandler(*evt, handler);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
 * Set session event handler
 */
void Session::SetEventHandler(char* evt, Local<Function> handler)
{
  if (tva_str_casecmp(evt, "connection-lost") == 0)
  {
    _disconnectHandler.push_back(Persistent<Function>::New(handler));
  }
  else if (tva_str_casecmp(evt, "connection-restored") == 0)
  {
    _reconnectHandler.push_back(Persistent<Function>::New(handler));
  }
  else if (tva_str_casecmp(evt, "close") == 0)
  {
    _terminateHandler.push_back(Persistent<Function>::New(handler));
  }
  else if (tva_str_casecmp(evt, "connect-info") == 0)
  {
    _connInfoHandler.push_back(Persistent<Function>::New(handler));
  }
  else if (tva_str_casecmp(evt, "gds-lost") == 0)
  {
    _gdsLostHandler.push_back(Persistent<Function>::New(handler));
  }
  else if (tva_str_casecmp(evt, "gds-restored") == 0)
  {
    _gdsRestoreHandler.push_back(Persistent<Function>::New(handler));
  }
  else if (tva_str_casecmp(evt, "notify") == 0)
  {
    _defaultHandler.push_back(Persistent<Function>::New(handler));
  }
}


/*****     Close     *****/

struct CloseRequest
{
  Session* session;
  TVA_STATUS result;
  Persistent<Function> complete;
};

/*-----------------------------------------------------------------------------
 * Disconnect from the Tervela fabric
 *
 * session.close(function (err) {
 *     // Close complete
 * });
 */
Handle<Value> Session::Close(const Arguments& args)
{
  HandleScope scope;
  Session* session = ObjectWrap::Unwrap<Session>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(1, args.Length());
  PARAM_REQ_FUNCTION(0, args);        // complete

  // Ready arguments
  Local<Function> complete = Local<Function>::Cast(args[0]);

  // Send data to worker thread
  CloseRequest* request = new CloseRequest;
  request->session = session;
  request->complete = Persistent<Function>::New(complete);

  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, Session::CloseWorker, Session::CloseWorkerComplete);

  return scope.Close(Undefined());
}

/*-----------------------------------------------------------------------------
 * Perform logout
 */
void Session::CloseWorker(uv_work_t* req)
{
  CloseRequest* request = (CloseRequest*)req->data;
  Session* session = request->session;

  request->result = session->Terminate();
}

/*-----------------------------------------------------------------------------
 * Perform logout
 */
TVA_STATUS Session::Terminate()
{
  TVA_STATUS rc;

  if (_gdHandle != TVA_INVALID_HANDLE)
  {
    tvagdContextTerm(_gdHandle);
    _gdHandle = TVA_INVALID_HANDLE;
  }

  if (_gdAckWindow)
  {
    delete[] _gdAckWindow;
    _gdAckWindow = NULL;
  }

  rc = tvaSessionTerm(_handle);
  _handle = TVA_INVALID_HANDLE;

  return rc;
}

/*-----------------------------------------------------------------------------
 * Close complete
 */
void Session::CloseWorkerComplete(uv_work_t* req)
{
  HandleScope scope;

  CloseRequest* request = (CloseRequest*)req->data;
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

  request->complete->Call(Context::GetCurrent()->Global(), 1, argv);
  request->complete.Dispose();
  if (tryCatch.HasCaught())
  {
    node::FatalException(tryCatch);
  }

  delete request;
}


/*-----------------------------------------------------------------------------
 * Tervela session notification callback
 */
void Session::SessionNotificationCallback(void* context, TVA_STATUS code, void* data)
{
  Session* session = (Session*)context;

  SessionNotificaton notification;
  notification.code = code;

  if (code == TVA_EVT_GD_ACK_RECV)
  {
    notification.data.messageId = *((TVA_UINT32*)data);
  }
  else if ((code == TVA_EVT_TMX_CONN_SINGLE) || (code == TVA_EVT_TMX_CONN_FT))
  {
    uint32_t* addrs = (uint32_t*)data;

    notification.data.addresses[0].s_addr = addrs[0];
    if (code == TVA_EVT_TMX_CONN_FT)
    {
      notification.data.addresses[1].s_addr = addrs[1];
    }
    else
    {
      notification.data.addresses[1].s_addr = 0;
    }
  }

  session->PostSessionEvent(notification);
}

/*-----------------------------------------------------------------------------
 * Post async session notification event to JavaScript
 */
void Session::SessionNotificationAsyncEvent(uv_async_t* async, int status)
{
  HandleScope scope;
  Session* session = (Session*)async->data;
  SessionNotificaton notification;

  Local<Object> context = Context::GetCurrent()->Global();
  while (session->GetNextSessionEvent(notification))
  {
    session->InvokeJsSessionNotification(context, notification);
  }
}

/*-----------------------------------------------------------------------------
 * Post async session notification event to JavaScript
 */
void Session::InvokeJsSessionNotification(Local<Object> context, SessionNotificaton& notification)
{
  bool handled = false;

  TVA_UINT32 code = notification.code;
  if ((code == TVA_EVT_GD_ACK_RECV) ||
      (code == TVA_ERR_GD_MSG_TIMEOUT) ||
      (code == TVA_ERR_GD_MSG_TOO_MANY_RETRANSMITS))
  {
    Handle<Value> argv[1];

    if (code == TVA_EVT_GD_ACK_RECV)
    {
      argv[0] = Undefined();
    }
    else
    {
      argv[0] = String::New(tvaErrToStr(code));
    }

    TryCatch tryCatch;

    GdAckWindowEntry* entry = &_gdAckWindow[notification.data.messageId];
    entry->complete->Call(context, 1, argv);
    if (tryCatch.HasCaught())
    {
      node::FatalException(tryCatch);
    }

    entry->complete.Dispose();
    handled = true;
  }
  else if ((code == TVA_EVT_TMX_CONN_SINGLE) || (code == TVA_EVT_TMX_CONN_FT))
  {
    Handle<Value> argv[2];

    argv[0] = String::New(inet_ntoa(notification.data.addresses[0]));
    if (code == TVA_EVT_TMX_CONN_FT)
    {
      argv[1] = String::New(inet_ntoa(notification.data.addresses[1]));
    }
    else
    {
      argv[1] = Undefined();
    }

    for (size_t i = 0; i < _connInfoHandler.size(); i++)
    {
      TryCatch tryCatch;

      _connInfoHandler[i]->Call(context, 2, argv);
      if (tryCatch.HasCaught())
      {
        node::FatalException(tryCatch);
      }
    }

    handled = (bool)(_connInfoHandler.size() > 0);
  }
  else if (code == TVA_ERR_TMX_FAILED)
  {
    Handle<Value> argv[1] = { Undefined() };
    for (size_t i = 0; i < _disconnectHandler.size(); i++)
    {
      TryCatch tryCatch;

      _disconnectHandler[i]->Call(context, 0, argv);
      if (tryCatch.HasCaught())
      {
        node::FatalException(tryCatch);
      }
    }

    handled = (bool)(_disconnectHandler.size() > 0);
  }
  else if (code == TVA_EVT_TMX_RECONNECT)
  {
    Handle<Value> argv[1] = { Undefined() };
    for (size_t i = 0; i < _reconnectHandler.size(); i++)
    {
      TryCatch tryCatch;

      _reconnectHandler[i]->Call(context, 0, argv);
      if (tryCatch.HasCaught())
      {
        node::FatalException(tryCatch);
      }
    }

    handled = (bool)(_reconnectHandler.size() > 0);
  }
  else if (code == TVA_ERR_GDS_COMM_LOST)
  {
    Handle<Value> argv[1] = { Undefined() };
    for (size_t i = 0; i < _gdsLostHandler.size(); i++)
    {
      TryCatch tryCatch;

      _gdsLostHandler[i]->Call(context, 0, argv);
      if (tryCatch.HasCaught())
      {
        node::FatalException(tryCatch);
      }
    }

    handled = (bool)(_gdsLostHandler.size() > 0);
  }
  else if (code == TVA_EVT_GDS_COMM_RESTORED)
  {
    Handle<Value> argv[1] = { Undefined() };
    for (size_t i = 0; i < _gdsRestoreHandler.size(); i++)
    {
      TryCatch tryCatch;

      _gdsRestoreHandler[i]->Call(context, 0, argv);
      if (tryCatch.HasCaught())
      {
        node::FatalException(tryCatch);
      }
    }

    handled = (bool)(_gdsRestoreHandler.size() > 0);
  }
  else if (code == TVA_EVT_SESSION_TERMINATED)
  {
    // Notify application
    Handle<Value> argv[1] = { Undefined() };
    for (size_t i = 0; i < _terminateHandler.size(); i++)
    {
      TryCatch tryCatch;

      _terminateHandler[i]->Call(context, 0, argv);
      if (tryCatch.HasCaught())
      {
        node::FatalException(tryCatch);
      }
    }

    handled = (bool)(_terminateHandler.size() > 0);

    // Perform cleanup
    uv_close((uv_handle_t*)GetAsyncObj(), Session::SessionHandleCloseComplete);

    for (size_t i = 0; i < _disconnectHandler.size(); i++)
    {
      _disconnectHandler[i].Dispose();
    }
    for (size_t i = 0; i < _reconnectHandler.size(); i++)
    {
      _reconnectHandler[i].Dispose();
    }
    for (size_t i = 0; i < _terminateHandler.size(); i++)
    {
      _terminateHandler[i].Dispose();
    }
    for (size_t i = 0; i < _defaultHandler.size(); i++)
    {
      _defaultHandler[i].Dispose();
    }
  }
  
  if (!handled)
  {
    Handle<Value> argv[2];
    argv[0] = Number::New(code);
    argv[1] = String::New(tvaErrToStr(code));

    for (size_t i = 0; i < _defaultHandler.size(); i++)
    {
      TryCatch tryCatch;

      _defaultHandler[i]->Call(context, 2, argv);
      if (tryCatch.HasCaught())
      {
        node::FatalException(tryCatch);
      }
    }
  }
}

/*-----------------------------------------------------------------------------
 * Async handle close complete
 */
void Session::SessionHandleCloseComplete(uv_handle_t* handle)
{
}

/*-----------------------------------------------------------------------------
 * Create a new subscription object
 */
Handle<Value> Session::CreateSubscription(const Arguments& args)
{
  HandleScope scope;
  return scope.Close(Subscription::NewInstance(args.This()));
}

/*****     Start     *****/

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

/*-----------------------------------------------------------------------------
 * Send a GD message
 */
TVA_STATUS Session::SendGdMessage(Publication* publisher, TVA_PUBLISH_MESSAGE_DATA_HANDLE messageData, Persistent<Function> complete)
{
  TVA_STATUS rc = TVA_ERR_GD_ONLY;

  if (_gdHandle != TVA_INVALID_HANDLE)
  {
    uv_mutex_lock(&_gdSendLock);
    int idx = _gdAckWindowIdx;
    _gdAckWindowIdx = (_gdAckWindowIdx + 1) % _gdAckWindowSize;
    uv_mutex_unlock(&_gdSendLock);

    GdAckWindowEntry* entry = &_gdAckWindow[idx];
    entry->publisher = publisher;
    entry->complete = complete;
    rc = tvagdMsgSend(_gdHandle, messageData, idx);
  }

  return rc;
}
