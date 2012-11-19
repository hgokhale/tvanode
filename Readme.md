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
        tmx: [ primary-tmx, secondary-tmx ],
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

    session
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
    
    // Subscriptions are created similarly
    session.createSubscription(topic, function (err, subscription) {
        if (err) {
            console.log("Error starting subscription: " + err);
        }
        else {
            subscription.on('message', function (msg) {
                // Once a subscription is started messages can be received
                console.log("Message received!");
                console.log("- Topic: " + msg.topic);
                console.log("- GenerationTime: " + msg.generationTime);
                console.log("- ReceiveTime: " + msg.receiveTime);
                
                for (var field in msg.fields) {
                    var fieldContents = msg.fields[field];
                    console.log("--- " + field + " : " + fieldContents);
                }
            });
        }
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

When a session is no longer needed, it should be terminated by calling close:

    session.close(function (err) { 
        if (err) {
            console.log("Error in close: " + err);
        }
    };

See the `test` directory for samples.

# Full API documentation

#### About the API

* The API is comprised of the objects `Session`, `Publication`, `Subscription`, `Replay`, and `Logger`.  All applications will create a session, and from there, any number of publications, subscriptions, and/or replays.
* A couple of object methods take an optional {options} parameter, an object that gives additional details on how the method should be invoked.  Properties in the object specify different parameters, and the values of those properties are the values of the parameters.  {option} objects can be left out of the method call if none of the options are required.

## Global functions

### tervela.connect(options, callback)

Creates a session and logs in to the Tervela fabric.

`options` is an object with the following details:

    {
        username      : [API username],                         (String, required)
        password      : [API password],                         (String, required)
        tmx           : [TMX name(s) or address(es)],           (String or array of strings, required)
        timeout       : [login timeout in seconds],             (integer, optional (default: 30))
        config        : [configuration parmaters],              (Object, optional (see below))
        name          : [client name for GD operations],        (String, only required when using GD)
        gdMaxOut      : [GD publisher max outstanding]          (integer, only required when using GD (default: 1000))
    }
	
`tmx` can be either a string or an array of strings.  If an array of strings is specified, the first element will be used as the primary TMX and the second element will be used as the secondary TMX.

A `timeout` of `0` means login will never timeout, and will internally retry until successful.

`callback` is a function that is called when `connect` completes:

    function (err, session) {
        // Session create complete
        // If `err` is set an error occurred and the session was not created successfully
    }

### tervela.connectSync(options)

Creates a session and logs in to the Tervela fabric (synchronous version).
On success connectSync returns a `Session` object.  On failure connectSync returns a `string`, the text being the reason for failure.

`options` is an object with the following details:

    {
        username      : [API username],                         (String, required)
        password      : [API password],                         (String, required)
        tmx           : [TMX name(s) or address(es)],           (String or array of strings, required)
        timeout       : [login timeout in seconds],             (integer, optional (default: 30))
        config        : [configuration parmaters],              (Object, optional (see below))
        name          : [client name for GD operations],        (String, only required when using GD)
        gdMaxOut      : [GD publisher max outstanding]          (integer, only required when using GD (default: 1000))
    });

`tmx` can be either a string or an array of strings.  If an array of strings is specified, the first element will be used as the primary TMX and the second element will be used as the secondary TMX.

A `timeout` of `0` means login will never timeout, and will internally retry until successful.

#### Connect Configuration Object

    config = {        // All parameters are optional
        pubRate               : [rate at which messages will be sent, in Kmps]                                          (integer)
        pubBandwidthLimit     : [the maximum bandwidth, in Mbps]                                                        (integer)
        subRate               : [rate at which messages will be received, in Kmps]                                      (integer)
        dataTransportType     : [underlying transport type: 'UDP' | 'TCP' | 'SSL']                                      (String)
        subAudit              : [turn on subscription auditing]                                                         (boolean)
        pubAudit              : [turn on publication auditing]                                                          (boolean)
        configFilename        : [name of Terela configuration file]                                                     (String)
        maxReconnectCount     : [maximum number of reconnect tries before giving up]                                    (integer)
        maxPublications       : [maximum number of publications the application is allowed to create]                   (integer)
        maxSubscriptions      : [maximum number of subscriptions the application is allowed to create]                  (integer)
        allowTerminationName  : [API user name of remote application that is allowed to terminate this application]     (String)
        logFilename           : [name of Tervela log file]                                                              (String)
        logTag                : [tag to use in Tervela log file]                                                        (String)
        favorTmxOrder         : [favor TMX[0] over TMX[1] when deciding active TMX]                                     (boolean)
        gcChannelOnly         : [only create the GC data channel with the TMX]                                          (boolean)
    };

## tervela.getLogger([filename], [tagname])

Get a reference to the logger object.

This function returns the instance of the Tervela API logger object, allowing the application to log messages to the same location as the Tervela API (`/var/log/messages` in Linux, `tervela.log` in Windows).  So if the application changes the `filename` or `tagname` options the settings will also be used by the Tervela API.

Once the log has been created `filename` and `tagname` cannot be changed.

When the Tervela API is initialized (during `connect` or `connectSync`) the log will be created if it has not been created yet.  So calls to `getLogger` after `connect` or `connectSync` cannot change the `filename` and `tagname`.

Only one instance of the logger is created.  Subsequent calls to `getLogger` return a reference to the originally created logger object.

`tagname` is used to change the tag for message in the log.  This lets multiple applications log to the same location and be distinguishable.

## Class: tervela.Session

This class represents a connection with the TMX.
    
### session.createPublication(topic, callback)

Create a new publication object, get ready to send messages.

`topic` can be either a discrete or wildcard topic.

`callback` is a function with the following prototype:

    function (err, publication) {
        // If 'err' is set an error occurred and the publication was not created successfully
		// Otherwise 'publication' is the newly created Publication object
    }

### session.createPublicationSync(topic)

Create a new publication object, get ready to send messages (synchronous version).

`topic` can be either a discrete or wildcard topic

On success `createPublicationSync` returns a `Publication` object.  On failure createPublicationSync returns a `String` object, the text being the reason for failure.

### session.createSubscription(topic, [options], callback)

Create a new subscription object, get ready to receive messages.

`topic` can be either a discrete or wildcard topic.

`options` is an object with the following details:

    {
        qos           : [Quality of service: 'BE'|'GC'|'GD'],   (String, optional (default: 'GC'))
        name          : [Subscription name],                    (String, only required when using GD)
        ackMode       : [message ACK mode: 'auto'|'manual']     (String, only required when using GD (default: 'auto'))
    }

`callback` is a function with the following prototype:

    function (err, subscription) {
        // If 'err' is set an error occurred and the subscription was not created successfully
        // Otherwise 'subscription' is the newly created Subscription object
    });

With `ackMode` set to `auto` messagse will be acknowledged once the message event listener completes.  With `ackMode` set to `manual` the application must call `subscription.ackMessage` for every message received.

### session.createSubscriptionSync(topic, [options])

Create a new subscription object, get ready to receive messages (synchronous version).

`topic` can be either a discrete or wildcard topic.

`options` is an object with the following details:

    {
        qos           : [Quality of service: 'BE'|'GC'|'GD'],   (String, optional (default: 'GC'))
        name          : [Subscription name],                    (String, only required when using GD)
        ackMode       : [message ACK mode: 'auto'|'manual']     (String, only required when using GD (default: 'auto'))
    }

With `ackMode` set to `auto` messagse will be acknowledged once the message event listener completes.  With `ackMode` set to `manual` the application must call `subscription.ackMessage` for every message received.  See `Subscription.ackMessage` for more information.

On success `createSubscriptionSync` returns a `Subscription` object.  On failure createSubscriptionSync returns a `String` object, the text being the reason for failure.

### session.createReplay(topic, options, callback)

Create a new replay object, get ready to receive messages.

`topic` can be either a discrete or wildcard topic.

`options` is an object with the following details:

    {
        startTime     : [Replay start time, in UTC]             (Date, required)
        endTime       : [Replay end time, in UTC]               (Date, required)
    }

`callback` is a function with the following prototype:

    function (err, replay) {
        // If 'err' is set an error occurred and the replay was not created successfully
        // Otherwise 'replay' is the newly created Replay object
    });


