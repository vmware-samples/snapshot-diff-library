# Copyright 2020-2021 VMware, Inc.
# SPDX-License-Identifier: BSD-2-Clause

CXXFLAGS = -static -Wall -std=c++14
CCFLAGS  = $(CXXFLAGS)

all: Linux/snapshot-diff Windows/snapshot-diff.exe

Linux/snapshot-diff: Linux/snapshot_diff.o snapshot_diff_cmd.cpp
	g++ $(CCFLAGS) -o $@ $^

Linux/snapshot_diff.o: snapshot_diff.cpp snapshot_diff.h json_writer.h
	mkdir -p Linux
	g++ -c $(CCFLAGS) -o $@ snapshot_diff.cpp

Windows/snapshot-diff.exe: Windows/snapshot_diff.o snapshot_diff_cmd.cpp
	x86_64-w64-mingw32-g++ -static-libgcc -static-libstdc++ $(CCFLAGS) -o $@ $^

Windows/snapshot_diff.o: snapshot_diff.cpp snapshot_diff.h json_writer.h
	mkdir -p Windows
	x86_64-w64-mingw32-g++ -c $(CCFLAGS) -o $@ -static-libgcc -static-libstdc++ snapshot_diff.cpp

clean:
	rm -rf Linux
	rm -rf Windows
