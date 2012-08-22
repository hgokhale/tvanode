#include <stdlib.h>
#include "v8-convert.hpp"
#include "DataTypes.h"
#include "Helpers.h"
#include "Session.h"
#include "Subscription.h"

using namespace v8;

Persistent<Function> Subscription::constructor;


/*-----------------------------------------------------------------------------
 * Initialize the Subscription module
 */
void Subscription::Init(Handle<Object> target)
{
  HandleScope scope;

  Local<FunctionTemplate> t = FunctionTemplate::New(New);
  t->SetClassName(String::NewSymbol("Subscription"));
  t->InstanceTemplate()->SetInternalFieldCount(1);

  t->PrototypeTemplate()->Set(String::NewSymbol("start"), FunctionTemplate::New(Start)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("on"), FunctionTemplate::New(On)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("ackMessage"), FunctionTemplate::New(AckMessage)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("stop"), FunctionTemplate::New(Stop)->GetFunction());

  constructor = Persistent<Function>::New(t->GetFunction());
}

/*-----------------------------------------------------------------------------
 * Construct a new Subscription object
 */
Handle<Value> Subscription::New(const Arguments& args)
{
  HandleScope scope;
  Session* session = ObjectWrap::Unwrap<Session>(args[0]->ToObject());
  Subscription* sub = new Subscription(session);
  sub->Wrap(args.This());
  return args.This();
}

Handle<Value> Subscription::NewInstance(const Local<Object> session)
{
  HandleScope scope;
  Handle<Value> argv[1] = { session };
  Local<Object> instance = constructor->NewInstance(1, argv);

  return scope.Close(instance);
}

/*-----------------------------------------------------------------------------
 * Constructor & Destructor
 */
Subscription::Subscription(Session* session)
{
  _async.data = this;
  _session = session;
  _handle = TVA_INVALID_HANDLE;
  uv_mutex_init(&_messageEventLock);
}

Subscription::~Subscription()
{
  uv_mutex_destroy(&_messageEventLock);
}


/*****     Start     *****/

struct StartRequest
{
  Subscription* subscription;
  char* topic;
  char* name;
  TVA_UINT32 qos;
  TVA_STATUS result;
  Subscription::GdSubscriptionAckMode gdAckMode;
  TVA_SUBSCRIPTION_HANDLE subHandle;
};

/*-----------------------------------------------------------------------------
 * Start the subscription, get ready to receive messages
 *
 * subscription.start({
 *    topic         : [topic name],                          (string, required)
 *    qos           : [quality of service: "BE"|"GC"|"GD"],  (string, optional (default: "GC"))
 *    name          : [subscription name],                   (string, only required when using GD)
 *    ackMode       : [message ack mode: "auto"|"manual"],   (string, only required when using GD)
 *    messageEvent  : [callback on message recv],            (function (message), required)
 *    complete      : [callback on start complete],          (function (err), required)
 * });
 */
Handle<Value> Subscription::Start(const Arguments& args)
{
  HandleScope scope;
  Subscription* subscription = ObjectWrap::Unwrap<Subscription>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(1, args.Length());
  PARAM_REQ_STRING(0, args);        // topic

  // Ready arguments
  String::AsciiValue topic(args[0]->ToString());
  Local<Function> complete;
  TVA_UINT32 qos = TVA_QOS_GUARANTEED_CONNECTED;
  char* name = NULL;
  GdSubscriptionAckMode gdAckMode = GdSubscriptionAckModeAuto;

  if (args.Length() > 1) {
    PARAM_REQ_OBJECT(1, args);      // options
    Local<Object> options = Local<Object>::Cast(args[1]);

    // Get options data
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
        name = strdup(*val);
      }
      else if (tva_str_casecmp(optionName, "qos") == 0)
      {
        String::AsciiValue val(optionValue->ToString());
        if (tva_str_casecmp(*val, "BE") == 0)
        {
          qos = TVA_QOS_BEST_EFFORT;
        }
        else if (tva_str_casecmp(*val, "GD") == 0)
        {
          qos = TVA_QOS_GUARANTEED_DELIVERY;
        }
      }
      else if (tva_str_casecmp(optionName, "ackMode") == 0)
      {
        String::AsciiValue val(optionValue->ToString());
        if (tva_str_casecmp(*val, "manual") == 0)
        {
          gdAckMode = GdSubscriptionAckModeManual;
        }
      }
    }
  }

  // If using GD make sure name is specified
  if ((qos == TVA_QOS_GUARANTEED_DELIVERY) && (!name))
  {
    ThrowException(Exception::TypeError(String::New("Incomplete options")));
    return scope.Close(Undefined());
  }

  // Send data to worker thread
  StartRequest* request = new StartRequest;
  request->subscription = subscription;
  request->qos = qos;
  request->gdAckMode = gdAckMode;
  request->topic = strdup(*topic);
  request->name = name;

  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, StartWorker, StartWorkerComplete);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
 * Perform create subscription
 */
