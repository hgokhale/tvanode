# Replace binding.gyp with this file to build tvaNode on node 0.6

{
  "targets": [
    {
        'target_name': "tervela",
        'sources': [ "src/Tervela.cpp", "src/Session.cpp", "src/Publication.cpp", "src/Subscription.cpp", "src/compat.cpp" ],
        'include_dirs': [ "./gyp/include/cvv8" ],
        'conditions': [
            ['OS=="win"',
                {
                    'include_dirs': [
                        "C:/Program Files/Tervela/Tervela Client APIs/include/tervelaapi",
                        "C:/Program Files (x86)/Tervela/Tervela Client APIs/include/tervelaapi"
                    ],
                    'libraries': [
                        "ws2_32.lib",
                        "C:/Program Files/Tervela/Tervela Client APIs/lib/TervelaAPI.lib",
                        "C:/Program Files (x86)/Tervela/Tervela Client APIs/lib/TervelaAPI.lib",
                        "C:/Program Files/Tervela/Tervela Client APIs/lib/TervelaGDAPI.lib",
                        "C:/Program Files (x86)/Tervela/Tervela Client APIs/lib/TervelaGDAPI.lib"
                    ]
                }
            ],
            ['OS=="linux"',
                {
                    'include_dirs': [ "/opt/tervela/include/tervelaapi" ],
                    'libraries': [ "/opt/tervela/lib/libtervelaapi.so", "/opt/tervela/lib/libgdapi.so" ],
                }
            ]
        ],
        'variables': {
          'node_ver': '<!(node --version | sed -e "s/^v\([0-9]*\\.[0-9]*\).*$/\\1/")'
        },
        'target_conditions': [
            ['node_ver<"0.8"', { 'defines': [ "MISSING_UV_THREADS" ] } ]
        ],
    },
  ]
}
