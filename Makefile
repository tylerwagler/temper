CXX = g++
PREFIX = /usr/local

# Try pkg-config first
PKG_CONFIG_NVML = $(shell pkg-config --list-all 2>/dev/null | grep -o 'nvidia-ml[^ ]*' | head -1)
ifneq ($(PKG_CONFIG_NVML),)
    NVML_CFLAGS = $(shell pkg-config --cflags $(PKG_CONFIG_NVML) 2>/dev/null)
    NVML_LIBS = $(shell pkg-config --libs $(PKG_CONFIG_NVML) 2>/dev/null)
else
    ifeq ($(NVML_CFLAGS),)
        $(error NVML not found via pkg-config. Please provide NVML_CFLAGS and NVML_LIBS.)
    endif
endif

CXXFLAGS = -Wall -Wextra -std=c++17 -O2 $(NVML_CFLAGS)
LDFLAGS = $(NVML_LIBS)

SRCDIR = src
BUILDDIR = build

TARGET = $(BUILDDIR)/temper
SOURCES = $(SRCDIR)/main.cpp $(SRCDIR)/NVMLManager.cpp $(SRCDIR)/CurveController.cpp $(SRCDIR)/IpmiController.cpp $(SRCDIR)/MetricServer.cpp $(SRCDIR)/HostMonitor.cpp $(SRCDIR)/LlamaMonitor.cpp $(SRCDIR)/ProcessUtils.cpp
OBJECTS = $(SOURCES:$(SRCDIR)/%.cpp=$(BUILDDIR)/%.o)

all: $(TARGET)

$(TARGET): $(OBJECTS) | $(BUILDDIR)
	$(CXX) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

$(BUILDDIR)/%.o: $(SRCDIR)/%.cpp | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

clean:
	rm -rf $(BUILDDIR)

install: $(TARGET)
	install -d $(PREFIX)/bin
	install -m 755 $(TARGET) $(PREFIX)/bin/

.PHONY: all clean install