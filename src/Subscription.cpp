/**
 * Copyright (c) 2012 Tervela.  All rights reserved.
 */

#include <stdlib.h>
#include <string>
#include "v8-convert.hpp"
#include "DataTypes.h"
#include "Helpers.h"
#include "Session.h"
#include "Subscription.h"

using namespace v8;

enum SubscriptionEvent
{
  EVT_MESSAGE = 0,
  EVT_ACK,
  EVT_STOP
};

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

  t->PrototypeTemplate()->Set(String::NewSymbol("on"), FunctionTemplate::New(On)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("acknowledge"), FunctionTemplate::New(AckMessage)->GetFunction());
  t->PrototypeTemplate()->Set(String::NewSymbol("stop"), FunctionTemplate::New(Stop)->GetFunction());

  constructor = Persistent<Function>::New(t->GetFunction());
}

/*-----------------------------------------------------------------------------
 * Construct a new Subscription object
 */
Handle<Value> Subscription::New(const Arguments& args)
{
  HandleScope scope;
  Subscription* sub = (Subscription*)External::Unwrap(args[0]->ToObject());
  sub->Wrap(args.This());
  return args.This();
}

Handle<Value> Subscription::NewInstance(Subscription* subscription)
{
  HandleScope scope;
  Handle<External> wrapper = External::New(subscription);
  Handle<Value> argv[1] = { wrapper };
  Local<Object> instance = constructor->NewInstance(1, argv);

  instance->Set(String::NewSymbol("topic"), String::New(subscription->GetTopic()), ReadOnly);
  switch (subscription->GetQos())
  {
  case TVA_QOS_BEST_EFFORT:
    instance->Set(String::NewSymbol("qos"), String::New("BE"), ReadOnly);
    break;

  case TVA_QOS_GUARANTEED_CONNECTED:
    instance->Set(String::NewSymbol("qos"), String::New("GC"), ReadOnly);
    break;

  case TVA_QOS_GUARANTEED_DELIVERY:
    instance->Set(String::NewSymbol("qos"), String::New("GD"), ReadOnly);
    break;
  }

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
  _topic = NULL;
  _isInUse = false;
  uv_mutex_init(&_messageEventLock);

  EventEmitterConfiguration events[] = 
  {
    { EVT_MESSAGE,  "message" },
    { EVT_ACK,      "ack"     },
    { EVT_STOP,     "stop"    }
  };
  SetValidEvents(3, events);
}

Subscription::~Subscription()
{
  if (_topic)
  {
    free(_topic);
  }

  uv_mutex_destroy(&_messageEventLock);
}

/*-----------------------------------------------------------------------------
 * Perform create subscription
 */
TVA_STATUS Subscription::Start(char* topic, uint8_t qos, char* name, GdSubscriptionAckMode gdAckMode)
{
  TVA_STATUS rc;
  TVA_HANDLE subHandle = TVA_INVALID_HANDLE;
  
  if (qos == TVA_QOS_GUARANTEED_DELIVERY)
  {
    rc = tvagdSubCbNew(GetSession()->GetGdHandle(), name, topic,
                       Subscription::MessageReceivedEvent, this, &subHandle);
  }
  else
  {
    int cachePeriod = 3000;
    if (qos == TVA_QOS_BEST_EFFORT)
    {
      cachePeriod = 50;
    }

    rc = tvaSubscribeWithCallbackEx(topic, Subscription::MessageReceivedEvent, this,
                                    GetSession()->GetHandle(), qos, true, cachePeriod, &subHandle);
  }

  if (rc == TVA_OK)
  {
    _topic = strdup(topic);
    _qos = qos;
    _ackMode = gdAckMode;
    _handle = subHandle;
    _session->AddSubscription(this);
  }

  return rc;
}


/*****     On     *****/

/*-----------------------------------------------------------------------------
 * Register for subscription events
 *
 * session.on(event, listener);
 *
 * Events / Listeners:
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

  if (subscription->AddListener(*evt, Persistent<Function>::New(handler)) == false)
  {
    THROW_INVALID_EVENT_LISTENER("subscription", *evt);
  }

  return scope.Close(args.This());
}


/*****     MessageEvent     *****/

