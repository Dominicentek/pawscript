ifeq ($(OS),Windows_NT)
	HOST_OS ?= Windows
else
	HOST_OS ?= $(shell uname -s 2>/dev/null || echo Unknown)
	ifneq (,$(findstring MINGW,HOST_OS))
		HOST_OS := Windows
	endif
endif

ifeq ($(HOST_OS),Windows)
	LIBRARY := pawscript.dll
	EXECUTABLE := paws.exe
elif ($(HOST_OS),Darwin)
	LIBRARY := pawscript.dylib
	EXECUTABLE := paws
else
	LIBRARY := libpawscript.so
	EXECUTABLE := paws
endif

.PHONY: all clean
all: $(EXECUTABLE)

$(LIBRARY): pawscript.cpp
	clang++ pawscript.cpp -shared -g -O2 -fPIC -o $(LIBRARY)

$(EXECUTABLE): $(LIBRARY) interpreter.c
	clang interpreter.c -g -O2 -L. -lpawscript -o $(EXECUTABLE)

clean:
	rm $(LIBRARY) $(EXECUTABLE)

