# Tervela-node

Tervela-node allows access to the Tervela Messaging Framework from within a Node.js application.

## Installing

### Node versions we have tested

The bulk of our testing has been with Node 0.8, but building with Node 0.6 also works, at least on Linux.

### Platforms we have tested

* CentOS 6.0 64 bit 
* Ubuntu 12.04 64 bit 
* Windows 7 64 bit with 32 bit node & 32 bit tva library (note: these must match)
* Windows 7 64 bit with 64 bit node & 64 bit tva library (note: these must match)

### Dependencies

* Tervela Client Library 5.0.0 or greater installed in the default location
* node 0.6 or >= 0.8
* node-gyp

### Note on Tervela Client Library Version
 
* Tervela API version 5.1.5 or higher is recommended.  Earlier versions do not support querying an existing
  publication for its QoS, which means that Tervela-node can not dynamically switch between using tvaSendMessage for
  BE/GC and tvagdMsgSend for GD.  Right now, if the library is older than 5.1.5, only GC publications will work.
  If you would prefer that only GD publications work, modify Session_Create.cpp (search for TVA_PUBINFO_QOS).

### How to Build

    git clone git://github.com/Tervela/tvanode.git
    cd tvanode
    node-gyp configure 
    node-gyp build
    
This will result in a Tervela node module in `build/Release/tervela.node`.  You can copy that to your node path, or reference the fully qualified path in your 'require' line.

### Notes

You may need to be root to run `node-gyp build`.  
On RHEL6 it seems you can't run `node-gyp configure build` as one invocation as you can on Ubuntu.  
On RHEL5, you need to run node-gyp with python 2.6 (`PYTHON=python26 node-gyp configure ...`), but there are bizarre compilation errors in the V8 headers.  


## How to Use

First, require `tervela`:

    var tervela = require('tervela');

Next, connect to the TMX:

    var connectOptions = {
        username: API-username,
        password: API-password,
        primaryTmx: primary-tmx,
        secondaryTmx: secondary-tmx,
        timeout: 30,
    };
    tervela.connect(connectOptions, function (err, session) {
        // Connection complete
        if (err) {
            // If `err` is set an error occurred
        }
        else {
            // Connection successful, session has been created
        }
    });

The `Session` object supports a number of events that the application can assign listeners to.

    session.
        .on('connection-lost', function () {
            // Temporarily disconnected from the fabric
            console.log("* Connection Lost");
        })
        .on('connection-restored', function () {
            // Reconnected to the fabric
            console.log("* Connection Restored");
        })
        .on('close', function () {
            // Completely disconneted from the fabric
            console.log("* Connection Closed");
        })
        .on('notify', function (code, msg) {
            // Misc. session notification event
            console.log("* Notification (%d): %s", code, msg);
        });

Once logged in, the `Session` object is used to create publications and subscriptions.

    session.createPublication(topic, function (err, publication) {
        if (err) {
            console.log("Error starting publication: " + err);
        }
        else {
            // Once a publication is started, messages can be sent
            var myMessage = {
                intField: 42,
                stringField: "Hello",
                dateField: new Date(),
                boolField: true
            };
            
            publication.sendMessage(topic, myMessage, {selfdescribe: true}, function (err) {
                if (err) {
                    console.log("Error sending message: " + err);
                }
            });
        }
    });
    
    // Unlike publications, subscriptions are created empty and must be started
    var subscription = session.createSubscription();
    subscription
        .start(topic)
        .on('start', function (err) {
            if (err) {
                console.log("Error starting subscription: " + err);
            }
        })
        .on('message', function (msg) {
            // Once a subscription is started messages can be received
            console.log("Message received!");
            console.log("- Topic: " + msg.topic);
            console.log("- GenerationTime: " + msg.generationTime);
            console.log("- ReceiveTime: " + msg.receiveTime);
            
            for (var field in msg.fields) {
                var fieldContents = msg.fields[field];
                console.log("--- " + field + " : " + fieldContents);
            }
        })
    });

When publications and subscriptions are no longer needed, they should be stopped:

    publication.stop(function (err) { 
        if (err) {
            console.log("Error stopping publication: " + err);
        }
    });
    
    subscription.stop(function (err) { 
        if (err) {
            console.log("Error stopping subscription: " + err);
        }
    });

