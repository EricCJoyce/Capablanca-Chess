#  Build a Docker image that can compile C and C++ into WebAssembly.
#          sudo docker build -t emscripten-c .

FROM emscripten/emsdk

WORKDIR /home
RUN git clone --recursive https://github.com/WebAssembly/wabt
WORKDIR /home/wabt
RUN git submodule update --init
RUN mkdir build
WORKDIR /home/wabt/build
RUN cmake ..
RUN cmake --build .
WORKDIR /home
RUN mkdir src
WORKDIR /home/src
