# Replace binding.gyp with this file to build tvaNode on node 0.6

{
  "targets": [
    {
        'target_name': "tervela",
        'sources': [ "src/Tervela.cpp", "src/Session.cpp", "src/Session_Create.cpp", 
                     "src/Publication.cpp", "src/Subscription.cpp", "src/Replay.cpp", 
                     "src/EventEmitter.cpp", "src/Logger.cpp", "src/compat.cpp" ],
        'include_dirs': [ "./gyp/include/cvv8" ],
        'conditions': [
            ['OS=="win"',
                {
                    'include_dirs': [
                        "C:/Program Files/Tervela/Tervela Client APIs/include/tervelaapi"
                    ],
                    'libraries': [
                        "ws2_32.lib",
                        "C:/Program Files/Tervela/Tervela Client APIs/lib/TervelaAPI.lib",
                        "C:/Program Files/Tervela/Tervela Client APIs/lib/TervelaGDAPI.lib",
                        "C:/Program Files/Tervela/Tervela Client APIs/lib/TervelaPEAPI.lib"
                    ]
                }
            ],
            ['OS=="linux"',
                {
                    'include_dirs': [ "/opt/tervela/include/tervelaapi" ],
                    'libraries': [ "/opt/tervela/lib/libtervelaapi.so", "/opt/tervela/lib/libgdapi.so", "/opt/tervela/lib/libpeapi.so" ],
                    'cflags': [ '-g' ],
                }
            ]
        ]
    }
  ]
}
