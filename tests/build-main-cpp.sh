em++ -s ASYNCIFY=1Heb  -s STANDALONE_WASM=1 -s INVOKE_RUN=0 -s WASM_BIGINT=1 -s MODULARIZE=1 -s EXPORT_KEEPALIVE=1 -s EXPORT_ES6=1 --bind -I ../src main.cpp ../cmake-build-debug-emscripten/lib/libserial.a -o serial.js -s "EXPORTED_FUNCTIONS=['_main','_open']"
#em++ --bind -I ../src main.cpp ../cmake-build-debug-emscripten/lib/libserial.a -o serial.js -s "EXPORTED_FUNCTIONS=['_main','_open']"