### session.createReplaySync(topic, options)

Create a new replay object, get ready to receive messages (synchronous version).

`topic` can be either a discrete or wildcard topic.

`options` is an object with the following details:

    {
        startTime     : [Replay start time, in UTC]             (Date, required)
        endTime       : [Replay end time, in UTC]               (Date, required)
    }

On success `createReplaySync` returns a `Replay` object.  On failure createReplaySync returns a `String` object, the text being the reason for failure.

### session.close([callback])

Logout and disconnect from the Tervela fabric.

`callback` will be added as a listener for the 'close' event.

### Event: 'connection-info'
* activeTmx
* [standbyTmx]

Emitted shortly after the session is first connected.  The argument `activeTmx` will be a `String`, the IP address of the active TMX.  If connected to a fault-tolerant pair the `standby` argument will be a `String`, the IP address of the standby TMX.


### Event: 'connection-lost'

Emitted when the connection to the TMX(s) is lost.  The API will automatically attempt to reconnect.

### Event: 'connection-restored'

Emitted when the connection to the TMX is restored (after being lost).

### Event: 'gds-lost'

Emitted when communications with the GDS have been lost.  The API will automatically attempt to re-establish communications.

### Event: 'gds-restored'

Emitted when communications with the GDS have been restored.

### Event: 'close'

