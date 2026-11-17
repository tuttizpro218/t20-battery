CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra
PREFIX   ?= $(HOME)/.local

all: t20-battery

t20-battery: src/t20-battery.cpp
	$(CXX) $(CXXFLAGS) -o $@ $<

install: t20-battery
	install -Dm755 t20-battery $(PREFIX)/bin/t20-battery

clean:
	rm -f t20-battery

.PHONY: all install clean
