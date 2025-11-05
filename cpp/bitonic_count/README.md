# Bitonic count microbenchmark

This is a quick, dirty, and naive implementation of bitonic count along with a microbenchmark against a simple atomic counter. The implementation is based on Chapter 12 of The Art of Multiprocessing and rewritten in C++ using ChatGPT 5 with some polishing from me. Unfortunately, it is neither well-polished nor tested. However, it looks correct.

## Build

```bash
mkdir -p build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
```

Note that, by default, you might have an old compiler, so you may prefer to specify the compiler manually:
```
mkdir -p build-clang && cd build-clang
CC=clang-18 CXX=clang++-18 cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j
```

This produces two executables:

- `bitonic_count` (from `main.cpp`): microbenchmark
- `demo` (from `demo.cpp`): simple functionality demo

## How to run

- `bitonic_count` usage: the first argument is the number of threads; the optional second argument is `-b` to use the bitonic network instead of a plain atomic counter, or `-o` to make each thread use its own atomic counter. The result is ops/second (total across all threads).

Examples:

```bash
# Atomic counter benchmark with 8 threads for ~10 seconds
./bitonic_count 8

# Bitonic network benchmark with 8 threads for ~10 seconds
./bitonic_count 8 -b

# Demo program
./demo
```

On NUMA systems, it is recommended to use either `taskset` or a cgroup to run threads on the same node:

```
build-clang > ./bitonic_count 64
21446246

build-clang > taskset -c 32-63,96-127 ./bitonic_count 64
38270180
```

## Some results

### A 128-core server

I have two Intel(R) Xeon(R) Gold 6338 CPUs @ 2.00 GHz with Hyper-Threading enabled.

```
> numactl --hardware
available: 2 nodes (0-1)
node 0 cpus: 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31 64 65 66 67 68 69 70 71 72 73 74 75 76 77 78 79 80 81 82 83 84 85 86 87 88 89 90 91 92 93 94 95
node 0 size: 257546 MB
node 0 free: 120262 MB
node 1 cpus: 32 33 34 35 36 37 38 39 40 41 42 43 44 45 46 47 48 49 50 51 52 53 54 55 56 57 58 59 60 61 62 63 96 97 98 99 100 101 102 103 104 105 106 107 108 109 110 111 112 113 114 115 116 117 118 119 120 121 122 123 124 125 126 127
node 1 size: 258032 MB
node 1 free: 80671 MB
node distances:
node   0   1
  0:  10  20
  1:  20  10
```

I set the host to the throughput-performance profile:
```
/usr/sbin/tuned-adm profile throughput-performance
```

Raw data:
```
threads,atomic,bitonic,own
2,27414033,17606687,245324243
4,24215605,10493005,490757716
8,20818010,12794355,981062136
16,18555367,13778151,1964119054
32,20132574,15978403,3940758908
64,21517080,18251228,7727336016
```

The "own" scales linearly with the number of threads. The rest behave in an interesting way:
![Bitonic benchmark (Intel 64)](img/bitonic_intel_64.png)

But it is clear that bitonic (at least a quick implementation) is slower than regular shared atomic.