When a session is no longer needed, it should be terminated by calling logout:

    session.logout(function (err) { 
        if (err != undefined) {
            console.log("Error in logout: " + err);
        }
    };

See the `test` directory for samples.

## Full API documentation

##### About the API

* The API is comprised of three objects, `Session`, `Publication`, and `Subscription`.  All applications will create a session, and from there, any number of publications and subscriptions.
* A couple of object methods take an optional {options} parameter, an object that gives additional details on how the method should be invoked.  Properties in the object specify different parameters, and the values of those properties are the values of the parameters.  {option} objects can be left out of the method call if none of the options are required.
* The last (or sometimes only) parameter to all methods is a function that is invoked when the method completes.  All of these functions contain at least an `err` parameter.  If `err` is `undefined`, no error occurred and the method completed successfully.  Otherwise `err` will be a string describing the error that occurred.

### Global functions

#### connect - Create a session and login to the Tervela fabric

    connect({
        username      : [API username],                         (string, required)
        password      : [API password],                         (string, required)
        primaryTmx    : [TMX name or address],                  (string, required)
        secondaryTmx  : [TMX name or address],                  (string, optional (default: [empty]))
        timeout       : [login timeout in seconds],             (integer, optional (default: 30))
        name          : [client name for GD operations],        (string, only required when using GD)
        gdMaxOut      : [GD publisher max outstanding]          (integer, only required when using GD (default: 1000))
    }, function (err, session) {
        // Session create complete
        // If `err` is set an error occurred and the session was not created successfully
    });

* `secondaryTmx` is used when logging into a TMX Fault-Tolerant pair
* A `timeout` of 0 means login will never timeout, and will internally retry until successful

#### connectSync - Create a session and login to the Tervela fabric (synchronous version)

    var session = tervela.connectSync({
        username      : [API username],                         (string, required)
        password      : [API password],                         (string, required)
        primaryTmx    : [TMX name or address],                  (string, required)
        secondaryTmx  : [TMX name or address],                  (string, optional (default: [empty]))
        timeout       : [login timeout in seconds],             (integer, optional (default: 30))
        name          : [client name for GD operations],        (string, only required when using GD)
        gdMaxOut      : [GD publisher max outstanding]          (integer, only required when using GD (default: 1000))
    });

* On success connectSync returns a `Session` object.  On failure connectSync returns a `string`, the text being the reason for failure.

### Session

#### Session.on - Assign event handler for session events

    session.on(event, listener);
    
    Events / Listeners:
      'connection-lost'      - Connection lost (will auto-reconnect)   - function () { }
      'connection-restored'  - Reconnected after connection lost       - function () { }
      'close'                - Session closed                          - function () { }
      'connect-info'         - Initial connection info                 - function (activeTmx, standbyTmx) { }
      'gds-lost'             - GDS communications lost                 - function () { }
      'gds-restored'         - GDS communications restored             - function () { }
      'notify'               - Misc. session notification event        - function (code, msg) { }

#### Session.createPublication - Create a new publication object, get ready to send messages

    session.createPublication(topic, function (err, publication) {
        // Create publication complete
        // If `err` is set an error occurred and the publication was not created successfully
    });

* `topic` can be either a discrete or wildcard topic

#### Session.createPublicationSync - Create a new publication object, get ready to send messages (synchronous version)

    var publication = session.createPublicationSync(topic);

* `topic` can be either a discrete or wildcard topic
* On success createPublicationSync returns a `Publication` object.  On failure createPublicationSync returns a `string`, the text being the reason for failure.

#### Session.createSubscription - Create a new subscription object, get ready to receive messages

    session.createSubscription(topic, {
        qos           : [Quality of service: 'BE'|'GC'|'GD'],   (string, optional (default: 'GC'))
        name          : [Subscription name],                    (string, only required when using GD)
        ackMode       : [message ACK mode: 'auto'|'manual']     (string, only required when using GD (default: 'auto'))
    }, function (err, subscription) {
        // Create subscription complete
        // If `err` is set an error occurred and the subscription was not created successfully
    });

* `topic` can be either a discrete or wildcard topic
* With `ackMode` set to `auto` messagse will be acknowledged once the message event listener completes.  With `ackMode` set to `manual` the application must call `subscription.ackMessage` for every message received.

#### Session.createSubscriptionSync - Create a new subscription object, get ready to receive messages (synchronous version)

    var subscription = session.createSubscriptionSync(topic, {
        qos           : [Quality of service: 'BE'|'GC'|'GD'],   (string, optional (default: 'GC'))
        name          : [Subscription name],                    (string, only required when using GD)
        ackMode       : [message ACK mode: 'auto'|'manual']     (string, only required when using GD (default: 'auto'))
    });

* `topic` can be either a discrete or wildcard topic
* With `ackMode` set to `auto` messagse will be acknowledged once the message event listener completes.  With `ackMode` set to `manual` the application must call `subscription.ackMessage` for every message received.  See `Subscription.ackMessage` for more information.
* On success createSubscriptionSync returns a `Subscription` object.  On failure createSubscriptionSync returns a `string`, the text being the reason for failure.

#### Session.createReplay - Create a new replay object, get ready to receive messages

    session.createReplay(topic {
        startTime     : [Replay start time]                     (Date, required)
        endTime       : [Replay end time]                       (Date, required)
    }, function (err, replay) {
        // Create replay complete
        // If `err` is set an error occurred and the replay was not created successfully
    });

* `topic` can be either a discrete or wildcard topic
* `startTime` and `endTime` must be in UTC

#### Session.createReplaySync - Create a new replay object, get ready to receive messages (synchronous version)

    var replay = session.createReplaySync(topic {
        startTime     : [Replay start time]                     (Date, required)
        endTime       : [Replay end time]                       (Date, required)
    });

* `topic` can be either a discrete or wildcard topic
* `startTime` and `endTime` must be in UTC
* On success createReplaySync returns a `Replay` object.  On failure createReplaySync returns a `string`, the text being the reason for failure.

#### Session.close - Logout and disconnect from the Tervela fabric

    session.close(function (err) {
        // Session logout complete
        // If `err` is set an error occurred
    });

### Publication

#### Publication.sendMessage - Send a message

    pub.sendMessage(topic, message, {options}, function (err) {
        // Send message complete
        // If `err` is set an error occurred
    });
    
    // None of the members of the options object are required
    options = {
        selfdescribe  : [ignore topic schema],                  (boolean, default: false)
    };

* The `topic` used when sending a message must match the `topic` used to create the publication, either directly or within a wildcard subset.  
For example, if the publication topic is a discrete topic such as A.1, the message must be sent on topic A.1.  However if the publication topic is a wildcard topic such as A.* then the message can be sent on any topic that falls under A.* (A.A, A.1, A.2, etc.).
* The `message` is a JavaScript object.  The property names become the field names in the Tervela message, and the property values become those field values.
* `selfdescribe` makes the message "self-describing" or not.  When `selfdescribe` is set to `false` (the default), property names must match schema field names; when `selfdescribe` is set to `true`, the schema field names are ignored and the property names become the field names.

#### Publication.stop - Stop the publication

    pub.stop(function (err), {
        // Publication stop complete
        // If `err` is set an error occurred
    });

### Subscription

#### Subscription.on - Assign event handlers for subscription events

    sub.on(event, listener);
    
    Events / Listeners:
      'message'              - Message received                        - function (message) { }

* The `message` listener is invoked for every message received.  The passed message is a JavaScript object.

##### Message format

    message = {
        topic,                 (string : message topic)
        generationTime,        (Date : when the message was sent by the publisher)
        receiveTime,           (Date : when the message was received)
        fields                 (Object : message fields list ([name]=value))
    }

#### Subscription.ackMessage - Acknowledge a received message when using "manual" ack mode with GD

    sub.ackMessage(message, function (err) {
        // Acknowledge complete
        // If `err` is set an error occurred
    };

* Messages received on a GD subscription must be acknowledged.  This informs the system the message has been consumed.  If `ackMode` on the subscription is set to "auto" the acknowledgement happens automatically after the `message` event listener returns.  If `ackMode` is set to "manual", however, the application is responsible for acknowledging the message.  The `message` object passed to the `ackMessage` method is the same `message` object that was given to the application in the `message` event listener.

#### Subscription.stop - Stop the subscription

    sub.stop(function (err), {
        // Subscription stop complete
        // If `err` is set an error occurred
    });

### Replay

#### Replay.on - Assign event handlers for replay events

    replay.on(event, listener);
    
    Events / Listeners:
      'message'              - Message received                        - function (message) { }
      'finish'               - Replay finished, all messages received  - function () { }
      'error'                - Replay error occurred                   - function (err) { }

* The `message` listener is invoked for every message received.  The passed message is a JavaScript object.
* The `finish` listener is invoked when the replay completes, meaning no additional messages will be received.  This listener is invoked after the `message` listener for that last message.
* The `error` listener is invoked for replay errors, such as when the data could not be found on the TPE.  No messages will be received when these errors occur.
* When the `finish` or `error` listeners are invoked the internal reference to the Replay object is released.  If the application does not have any references to the object it will then be freed and eligible for garbage collection.

##### Message format (same as for Subscriptions)

    message = {
        topic,                 (string : message topic)
        generationTime,        (Date : when the message was sent by the publisher)
        receiveTime,           (Date : when the message was received)
        fields                 (Object : message fields list ([name]=value))
    }

#### Replay.pause - Pause an active replay

    replay.pause(function (err) {
        // Replay pause complete
        // If `err` is set an error occurred
    });

#### Replay.resume - Resume a paused replay

    replay.resume(function (err) {
        // Replay resume complete
        // If `err` is set an error occurred
    });

#### Replay.stop - Stop an active or paused replay

    replay.stop(function (err) {
        // Replay stop complete
        // If `err` is set an error occurred
    });

* `replay.stop` is only required if the application wishes to stop a replay that has not completed.  When a replay completes (either with `finish` or `error`) internal resources are freed when the object is garbage collected.