/*-----------------------------------------------------------------------------
 * Subscription message received callback
 */
void Subscription::MessageReceivedEvent(TVA_MESSAGE* message, void* context)
{
  Subscription* subscription = (Subscription*)context;
  MessageEvent messageEvent;

  TVA_STATUS rc = Subscription::ProcessRecievedMessage(message, messageEvent);
  if (rc == TVA_OK)
  {
    if (!subscription->PostMessageEvent(messageEvent))
    {
      // Post failed, need to release the message
      tvaReleaseMessageData(message);
    }
  }
}

/*-----------------------------------------------------------------------------
 * Process the received message (shared with Replay class)
 */
TVA_STATUS Subscription::ProcessRecievedMessage(TVA_MESSAGE* message, MessageEvent& messageEvent)
{
  TVA_STATUS rc;

  messageEvent.tvaMessage = message;
  messageEvent.jmsMessageType = 0;

  TVA_MESSAGE_DATA_HANDLE msgData = message->messageData;
  TVA_FIELD_ITERATOR_HANDLE fieldItr = NULL;
  TVA_MSG_FIELD_INFO fieldInfo;

  if (TVA_MSG_ISLAST(message))
  {
    messageEvent.isLastMessage = true;
  }

#ifdef TVA_MSG_ISFROMJMS
  messageEvent.jmsMessageType = TVA_JMS_MSG_TYPE_MAP;
  if (TVA_MSG_ISFROMJMS(message))
  {
    TVA_MSG_JMS_HDR jmsHeader;
    rc = tvaMsgInfoGet(message, TVA_MSGINFO_JMS_HDR, &jmsHeader, sizeof(jmsHeader));
    if (rc == TVA_OK)
    {
      if (jmsHeader.jmsMessageType == TVA_JMS_MSG_TYPE_TEXT)
      {
        messageEvent.jmsMessageType = TVA_JMS_MSG_TYPE_TEXT;
      }
    }
  }
#endif

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
            field.value.boolValue = (val != 0);
          }
        }
        break;

      case FIELD_TYPE_BYTE:
        {
          TVA_UINT8 val;
          rc = tvaGetByteFromMessageByFieldId(msgData, fieldInfo.fieldId, &val);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeInt32;
            field.value.int32Value = (int)val;
          }
        }
        break;

      case FIELD_TYPE_SHORT:
        {
          TVA_INT16 val;
          rc = tvaGetShortFromMessageByFieldId(msgData, fieldInfo.fieldId, &val);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeInt32;
            field.value.int32Value = (int)val;
          }
        }
        break;

      case FIELD_TYPE_INTEGER:
        {
          TVA_INT32 val;
          rc = tvaGetIntFromMessageByFieldId(msgData, fieldInfo.fieldId, &val);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeInt32;
            field.value.int32Value = (int)val;
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

      case FIELD_TYPE_BYTEARRAY:
        {
#ifdef TVA_MSG_ISFROMJMS
          if (messageEvent.jmsMessageType == TVA_JMS_MSG_TYPE_TEXT)
          {
            TVA_UINT8* val;
            TVA_UINT32 len;
            rc = tvaGetBytesFromMessageByFieldId(msgData, fieldInfo.fieldId, &val, &len);
            if (rc == TVA_OK)
            {
              field.type = MessageFieldDataTypeString;
              field.value.stringValue = (TVA_STRING)val;
            }
          }
#endif
        }
        break;

      case FIELD_TYPE_BOOLEAN_ARRAY:
        {
          TVA_BOOLEAN* val;
          TVA_UINT32 count;
          rc = tvaGetBooleanArrayFromMessageByFieldId(msgData, fieldInfo.fieldId, &val, &count);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeBooleanArray;
            field.count = count;
            field.value.arrayValue = val;
          }
        }
        break;

      case FIELD_TYPE_SHORT_ARRAY:
        {
          TVA_INT16* val;
          TVA_UINT32 count;
          rc = tvaGetShortArrayFromMessageByFieldId(msgData, fieldInfo.fieldId, &val, &count);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeInt16Array;
            field.count = count;
            field.value.arrayValue = val;
          }
        }
        break;

      case FIELD_TYPE_INTEGER_ARRAY:
        {
          TVA_INT32* val;
          TVA_UINT32 count;
          rc = tvaGetIntArrayFromMessageByFieldId(msgData, fieldInfo.fieldId, &val, &count);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeInt32Array;
            field.count = count;
            field.value.arrayValue = val;
          }
        }
        break;

      case FIELD_TYPE_LONG_ARRAY:
        {
          TVA_INT64* val;
          TVA_UINT32 count;
          rc = tvaGetLongArrayFromMessageByFieldId(msgData, fieldInfo.fieldId, &val, &count);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeInt64Array;
            field.count = count;
            field.value.arrayValue = val;
          }
        }
        break;

      case FIELD_TYPE_FLOAT_ARRAY:
        {
          TVA_FLOAT* val;
          TVA_UINT32 count;
          rc = tvaGetFloatArrayFromMessageByFieldId(msgData, fieldInfo.fieldId, &val, &count);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeFloatArray;
            field.count = count;
            field.value.arrayValue = val;
          }
        }
        break;

      case FIELD_TYPE_DOUBLE_ARRAY:
        {
          TVA_DOUBLE* val;
          TVA_UINT32 count;
          rc = tvaGetDoubleArrayFromMessageByFieldId(msgData, fieldInfo.fieldId, &val, &count);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeDoubleArray;
            field.count = count;
            field.value.arrayValue = val;
          }
        }
        break;

      case FIELD_TYPE_DATETIME_ARRAY:
        {
          TVA_DATE* val;
          TVA_UINT32 count;
          rc = tvaGetDateTimeArrayFromMessageByFieldId(msgData, fieldInfo.fieldId, &val, &count);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeDateArray;
            field.count = count;
            field.value.arrayValue = val;
          }
        }
        break;

      case FIELD_TYPE_STRING_ARRAY:
        {
          TVA_STRING* val;
          TVA_UINT32 count;
          rc = tvaGetStringArrayFromMessageByFieldId(msgData, fieldInfo.fieldId, &val, &count);
          if (rc == TVA_OK)
          {
            field.type = MessageFieldDataTypeStringArray;
            field.count = count;
            field.value.arrayValue = val;
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

  if (fieldItr)
  {
    tvaReleaseMessageFieldIterator(fieldItr);
  }

  return rc;
}

/*-----------------------------------------------------------------------------
 * Create an object to be sent to JavaScript
 */
Local<Object> Subscription::CreateJsMessageObject(MessageEvent& messageEvent)
{
  Local<Object> fields = Object::New();
  while (!messageEvent.fieldData.empty())
  {
    MessageFieldData field = messageEvent.fieldData.front();
    messageEvent.fieldData.pop_front();

    switch (field.type)
    {
    case MessageFieldDataTypeBoolean:
      fields->Set(String::NewSymbol(field.name), Boolean::New(field.value.boolValue));
      break;

    case MessageFieldDataTypeInt32:
      fields->Set(String::NewSymbol(field.name), Int32::New(field.value.int32Value));
      break;

    case MessageFieldDataTypeNumber:
      fields->Set(String::NewSymbol(field.name), Number::New(field.value.numberValue));
      break;

    case MessageFieldDataTypeDate:
      fields->Set(String::NewSymbol(field.name), Date::New((double)(field.value.dateValue.timeInMicroSecs / 1000)));
      break;

    case MessageFieldDataTypeString:
      fields->Set(String::NewSymbol(field.name), String::New(field.value.stringValue));
      tvaReleaseFieldValue(field.value.stringValue);
      break;

    case MessageFieldDataTypeBooleanArray:
      {
        TVA_BOOLEAN* arrayData = (TVA_BOOLEAN*)field.value.arrayValue;
        Local<Array> fieldData = Array::New();
        for (int i = 0; i < field.count; i++)
        {
          fieldData->Set(i, Boolean::New((arrayData[i] != 0)));
        }

        fields->Set(String::NewSymbol(field.name), fieldData);
        tvaReleaseFieldValue(arrayData);
      }
      break;

    case MessageFieldDataTypeInt16Array:
      {
        TVA_INT16* arrayData = (TVA_INT16*)field.value.arrayValue;
        Local<Array> fieldData = Array::New();
        for (int i = 0; i < field.count; i++)
        {
          fieldData->Set(i, Int32::New((int)arrayData[i]));
        }

        fields->Set(String::NewSymbol(field.name), fieldData);
        tvaReleaseFieldValue(arrayData);
      }
      break;

    case MessageFieldDataTypeInt32Array:
      {
        TVA_INT32* arrayData = (TVA_INT32*)field.value.arrayValue;
        Local<Array> fieldData = Array::New();
        for (int i = 0; i < field.count; i++)
        {
          fieldData->Set(i, Int32::New(arrayData[i]));
        }

        fields->Set(String::NewSymbol(field.name), fieldData);
        tvaReleaseFieldValue(arrayData);
      }
      break;

    case MessageFieldDataTypeInt64Array:
      {
        TVA_INT64* arrayData = (TVA_INT64*)field.value.arrayValue;
        Local<Array> fieldData = Array::New();
        for (int i = 0; i < field.count; i++)
        {
          fieldData->Set(i, Number::New((double)arrayData[i]));
        }

        fields->Set(String::NewSymbol(field.name), fieldData);
        tvaReleaseFieldValue(arrayData);
      }
      break;

    case MessageFieldDataTypeFloatArray:
      {
        TVA_FLOAT* arrayData = (TVA_FLOAT*)field.value.arrayValue;
        Local<Array> fieldData = Array::New();
        for (int i = 0; i < field.count; i++)
        {
          fieldData->Set(i, Number::New((double)arrayData[i]));
        }

        fields->Set(String::NewSymbol(field.name), fieldData);
        tvaReleaseFieldValue(arrayData);
      }
      break;

    case MessageFieldDataTypeDoubleArray:
      {
        TVA_DOUBLE* arrayData = (TVA_DOUBLE*)field.value.arrayValue;
        Local<Array> fieldData = Array::New();
        for (int i = 0; i < field.count; i++)
        {
          fieldData->Set(i, Number::New((double)arrayData[i]));
        }

        fields->Set(String::NewSymbol(field.name), fieldData);
        tvaReleaseFieldValue(arrayData);
      }
      break;

    case MessageFieldDataTypeDateArray:
      {
        TVA_DATE* arrayData = (TVA_DATE*)field.value.arrayValue;
        Local<Array> fieldData = Array::New();
        for (int i = 0; i < field.count; i++)
        {
          fieldData->Set(i, Date::New((double)(arrayData[i].timeInMicroSecs / 1000)));
        }

        fields->Set(String::NewSymbol(field.name), fieldData);
        tvaReleaseFieldValue(arrayData);
      }
      break;

    case MessageFieldDataTypeStringArray:
      {
        TVA_STRING* arrayData = (TVA_STRING*)field.value.arrayValue;
        Local<Array> fieldData = Array::New();
        for (int i = 0; i < field.count; i++)
        {
          fieldData->Set(i, String::New(arrayData[i]));
          tvaReleaseFieldValue(arrayData[i]);
        }

        fields->Set(String::NewSymbol(field.name), fieldData);
        tvaReleaseFieldValue(arrayData);
      }
      break;

    default:
      break;
    }
  }

  Local<Object> message = Object::New();
  message->Set(String::NewSymbol("topic"), String::New(messageEvent.tvaMessage->topicName), ReadOnly);
  message->Set(String::NewSymbol("generationTime"), Date::New((double)(messageEvent.tvaMessage->msgGenerationTime / 1000)), ReadOnly);
  message->Set(String::NewSymbol("receiveTime"), Date::New((double)(messageEvent.tvaMessage->msgReceiveTime / 1000)), ReadOnly);
  message->Set(String::NewSymbol("lossGap"), Int32::New(messageEvent.tvaMessage->topicSeqGap), ReadOnly);
  message->Set(String::NewSymbol("fields"), fields, ReadOnly);
  message->Set(String::NewSymbol("reserved"), Number::New((double)((intptr_t)(messageEvent.tvaMessage))), ReadOnly);

#ifdef TVA_MSG_ISFROMJMS
  if (messageEvent.jmsMessageType == TVA_JMS_MSG_TYPE_TEXT)
  {
    message->Set(String::NewSymbol("messageType"), String::New("text"), ReadOnly);
  }
  else
  {
    message->Set(String::NewSymbol("messageType"), String::New("map"), ReadOnly);
  }
#else
  message->Set(String::NewSymbol("messageType"), String::New("map"), ReadOnly);
#endif

  return message;
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
  Local<Object> message = Subscription::CreateJsMessageObject(messageEvent);
  Handle<Value> argv[] = { message };
  
  TryCatch tryCatch;

  Emit(EVT_MESSAGE, 1, argv);
  if (tryCatch.HasCaught())
  {
    node::FatalException(tryCatch);
  }

  if (_qos != TVA_QOS_GUARANTEED_DELIVERY)
  {
    // Non-GD messages must be released.
    tvaReleaseMessageData(messageEvent.tvaMessage);
  }
  else 
  {
    // Is a GD message.  If ACK mode is "auto" acknowledge it now.
    if (_ackMode == GdSubscriptionAckModeAuto)
    {
      tvagdMsgACK(messageEvent.tvaMessage);
    }
  }
}


/*****     AckMessage     *****/

struct AckMessageRequest
{
  Subscription* subscription;
  TVA_MESSAGE* tvaMessage;
  TVA_STATUS result;
  Persistent<Object> origMessage;
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
  PARAM_REQ_NUM(1, args.Length());
  PARAM_REQ_OBJECT(0, args);        // message

  // Ready arguments
  Local<Object> message = Local<Object>::Cast(args[0]);
  Local<Function> complete;
  TVA_MESSAGE* tvaMessage = NULL;

  if (args.Length() > 1)
  {
    complete = Local<Function>::Cast(args[1]);
  }

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
  request->origMessage = Persistent<Object>::New(message);

  if (!complete.IsEmpty())
  {
    request->complete = Persistent<Function>::New(complete);
  }

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

  Handle<Value> argv[2];
  if (request->result == TVA_OK)
  {
    argv[0] = Undefined();
  }
  else
  {
    argv[0] = String::New(tvaErrToStr(request->result));
  }
  argv[1] = request->origMessage;

  TryCatch tryCatch;

  if (!request->complete.IsEmpty())
  {
    request->complete->Call(Context::GetCurrent()->Global(), 2, argv);
    request->complete.Dispose();
  }

  request->subscription->Emit(EVT_ACK, 2, argv);

  if (tryCatch.HasCaught())
  {
    node::FatalException(tryCatch);
  }

  request->origMessage.Dispose();

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

  if (args.Length() > 0)
  {
    // Read argument (complete callback)
    Local<Function> complete = Local<Function>::Cast(args[0]);
    subscription->AddOnceListener(EVT_STOP, Persistent<Function>::New(complete));
  }

  // Send data to worker thread
  SubscriptionStopRequest* request = new SubscriptionStopRequest;
  request->subscription = subscription;

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
  request->result = request->subscription->Stop(false);
}

TVA_STATUS Subscription::Stop(bool sessionClosing)
{
  TVA_STATUS rc;

  if (_qos == TVA_QOS_GUARANTEED_DELIVERY)
  {
    rc = tvagdSubTerm(_handle);
  }
  else
  {
    rc = tvaTerminateSubscription(_handle, TVA_INVALID_HANDLE);
  }

  if (!sessionClosing)
  {
    _session->RemoveSubscription(this);
  }

  if (_topic)
  {
    free(_topic);
    _topic = NULL;
  }

  _handle = TVA_INVALID_HANDLE;
  return rc;
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

  request->subscription->Emit(EVT_STOP, 1, argv);

  if (tryCatch.HasCaught())
  {
    node::FatalException(tryCatch);
  }

  request->subscription->MarkInUse(false);

  delete request;
}

void Subscription::SubscriptionHandleCloseComplete(uv_handle_t* handle)
{
}