void Subscription::StartWorker(uv_work_t* req)
{
  StartRequest* request = (StartRequest*)req->data;
  Subscription* subscription = request->subscription;

  TVA_STATUS rc;
  TVA_HANDLE subHandle = TVA_INVALID_HANDLE;
  
  if (request->qos == TVA_QOS_GUARANTEED_DELIVERY)
  {
    rc = tvagdSubCbNew(subscription->GetSession()->GetGdHandle(), request->name, request->topic,
                       Subscription::MessageReceivedEvent, subscription, &subHandle);
  }
  else
  {
    int cachePeriod = 3000;
    if (request->qos == TVA_QOS_BEST_EFFORT)
    {
      cachePeriod = 50;
    }

    rc = tvaSubscribeWithCallbackEx(request->topic, Subscription::MessageReceivedEvent, subscription,
                                    subscription->GetSession()->GetHandle(), request->qos, true, cachePeriod, &subHandle);
  }

  if (rc == TVA_OK)
  {
    subscription->SetQos(request->qos);
    subscription->SetAckMode(request->gdAckMode);
    subscription->SetHandle(subHandle);
  }

  request->result = rc;
}

/*-----------------------------------------------------------------------------
 * Create subscription complete
 */
void Subscription::StartWorkerComplete(uv_work_t* req)
{
  HandleScope scope;

  StartRequest* request = (StartRequest*)req->data;
  delete req;

  request->subscription->InvokeJsStartEvent(Context::GetCurrent()->Global(), request->result);

  uv_async_init(uv_default_loop(), request->subscription->GetAsyncObj(), MessageAsyncEvent);

  free(request->topic);
  delete request;
}

/*-----------------------------------------------------------------------------
 * Invoke 'start' event handler
 */
void Subscription::InvokeJsStartEvent(Local<Object> context, TVA_STATUS rc)
{
  Handle<Value> argv[1];
  if (rc == TVA_OK)
  {
    argv[0] = Undefined();
  }
  else
  {
    argv[0] = String::New(tvaErrToStr(rc));
  }

  for (size_t i = 0; i < _startHandler.size(); i++)
  {
    TryCatch tryCatch;

    _startHandler[i]->Call(context, 1, argv);
    if (tryCatch.HasCaught())
    {
      node::FatalException(tryCatch);
    }
  }
}


/*****     On     *****/

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
Handle<Value> Subscription::On(const Arguments& args)
{
  HandleScope scope;
  Subscription* subscription = ObjectWrap::Unwrap<Subscription>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(2, args.Length());
  PARAM_REQ_STRING(0, args);          // event
  PARAM_REQ_FUNCTION(1, args);        // handler

  String::AsciiValue evt(args[0]->ToString());
  Local<Function> handler = Local<Function>::Cast(args[1]);

  subscription->SetEventHandler(*evt, handler);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
 * Set subscription event handler
 */
void Subscription::SetEventHandler(char* evt, Local<Function> handler)
{
  if (tva_str_casecmp(evt, "start") == 0)
  {
    _startHandler.push_back(Persistent<Function>::New(handler));
  }
  else if (tva_str_casecmp(evt, "message") == 0)
  {
    _messageHandler.push_back(Persistent<Function>::New(handler));
  }
}


/*****     MessageEvent     *****/

/*-----------------------------------------------------------------------------
 * Subscription message received callback
 */
void Subscription::MessageReceivedEvent(TVA_MESSAGE* message, void* context)
{
  Subscription* subscription = (Subscription*)context;
  TVA_STATUS rc = TVA_ERROR;

  MessageEvent messageEvent;
  messageEvent.tvaMessage = message;
  messageEvent.generationTime = message->msgGenerationTime;
  messageEvent.receiveTime = message->msgReceiveTime;
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

  if (subscription->GetQos() != TVA_QOS_GUARANTEED_DELIVERY)
  {
    tvaReleaseMessageData(message);
  }

  if (rc == TVA_OK)
  {
    subscription->PostMessageEvent(messageEvent);
  }
}

/*-----------------------------------------------------------------------------
 * Post async message received event to JavaScript
 */
void Subscription::MessageAsyncEvent(uv_async_t* async, int status)
{
  HandleScope scope;
  Subscription* subscription = (Subscription*)async->data;
  MessageEvent messageEvent;

  Local<Object> context = Context::GetCurrent()->Global();
  while (subscription->GetNextMessageEvent(messageEvent))
  {
    subscription->InvokeJsMessageEvent(context, messageEvent);
  }
}

/*-----------------------------------------------------------------------------
 * Post async message received event to JavaScript
 */
void Subscription::InvokeJsMessageEvent(Local<Object> context, MessageEvent& messageEvent)
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

  if ((_qos == TVA_QOS_GUARANTEED_DELIVERY) && (_ackMode == GdSubscriptionAckModeAuto))
  {
    tvagdMsgACK(messageEvent.tvaMessage);
  }
}