Emitted when the session has been closed.

### Event: 'notify'

* code
* message

Emitted when a miscellaneous session event occurs.  If one of the above events is emitted and not handled by an explicit listener it will end up here.

## Class: tervela.Publication

This object is used to send messages.

### publication.sendMessage(topic, message, [options], [callback])

Send a message on the given topic.

`options` is an object with the following details:

    {
        selfdescribe  : [ignore topic schema],                  (boolean, default: false)
    }

`callback` will be added as a listener for the 'send-message' event.

The `topic` used when sending a message must match the `topic` used to create the publication, either directly or within a wildcard subset.  For example, if the publication topic is a discrete topic such as A.1, the message must be sent on topic A.1.  However if the publication topic is a wildcard topic such as A.* then the message can be sent on any topic that falls under A.* (A.A, A.1, A.2, etc.).

The `message` is a JavaScript object.  The property names become the field names in the Tervela message, and the property values become those field values.

`selfdescribe` makes the message "self-describing" or not.  When `selfdescribe` is set to `false` (the default), property names must match schema field names; when `selfdescribe` is set to `true`, the schema field names are ignored and the property names become the field names.

### publication.stop([callback])

Stop the publication.

`callback` will be added as a listener for the 'stop' event.

### Property: 'topic'

Gets the topic the publication was created on.  This is a read-only value.

### Property: 'qos'

Gets the quality of service of the publication.  Possible values are "BE", "GC", and "GD".  This is a read-only value.

### Event: 'send-message'

* err
* message

Emitted when a message has been sent.  If `err` is set it will be a `String` object, the text of the error that occurred.  `message` will be the message that was sent.

### Event: 'stop'

* err

Emitted when the `publication` is stopped.  If `err` is set it will be a `String` object, the text of the error that occurred.

## Class: tervela.Subscription

### subscription.acknowledge(message, [callback])

Acknowledge a received message when using "manual" ack mode with GD.

`message` is the message to acknowledge.

`callback` will be added as a listener for the 'ack' event.

Messages received on a GD subscription must be acknowledged.  This informs the system the message has been consumed.  If `ackMode` on the subscription is set to "auto" the acknowledgement happens automatically after the `message` event listener returns.  If `ackMode` is set to "manual", however, the application is responsible for acknowledging the message.  The `message` object passed to the `ackMessage` method is the same `message` object that was given to the application in the `message` event listener.

### subscription.stop([callback])

Stop the subscription.

The `callback` parameter will be added as a listener for the 'stop' event.

### Property: 'topic'

Gets the topic the publication was created on.  This is a read-only value.

### Property: 'qos'

Gets the quality of service of the publication.  Possible values are "BE", "GC", and "GD".  This is a read-only value.

### Event: 'message'

* message

Emitted when a message has been received.  `message` is an object with the following details:

    {
        topic,                 (String : message topic)
        generationTime,        (Date : when the message was sent by the publisher)
        receiveTime,           (Date : when the message was received)
        lossGap,               (Number : number of missing (lost) messages between this message and the last in order message on this topic)
        fields                 (Object : message fields list ([name]=value))
    }

### Event: 'ack'

* err
* message

Emitted when message acknowledgement completes (after calling `subscription.acknowledge`).  If `err` is set it will be a `String` object, the text of the error that occurred.  `message` is the message which was acknowledged.

### Event: 'stop'

* err

Emitted when the `subscription` is stopped.  If `err` is set it will be a `String` object, the text of the error that occurred.

