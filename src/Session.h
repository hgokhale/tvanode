#pragma once

#include <queue>
#include <vector>
#include <v8.h>
#include <node.h>
#include "DataTypes.h"
#include "tvaClientAPI.h"
#include "tvaClientAPIInterface.h"
#include "tvaGDAPI.h"
#include "compat.h"

class Publication;

struct GdAckWindowEntry
{
  Publication* publisher;
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

class Session: node::ObjectWrap
{
public:
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
   * Create a new subscription
   *
   * var sub = session.createSubscription();
   */
  static v8::Handle<v8::Value> CreateSubscription(const v8::Arguments& args);


  /* Internal methods */
  Session();
  ~Session();

  TVA_STATUS SendGdMessage(Publication* publisher, TVA_PUBLISH_MESSAGE_DATA_HANDLE messageData, v8::Persistent<v8::Function> complete);
  TVA_STATUS Terminate();

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

  void SetEventHandler(char* evt, v8::Local<v8::Function> handler);

  inline void PostSessionEvent(SessionNotificaton& notificationEvent)
  {
    uv_mutex_lock(&_sessionEventLock);
    _sessionEventQueue.push(notificationEvent);
    uv_mutex_unlock(&_sessionEventLock);
    uv_async_send(GetAsyncObj());
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

private:
  static void ConnectWorker(uv_work_t* req);
  static void ConnectWorkerComplete(uv_work_t* req);
  static void CloseWorker(uv_work_t* req);
  static void CloseWorkerComplete(uv_work_t* req);
  static void CreatePublicationWorker(uv_work_t* req);
  static void CreatePublicationWorkerComplete(uv_work_t* req);
  static void SessionHandleCloseComplete(uv_handle_t* handle);
  void InvokeJsSessionNotification(v8::Local<v8::Object> context, SessionNotificaton& notificationEvent);

  static v8::Persistent<v8::Function> constructor;

  TVA_SESSION_HANDLE _handle;
  TVAGD_CONTEXT_HANDLE _gdHandle;
  uv_async_t _async;
  std::queue<SessionNotificaton> _sessionEventQueue;
  uv_mutex_t _sessionEventLock;

  std::vector< v8::Persistent<v8::Function> > _disconnectHandler;
  std::vector< v8::Persistent<v8::Function> > _reconnectHandler;
  std::vector< v8::Persistent<v8::Function> > _terminateHandler;
  std::vector< v8::Persistent<v8::Function> > _connInfoHandler;
  std::vector< v8::Persistent<v8::Function> > _gdsLostHandler;
  std::vector< v8::Persistent<v8::Function> > _gdsRestoreHandler;
  std::vector< v8::Persistent<v8::Function> > _defaultHandler;
  
  uv_mutex_t _gdSendLock;
  GdAckWindowEntry* _gdAckWindow;
  int _gdAckWindowSize;
  int _gdAckWindowIdx;
};