/*****     AckMessage     *****/

struct AckMessageRequest
{
  Subscription* subscription;
  TVA_MESSAGE* tvaMessage;
  TVA_STATUS result;
  Persistent<Function> complete;
};

/*-----------------------------------------------------------------------------
 * Acknowledge a received message when using "manual" ack mode with GD
 *
 * subscription.ackMessage(message, function (err) {
 *     // Message acknowledge complete
 * });
 */
Handle<Value> Subscription::AckMessage(const Arguments& args)
{
  HandleScope scope;
  Subscription* subscription = ObjectWrap::Unwrap<Subscription>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(2, args.Length());
  PARAM_REQ_OBJECT(0, args);        // message
  PARAM_REQ_FUNCTION(1, args);      // complete

  // Ready arguments
  Local<Object> message = Local<Object>::Cast(args[0]);
  Local<Function> complete = Local<Function>::Cast(args[1]);
  TVA_MESSAGE* tvaMessage = NULL;

  // Look for the reserved field in the message
  std::vector<std::string> fieldNames = cvv8::CastFromJS<std::vector<std::string> >(message->GetPropertyNames());
  for (size_t i = 0; i < fieldNames.size(); i++)
  {
    char* fieldName = (char*)(fieldNames[i].c_str());
    if (tva_str_casecmp(fieldName, "reserved") == 0)
    {
      Local<Value> fieldValue = message->Get(String::NewSymbol(fieldName));
      tvaMessage = (TVA_MESSAGE*)((intptr_t)fieldValue->NumberValue());
      break;
    }
  }

  if (tvaMessage == NULL)
  {
    ThrowException(Exception::TypeError(String::New("Invalid message")));
    return scope.Close(args.This());
  }

  // Send data to worker thread
  AckMessageRequest* request = new AckMessageRequest;
  request->subscription = subscription;
  request->tvaMessage = tvaMessage;
  request->complete = Persistent<Function>::New(complete);

  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, Subscription::AckWorker, Subscription::AckWorkerComplete);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
 * Perform GD message ACK
 */
void Subscription::AckWorker(uv_work_t* req)
{
  AckMessageRequest* request = (AckMessageRequest*)req->data;
  request->result = tvagdMsgACK(request->tvaMessage);
}

/*-----------------------------------------------------------------------------
 * GD message ACK complete
 */
void Subscription::AckWorkerComplete(uv_work_t* req)
{
  HandleScope scope;

  AckMessageRequest* request = (AckMessageRequest*)req->data;
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


/*****     DeleteSubscription     *****/

struct SubscriptionStopRequest
{
  Subscription* subscription;
  TVA_STATUS result;
  Persistent<Function> complete;
};

/*-----------------------------------------------------------------------------
 * Stop the subscription
 *
 * subscription.stop(function (err) {
 *     // Subscription stop complete
 * });
 */
Handle<Value> Subscription::Stop(const Arguments& args)
{
  HandleScope scope;
  Subscription* subscription = ObjectWrap::Unwrap<Subscription>(args.This());

  // Arguments checking
  PARAM_REQ_NUM(1, args.Length());
  PARAM_REQ_FUNCTION(0, args);        // complete

  // Ready arguments
  Local<Function> complete = Local<Function>::Cast(args[0]);

  // Send data to worker thread
  SubscriptionStopRequest* request = new SubscriptionStopRequest;
  request->subscription = subscription;
  request->complete = Persistent<Function>::New(complete);

  uv_work_t* req = new uv_work_t();
  req->data = request;

  uv_queue_work(uv_default_loop(), req, Subscription::StopWorker, Subscription::StopWorkerComplete);

  return scope.Close(args.This());
}

/*-----------------------------------------------------------------------------
 * Perform delete subscription
 */
void Subscription::StopWorker(uv_work_t* req)
{
  SubscriptionStopRequest* request = (SubscriptionStopRequest*)req->data;
  Subscription* subscription = request->subscription;
  TVA_STATUS rc;

  if (subscription->GetQos() == TVA_QOS_GUARANTEED_DELIVERY)
  {
    rc = tvagdSubTerm(subscription->GetHandle());
  }
  else
  {
    rc = tvaTerminateSubscription(subscription->GetHandle(), TVA_INVALID_HANDLE);
  }

  subscription->SetHandle(TVA_INVALID_HANDLE);

  request->result = rc;
}

/*-----------------------------------------------------------------------------
 * Delete subscription complete
 */
void Subscription::StopWorkerComplete(uv_work_t* req)
{
  HandleScope scope;

  SubscriptionStopRequest* request = (SubscriptionStopRequest*)req->data;
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

  uv_close((uv_handle_t*)request->subscription->GetAsyncObj(), Subscription::SubscriptionHandleCloseComplete);

  delete request;
}

void Subscription::SubscriptionHandleCloseComplete(uv_handle_t* handle)
{
}
