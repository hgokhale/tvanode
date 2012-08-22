all:
	node-gyp configure build
clean:
	node-gyp clean
rebuild:
	node-gyp rebuild
