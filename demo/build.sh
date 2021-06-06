SHELL_FOLDER=$(cd "$(dirname "$0")";pwd)
mkdir -p $SHELL_FOLDER/build
cmake -S $SHELL_FOLDER/.. -B $SHELL_FOLDER/build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_CXX_FLAGS=-s ENVIRONMENT=web -s EXPORTED_RUNTIME_METHODS=['ccall','callMain','FS'] -s DISABLE_EXCEPTION_CATCHING=0" -DCMAKE_EXECUTABLE_SUFFIX_CXX=.html -DCMAKE_CXX_COMPILER=em++ -DCMAKE_C_COMPILER=emcc
cmake --build $SHELL_FOLDER/build -j4
cp $SHELL_FOLDER/build/syc.js $SHELL_FOLDER/www
cp $SHELL_FOLDER/build/syc.wasm $SHELL_FOLDER/www
echo "Live demo is in \`$SHELL_FOLDER/www\`, use webserver to serve them."
