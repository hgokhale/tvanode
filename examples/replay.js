/**
 * replay.js
 *
 * This application sends messages on a GD topic (it is assumed the TPE is subscribed to this topic)
 * and then replays those messages back.
 *
 * Usage:
 *   node replay.js --tmx=tmx[:tmx] [options]
 *      --user=username                  (optional, default: tervela)
 *      --pass=password                  (optional, default: tva123ma1)
 *      --tmx=tmx[:tmx]                  (required)
 *      --gdname=gd_client_name          (optional, default: ReplayJs)
 *      --topic=topic                    (optional, default: TEST.GD.JS)
 *      --count=msg_count                (optional, default: 100)
 */

// Set NODE_PATH environment variable to include location of tervela.node file
var tervela = require("tervela");

process.on('uncaughtException', function (err) {
    console.log(err.stack);
});

var _username = "tervela";
var _password = "tva123ma1";
var _primaryTmx = "";
var _secondaryTmx = "";
var _gdName = "ReplayJs";
var _topic = "TEST.GD.JS";
var _count = 100;
var _verbose = false;
var _exit = false;
var _testingComplete = false;

var args = process.argv.splice(2);
args.forEach(function (val, index, array) {
    var keyval = val.split("=");
    if (keyval.length == 2) {
        if (keyval[0] == "--user") {
            _username = keyval[1];
        }
        else if (keyval[0] == "--pass") {
            _password = keyval[1];
        }
        else if (keyval[0] == "--topic") {
            _topic = keyval[1];
        }
        else if (keyval[0] == "--count") {
            _count = keyval[1];
        }
        else if (keyval[0] == "--tmx") {
            var ps = keyval[1].split(":");
            _primaryTmx = ps[0];
            if (ps.length > 1) {
                _secondaryTmx = ps[1];
            }
        }
        else if (keyval[0] == "--gdname") {
            _gdName = keyval[1];
        }
    }
    else if (keyval.length == 1) {
        if (keyval[0] == "--verbose") {
            _verbose = true;
        }
        else if (keyval[0] == "--help") {
            _exit = true;
        }
    }
});

if ((!_exit) && (_primaryTmx.length == 0)) {
    console.log("Missing TMX");
    _exit = true;
}

if (_exit) {
    printUsage();
}
else {
    console.log("Running with options:");
    console.log("  username     : " + _username);
    console.log("  password     : " + _password);
    console.log("  primaryTmx   : " + _primaryTmx);
    console.log("  secondaryTmx : " + _secondaryTmx);
    console.log("  gdName       : " + _gdName);
    console.log("  topic        : " + _topic);
    console.log("  count        : " + _count);
    console.log("  verbose      : " + _verbose);
    console.log("");

    var connectOptions = {
        username: _username,
        password: _password,
        primaryTmx: _primaryTmx,
        secondaryTmx: _secondaryTmx,
        name: _gdName
    };

    // Login to the TMX/TMXs
    console.log("Connecting to %s...", (_secondaryTmx) ? "TMXs" : "TMX");
    var session = tervela.connectSync(connectOptions);
    if (typeof session === "string") {
        console.log("Connect failed: " + session);
    }
    else {
        console.log("Connected.");

        // Set up session event listeners
        session
            .on('connection-info', function (activeTmx, standbyTmx) {
                var info = "* Session connected to active TMX " + activeTmx;
                if (standbyTmx) {
                    info += ", standby TMX " + standbyTmx;
                }
                console.log(info);
            })
            .on('connection-lost', function () {
                console.log("* Lost session connection, all operations affected");
            })
            .on('connection-restored', function () {
                console.log("* Session connection restored, all operations will continue");
            })
            .on('gds-lost', function () {
                console.log("* Lost communications with the GDS, GD operations affected");
            })
            .on('gds-restored', function () {
                console.log("* Communications with the GDS have been restored, GD operations will continue");
            });

        // Create publication
        var publication = session.createPublicationSync(_topic);
        if (typeof publication === "string") {
            console.log("Failed to create publication on topic %s: %s", _topic, publication);
            shutdown(session);
        }
        else {
            // Send messages
            console.log("Sending %d messages...", _count);

            var startTime = new Date();
            sendMessages(publication, _topic, _count, function () {
                var endTime = new Date();
                console.log("Sent %d messages, initiating replay...", _count);

                if (_verbose) {
                    console.log("Replay on topic %s from %s to %s", _topic, startTime.toLocaleTimeString(), endTime.toLocaleTimeString());
                }

                var replay = session.createReplaySync(_topic, { startTime: startTime, endTime: endTime });
                if (typeof replay === "string") {
                    console.log("Failed to create replay on topic %s: %s", _topic, replay);
                    shutdown(session);
                }
                else {
                    var replayCount = 0;
                    replay.on('message', function (msg) {
                        replayCount++;
                        if (_verbose) {
                            console.log("Replayed message " + replayCount);
                        }
                    }).on('error', function (err) {
                        console.log("Replay failed: " + err);
                        shutdown(session);
                    }).on('finish', function () {
                        console.log("Replay complete, replayed %d messages.", replayCount);
                        shutdown(session);
                    });
                }
            });
        }
    }
}

function sendMessages(publication, topic, total, sendAllComplete) {
    var sent = 0;

    for (var i = 0; i < total; i++) {
        var message = {
            count: i,
            total: total,
            timestamp: new Date()
        };

        publication.sendMessage(topic, message, { selfdescribe: true }, function (err) {
            sent++;
            if (err) {
                console.log("Error sending message: " + err);
            }
            else if (_verbose) {
                console.log("Sent message " + sent);
            }

            if (sent == total) {
                setTimeout(sendAllComplete, 0);
            }
        });
    }
}

function shutdown(session) {
    console.log("Disconnecting from TMX...");
    session.close(function (err) {
        console.log("Session closed");
    });
}

function printUsage() {
    console.log("Usage: node pub.js --tmx=tmx[:tmx] [options]");
    console.log("  --user=username                  (optional, default: tervela)");
    console.log("  --pass=password                  (optional, default: tva123ma1)");
    console.log("  --gdname=gd_client_name          (optional, default: ReplayJs)");
    console.log("  --topic=topic                    (optional, default: TEST.GD.A)");
    console.log("  --count=msg_count                (optional, default: 100)");
    console.log("  --verbose                        (optional, default: false)");
}