## Class: tervela.Replay

This class represents an active replay.

### replay.pause([callback])

Pause an active replay

`callback` will be added as a listener for the 'pause' event.

### replay.resume([callback])

Resume a paused replay

`callback` will be added as a listener for the 'resume' event.

### replay.stop([callback])

Stop an active or paused replay

`callback` will be added as a listener for the 'stop' event.

`replay.stop` is only required if the application wishes to stop a replay that has not completed.  When a replay completes (either with the 'finish' or 'error' event) internal resources are freed when the object is garbage collected.

### Event: 'message'

Emitted when a message has been received.  `message` is an object with the following details:

    {
        topic,                 (String : message topic)
        generationTime,        (Date : when the message was sent by the publisher)
        receiveTime,           (Date : when the message was received)
        lossGap,               (Number : number of missing (lost) messages between this message and the last in order message on this topic)
        fields                 (Object : message fields list ([name]=value))
    }

### Event: 'pause'

* err

Emitted when the `replay` is paused.  If `err` is set it will be a `String` object, the text of the error that occurred.

### Event: 'resume'

* err

Emitted when the `replay` is resumed after a pause.  If `err` is set it will be a `String` object, the text of the error that occurred.

### Event: 'stop'

* err

Emitted when the `replay` is artificially stopped via `replay.stop`.  If `err` is set it will be a `String` object, the text of the error that occurred.

### Event: 'finish'

Emitted when the replay finishes, meaning no additional messages will be received.  This listener is invoked after the `message` listener for that last message.

When the `finish` or `error` listeners are invoked the internal reference to the Replay object is released.  If the application does not have any references to the object it will then be freed and eligible for garbage collection.

### Event: 'error'

Emitted when the replay encounters an error, such as when the data could not be found on the TPE.  No messages will be received when these errors occur.

When the `finish` or `error` listeners are invoked the internal reference to the Replay object is released.  If the application does not have any references to the object it will then be freed and eligible for garbage collection.

## Class: tervela.Logger

The `Logger` gives write access to the Tervela API log file.  Logging is controlled by a bitmask of active log levels.  When the application asks to write something to the log, the log level of the write is checked against the list of currently active levels.  If the log level is active the data is written to the log; if the log level is not active the data is not written.  This allows the application to write as many log statements as required for field debugging while knowing the log will not be populated unless in debug mode.

### Enum: Logger.Level

The `Logger` exports the following log levels:

    * Level.ERROR   (0x00000001)
    * Level.WARN    (0x00000002)
    * Level.INFO    (0x00000004)
    * Level.DATA    (0x00000008)
    * Level.STATE   (0x00000010)
    * Level.VSTATE  (0x00000020)
    * Level.STATS   (0x00000040)
    * Level.QSTATS  (0x00000080)
    * Level.RSTATS  (0x00000100)
    * Level.LSTATS  (0x00000200)
    * Level.VSTATS  (0x00000400)
    * Level.DIAG    (0x00000800)
    * Level.VDIAG   (0x00001000)

The application is free to assign any meaning to each of the log levels it desires, but know that `ERROR`, `WARN`, and `STATE` are enabled by default.

### Logger.getLevels - Get the bitmask of currently active log levels

    var levels = logger.getLevels();

* The return value will be the sum of all active log levels (from above).

### Logger.setLevel - Enable the given log levels

    logger.setLevel(/*One or more of the above log levels*/);

Example:

    logger.setLevel(logger.Level.INFO | logger.Level.VSTATE | logger.Level.DATA);

* The given level(s) are added to the currently active level(s).  So in the above example, assuming the default `ERROR`, `WARN`, and `STATE` were previously active, the new levels would be `ERROR`, `WARN`, `STATE`, `INFO`, `VSTATE`, and `DATA`.

### Logger.clearLevel - Clear the given log levels from the mask of currently active levels

    logger.clearLevel(/*One or more of the above log levels*/);

Example:

    logger.clearLevel(logger.Level.INFO | logger.Level.DATA);

* The given level(s) are removed from the currently active level(s).  Continuing the above example, the new levels would be `ERROR`, `WARN`, `STATE`, and `VSTATE`.

### Logger.write - Write a message to the log

	logger.write(/*One or more of the above log levels*/, "Message to write to the log");

* If the given log level is among the list of currently active log levels the message will be written to the log; otherwise the function will return without writting the message.
