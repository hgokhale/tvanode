#include <stdlib.h>
#include "v8-convert.hpp"
#include "DataTypes.h"
#include "Helpers.h"
#include "Session.h"
#include "Replay.h"

using namespace v8;

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

  replay->SetEventHandler(*evt, handler);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
 * Set subscription event handler
 */
void Replay::SetEventHandler(char* evt, Local<Function> handler)
{
  if (tva_str_casecmp(evt, "message") == 0)
  {
    _messageHandler.push_back(Persistent<Function>::New(handler));
  }
  else if (tva_str_casecmp(evt, "finish") == 0)
  {
    _finishHandler.push_back(Persistent<Function>::New(handler));
  }
  else if (tva_str_casecmp(evt, "error") == 0)
  {
    _errorHandler.push_back(Persistent<Function>::New(handler));
  }
}


/*****     MessageEvent     *****/

/*-----------------------------------------------------------------------------
 * Replay message received callback
 */
void Replay::MessageReceivedEvent(TVA_MESSAGE* message, void* context)
{
  Replay* replay = (Replay*)context;
  TVA_STATUS rc = TVA_ERROR;

  MessageEvent messageEvent;
  messageEvent.tvaMessage = message;
  messageEvent.generationTime = message->msgGenerationTime;
  messageEvent.receiveTime = message->msgReceiveTime;
  messageEvent.isLastMessage = (TVA_MSG_ISLAST(message) != 0);
  tva_strncpy(messageEvent.topic, message->topicName, sizeof(messageEvent.topic));

  TVA_MESSAGE_DATA_HANDLE msgData = message->messageData;
  TVA_FIELD_ITERATOR_HANDLE fieldItr;
  TVA_MSG_FIELD_INFO fieldInfo;

  do
  {
    rc = tvaCreateMessageFieldIterator(msgData, &fieldItr);
    if (rc != TVA_OK) break;

    rc = tvaMsgFieldNext(fieldItr, &fieldInfo);
    while (rc == TVA_OK)
    {
      MessageFieldData field;
      char* fieldName;
      rc = tvaGetFieldNameFromFieldId(msgData, fieldInfo.fieldId, &fieldName);
      if (rc == TVA_OK)
      {
        tva_strncpy(field.name, fieldName, sizeof(field.name));
        tvaReleaseFieldName(fieldName);
      }
      else
      {
        field.name[0] = 0;
      }

      switch (fieldInfo.fieldType)
      {
      case FIELD_TYPE_BOOLEAN:
        {
          TVA_BOOLEAN val;
          rc = tvaGetBooleanFromMessageByFieldId(msgData, fieldInfo.fieldId, &val);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeBoolean;
            field.value.boolValue = (val) ? true : false;
          }
        }
        break;

      case FIELD_TYPE_BYTE:
        {
          TVA_UINT8 val;
          rc = tvaGetByteFromMessageByFieldId(msgData, fieldInfo.fieldId, &val);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeNumber;
            field.value.numberValue = (double)val;
          }
        }
        break;

      case FIELD_TYPE_SHORT:
        {
          TVA_INT16 val;
          rc = tvaGetShortFromMessageByFieldId(msgData, fieldInfo.fieldId, &val);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeNumber;
            field.value.numberValue = (double)val;
          }
        }
        break;

      case FIELD_TYPE_INTEGER:
        {
          TVA_INT32 val;
          rc = tvaGetIntFromMessageByFieldId(msgData, fieldInfo.fieldId, &val);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeNumber;
            field.value.numberValue = (double)val;
          }
        }
        break;

      case FIELD_TYPE_LONG:
        {
          TVA_INT64 val;
          rc = tvaGetLongFromMessageByFieldId(msgData, fieldInfo.fieldId, &val);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeNumber;
            field.value.numberValue = (double)val;
          }
        }
        break;

      case FIELD_TYPE_FLOAT:
        {
          TVA_FLOAT val;
          rc = tvaGetFloatFromMessageByFieldId(msgData, fieldInfo.fieldId, &val);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeNumber;
            field.value.numberValue = (double)val;
          }
        }
        break;

      case FIELD_TYPE_DOUBLE:
        {
          TVA_DOUBLE val;
          rc = tvaGetDoubleFromMessageByFieldId(msgData, fieldInfo.fieldId, &val);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeNumber;
            field.value.numberValue = (double)val;
          }
        }
        break;

      case FIELD_TYPE_DATETIME:
        {
          TVA_DATE val;
          rc = tvaGetDateTimeFromMessageByFieldId(msgData, fieldInfo.fieldId, &val);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeDate;
            field.value.dateValue = val;
          }
        }
        break;

      case FIELD_TYPE_STRING:
        {
          TVA_STRING val;
          rc = tvaGetStringFromMessageByFieldId(msgData, fieldInfo.fieldId, &val);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeString;
            field.value.stringValue = val;
          }
        }
        break;

      default:
        rc = TVA_ERR_NOT_IMPLEMENTED;
        break;
      }

      if (rc == TVA_OK)
      {
        messageEvent.fieldData.push_back(field);
      }

      rc = tvaMsgFieldNext(fieldItr, &fieldInfo);
    }

    if (rc == TVA_ERR_NO_FIELDS_REMAINING)
    {
      rc = TVA_OK;
    }
  } while (0);

  tvaReleaseMessageData(message);

  if (rc == TVA_OK)
  {
    replay->PostMessageEvent(messageEvent);
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
  size_t fieldCount = messageEvent.fieldData.size();
  Local<Object> fields = Object::New();
  for (size_t i = 0; i < fieldCount; i++)
  {
    MessageFieldData* field = &messageEvent.fieldData[i];
    switch (field->type)
    {
    case MessageFieldDataTypeBoolean:
      fields->Set(String::NewSymbol(field->name), Boolean::New(field->value.boolValue));
      break;

    case MessageFieldDataTypeNumber:
      fields->Set(String::NewSymbol(field->name), Number::New(field->value.numberValue));
      break;

    case MessageFieldDataTypeDate:
      fields->Set(String::NewSymbol(field->name), Date::New((double)(field->value.dateValue.timeInMicroSecs / 1000)));
      break;

    case MessageFieldDataTypeString:
      fields->Set(String::NewSymbol(field->name), String::New(field->value.stringValue));
      tvaReleaseFieldValue(field->value.stringValue);
      break;

    default:
      break;
    }
  }

  Local<Object> message = Object::New();
  message->Set(String::NewSymbol("topic"), String::New(messageEvent.topic));
  message->Set(String::NewSymbol("generationTime"), Date::New((double)(messageEvent.generationTime / 1000)));
  message->Set(String::NewSymbol("receiveTime"), Date::New((double)(messageEvent.receiveTime / 1000)));
  message->Set(String::NewSymbol("fields"), fields);
  message->Set(String::NewSymbol("reserved"), Number::New((double)((intptr_t)(messageEvent.tvaMessage))));

  Handle<Value> argv[1];
  argv[0] = message;

  for (size_t i = 0; i < _messageHandler.size(); i++)
  {
    TryCatch tryCatch;
    _messageHandler[i]->Call(context, 1, argv);
    if (tryCatch.HasCaught())
    {
      node::FatalException(tryCatch);
    }
  }

  if (messageEvent.isLastMessage)
  {
    for (size_t i = 0; i < _finishHandler.size(); i++)
    {
      TryCatch tryCatch;
      _finishHandler[i]->Call(context, 1, argv);
      if (tryCatch.HasCaught())
      {
        node::FatalException(tryCatch);
      }
    }

    MarkInUse(false);
  }
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

  for (size_t i = 0; i < _errorHandler.size(); i++)
  {
    TryCatch tryCatch;
    _errorHandler[i]->Call(context, 1, argv);
    if (tryCatch.HasCaught())
    {
      node::FatalException(tryCatch);
    }
  }
}


