# wx-hook-sol -- private minimal sender
CC       := gcc
CXX      := g++
CXXFLAGS := -O2 -std=c++17 -Wall -Wextra -Wno-unused-parameter -Wno-stringop-truncation \
            -D__USE_MINGW_ANSI_STDIO=1 -D_WIN32_WINNT=0x0601 -DWIN32_LEAN_AND_MEAN -MMD -MP \
            -Isrc -Ithird_party -Ithird_party/minhook/include \
            -Ithird_party/minhook/src -Ithird_party/minhook/src/hde
CFLAGS   := -O2 -Wall -D__USE_MINGW_ANSI_STDIO=1 -D_WIN32_WINNT=0x0601 -DWIN32_LEAN_AND_MEAN \
            -Ithird_party/minhook/include -Ithird_party/minhook/src \
            -Ithird_party/minhook/src/hde -MMD -MP
LDDLL    := -shared -static -static-libgcc -static-libstdc++ -Wl,--exclude-all-symbols
LIBS     := -lws2_32 -lpsapi -lole32

DLL_SRCS  := src/app/dllmain.cpp src/app/routes.cpp \
             src/platform/runtime.cpp \
             src/transport/http.cpp src/transport/server.cpp \
             src/wechat/account.cpp src/wechat/logic.cpp \
             src/wechat/media.cpp src/wechat/crash.cpp \
             src/features/send.cpp src/features/pat.cpp src/features/status.cpp
MH_SRCS   := third_party/minhook/src/hook.c third_party/minhook/src/buffer.c \
             third_party/minhook/src/trampoline.c third_party/minhook/src/hde/hde64.c
OBJS      := $(DLL_SRCS:.cpp=.o) $(MH_SRCS:.c=.o)

.PHONY: all clean
all: dist/wxhook.dll dist/wxhook_inject.exe

dist:
	mkdir -p dist

dist/wxhook.dll: $(OBJS) | dist
	$(CXX) $(LDDLL) -Wl,-Map=dist/wxhook.map -o $@ $(OBJS) $(LIBS)
	@echo "built dist/wxhook.dll"

dist/wxhook_inject.exe: src/injector/main.cpp | dist
	$(CXX) -O2 -std=c++17 -static -static-libgcc -static-libstdc++ \
		-D_WIN32_WINNT=0x0601 -DWIN32_LEAN_AND_MEAN \
		-o $@ src/injector/main.cpp -lpsapi -lws2_32
	@echo "built dist/wxhook_inject.exe"

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) dist/wxhook.dll dist/wxhook_inject.exe dist/wxhook.map

-include $(OBJS:.o=.d)
