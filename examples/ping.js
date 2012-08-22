/**
 * ping.js
 *
 * Ping latency test application
 *
 * Usage:
 *   node ping.js --tmx=tmx[:tmx] [options]
 *      --user=username             (optional, default: tervela)
 *      --pass=password             (optional, default: tva123ma1)
 *      --topic=topic               (optional, default: PING)
 *      --tmx=tmx[:tmx]             (required)
 *      --count=message_count       (optional, default: 100)
 *      --delay=inter_msg_gap_ms    (optional, default: 10)
 */

// Set NODE_PATH environment variable to include location of tervela.node file
var tervela = require("tervela");

process.on('uncaughtException', function (err) {
    console.error(err.stack);
});

process.on('exit', function () {
    console.log("Done.");
});

var username = "tervela";
var password = "tva123ma1";
var topic = "PING";
var primaryTmx = "";
var secondaryTmx = "";
var count = 100;
var delay = 10;
var verbose = false;
var exit = false;

// Process command line arguments
var args = process.argv.splice(2);
args.forEach(function (val, index, array) {
    var keyval = val.split("=");
    if (keyval.length == 2) {
        if (keyval[0] == "--user") {
            username = keyval[1];
        }
        else if (keyval[0] == "--pass") {
            password = keyval[1];
        }
        else if (keyval[0] == "--topic") {
            topic = keyval[1];
        }
        else if (keyval[0] == "--count") {
            count = keyval[1];
        }
        else if (keyval[0] == "--delay") {
            delay = keyval[1];
        }
        else if (keyval[0] == "--tmx") {
            var ps = keyval[1].split(":");
            primaryTmx = ps[0];
            if (ps.length > 1) {
                secondaryTmx = ps[1];
            }
        }
    }
    else if (keyval.length == 1) {
        if (keyval[0] == "--verbose") {
            verbose = true;
        }
        else if (keyval[0] == "--help") {
            exit = true;
        }
    }
});

if ((!exit) && (primaryTmx.length == 0)) {
    console.log("Missing TMX");
    exit = true;
}

if (exit) {
    printUsage();
}
else {
    console.log("Running with options:");
    console.log("  username     : " + username);
    console.log("  password     : " + password);
    console.log("  primaryTmx   : " + primaryTmx);
    console.log("  secondaryTmx : " + secondaryTmx);
    console.log("  topic        : " + topic);
    console.log("  count        : " + count);
    console.log("  delay        : " + delay);
    console.log("  verbose      : " + verbose);
    console.log("");

    var latencies = new Array();
    var txcount = 0;
    var rxcount = 0;
    var testingComplete = false;
    var startTime, endTime;

    // Login to the TMX/TMXs
    tervela.connect({
        username: username,
        password: password,
        primaryTmx: primaryTmx,
        secondaryTmx: secondaryTmx
    }, function (err, session) {
        if (err) {
            console.log("Login failed: " + err);
            return;
        }

        session.on('notify', function (code, msg) {
            console.log("* Session notification %d : %s", code, msg);
        });

        console.log("Login complete");

        var subscription, publication;

        // Create subscription
        subscription = session.createSubscription();
        subscription
            .start(topic, { qos: 'BE' })
            .on('message', function (msg) {
                try {
                    var lat = msg.fields["time"];
                    var now = new Date();
                    var diff = now.getTime() - lat.getTime();
                    latencies.push(diff);

                    if (verbose) {
                        console.log("RX: %d : %d %d (%d)", rxcount, now, lat, diff);
                    }

                    rxcount++;
                    if (rxcount == count) {
                        testComplete(subscription, publication, session);
                    }
                }
                catch (e) {
                    console.log("messageEvent: Caught exception: " + e.message);
                    console.log(e.stack);
                }
            })
            .on('start', function (err) {
                if (err) {
                    console.log("Error creating subscription to " + topic + ": " + err);
                    setTimeout(function () { shutdown(subscription, publication, session); }, 10);
                    return;
                }

                console.log("Start subscription complete");

                // Create publication
                session.createPublication(topic, function (err, pub) {
                    if (err) {
                        console.log("Error creating publication to " + topic + ": " + err);
                        setTimeout(function () { shutdown(subscription, publication, session); }, 10);
                        return;
                    }

                    publication = pub;

                    console.log("Start publication complete");
                    startTime = new Date();

                    setTimeout(function () { sendMessage(pub); }, delay);
                });
            });
    });
}

function sendMessage(publication) {
    try {
        var message = {
            time: new Date()
        };

        if (verbose) {
            console.log("TX: %d : %d", txcount, message["time"].getTime());
        }

        publication.sendMessage(topic, message, { selfdescribe: true}, function (err) {
            if (err) {
                console.log("-- Error sending message: " + err);
            }
        });

        txcount++;
        if (txcount < count) {
            setTimeout(function () { sendMessage(publication); }, delay);
        }
        else {
            // Need to keep the script running while we wait for all 
            // messages to be received
            setTimeout(waitForCompletion, 500);
        }
    }
    catch (e) {
        console.log("sendMessage: Caught exception: " + e.message);
        console.log(e.stack);
    }
}

function waitForCompletion() {
    if (!testingComplete) {
        setTimeout(waitForCompletion, 500);
    }
}

function testComplete(subscription, publication, session) {
    endTime = new Date();
    var duration = endTime.getTime() - startTime.getTime();
    
    var total = 0;
    latencies.sort(function (a,b) { return (a - b); });    
    latencies.forEach(function (val, index, array) {
        total += val;
    });
    
    var min = latencies[0];
    var max = latencies[latencies.length - 1];
    var mean = total / latencies.length;
    
    console.log("\n*** Latency results (ms):");
    console.log("* Count:%d  min:%d  max:%d  mean:%d  runtime:%d", latencies.length, min, max, mean, duration);
    console.log(" ");

    shutdown(subscription, publication, session);
    testingComplete = true;
}

function shutdown(subscription, publication, session) {
    if (publication) {
        shutdownPublication(subscription, publication, session);
    }
    else if (subscription) {
        shutdownSubscription(subscription, session);
    }
    else {
        shutdownSession(session);
    }
}

function shutdownPublication(subscription, publication, session) {
    publication.stop(function (err) {
        console.log("Stop publication complete");
        shutdownSubscription(subscription, session);
    });
}

function shutdownSubscription(subscription, session) {
    subscription.stop(function (err) {
        console.log("Stop subscription complete");
        shutdownSession(session);
    });
}

function shutdownSession(session) {
    session.close(function (err) { console.log("Logout complete"); });
}

function printUsage() {
    console.log("Usage: node ping.js --tmx=tmx[:tmx] [options]");
    console.log("  --user=username                  (optional, default: tervela)");
    console.log("  --pass=password                  (optional, default: tva123ma1)");
    console.log("  --topic=topic                    (optional, default: PING)");
    console.log("  --count=message_count            (optional, default: 100)");
    console.log("  --delay=inter_msg_gap_ms         (optional, default: 10)");
    console.log("  --verbose                        (optional, default: false)");
}