/*****     Pause/Resume     *****/

struct ReplayPauseResumeRequest
{
  Replay* replay;
  bool isPause;
  TVA_STATUS result;
  Persistent<Function> complete;
};

/*-----------------------------------------------------------------------------
 * Pause the replay
 *
 * replay.pause(function (err) {
 *     // Replay pause complete
 * });
 */
Handle<Value> Replay::Pause(const Arguments& args)
{
  HandleScope scope;
  Replay* replay = ObjectWrap::Unwrap<Replay>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(1, args.Length());
  PARAM_REQ_FUNCTION(0, args);        // complete

  // Ready arguments
  Local<Function> complete = Local<Function>::Cast(args[0]);

  // Send data to worker thread
  ReplayPauseResumeRequest* request = new ReplayPauseResumeRequest;
  request->replay = replay;
  request->isPause = true;
  request->complete = Persistent<Function>::New(complete);

  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, Replay::PauseResumeWorker, Replay::PauseResumeWorkerComplete);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
 * Resume the replay
 *
 * replay.resume(function (err) {
 *     // Replay resume complete
 * });
 */
Handle<Value> Replay::Resume(const Arguments& args)
{
  HandleScope scope;
  Replay* replay = ObjectWrap::Unwrap<Replay>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(1, args.Length());
  PARAM_REQ_FUNCTION(0, args);        // complete

  // Ready arguments
  Local<Function> complete = Local<Function>::Cast(args[0]);

  // Send data to worker thread
  ReplayPauseResumeRequest* request = new ReplayPauseResumeRequest;
  request->replay = replay;
  request->isPause = false;
  request->complete = Persistent<Function>::New(complete);

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

  request->complete->Call(Context::GetCurrent()->Global(), 1, argv);
  request->complete.Dispose();
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
  Persistent<Function> complete;
};

/*-----------------------------------------------------------------------------
 * Stop the replay
 *
 * replay.stop(function (err) {
 *     // Replay stop complete
 * });
 */
Handle<Value> Replay::Stop(const Arguments& args)
{
  HandleScope scope;
  Replay* replay = ObjectWrap::Unwrap<Replay>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(1, args.Length());
  PARAM_REQ_FUNCTION(0, args);        // complete

  // Ready arguments
  Local<Function> complete = Local<Function>::Cast(args[0]);

  // Send data to worker thread
  ReplayStopRequest* request = new ReplayStopRequest;
  request->replay = replay;
  request->complete = Persistent<Function>::New(complete);

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

  request->complete->Call(Context::GetCurrent()->Global(), 1, argv);
  request->complete.Dispose();
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
