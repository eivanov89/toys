# Coroutine Demo with TPC-C Benchmark Simulation

This is a simple coroutine demo that captures a real scenario involving cooperative threading. In particular,
it shows a way to implement TPC-C benchmark: there are thousands of terminals, each doing multiple async calls in a row after sleeping.

## How it works

The demo works as follows:
- There are multiple terminals (workers)
- Each terminal executes infinitely until stop is requested
- Execution includes: multiple async calls (emulated using DoAsyncRequest method) and sleeping

## Implementation Details

The implementation includes:
- Timer thread for handling async sleep requests
- Simple scheduler for terminals (workers)

## Building and Running

The program accepts the following command line arguments:
- `-t, --max-threads N`    Maximum number of threads (default: number of available CPUs)
- `-w, --max-terminals N`  Maximum number of terminals (default: 10)
- `-i, --interval N`       Execution interval in seconds (default: 10)
- `--multi`               Run benchmark with increasing number of terminals (1 to 128, x2 step)
- `-h, --help`            Show help message

Example:
```bash
# Run with specific number of threads and terminals
./program --max-threads 4 --max-terminals 100 --interval 30

# Run with default number of threads (number of CPUs)
./program --max-terminals 100 --interval 30

# Run benchmark with increasing number of terminals
./program --max-threads 4 --interval 30 --multi
```

# Performance

Initial version (23748c75817):
```
> LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 ./hello_coroutines -w 10000 -i 10 --max-threads 128 --multi
terminals: 1, RPS: 938.7, total tasks done: 9387, duration: 10 seconds
terminals: 2, RPS: 1838.1, total tasks done: 18381, duration: 10 seconds
terminals: 4, RPS: 3605.2, total tasks done: 36052, duration: 10 seconds
terminals: 8, RPS: 7100.7, total tasks done: 71007, duration: 10 seconds
terminals: 16, RPS: 13582.9, total tasks done: 135829, duration: 10 seconds
terminals: 32, RPS: 22918.8, total tasks done: 229188, duration: 10 seconds
terminals: 64, RPS: 39302.3, total tasks done: 393023, duration: 10 seconds
terminals: 128, RPS: 33489.5, total tasks done: 334895, duration: 10 seconds
terminals: 256, RPS: 40118, total tasks done: 401180, duration: 10 seconds
terminals: 512, RPS: 38789.2, total tasks done: 387892, duration: 10 seconds
terminals: 1024, RPS: 41791.7, total tasks done: 417917, duration: 10 seconds
terminals: 2048, RPS: 41026.1, total tasks done: 410261, duration: 10 seconds
terminals: 4096, RPS: 46002.9, total tasks done: 460029, duration: 10 seconds
terminals: 8192, RPS: 47836.8, total tasks done: 478368, duration: 10 seconds
```

Terminals pinned to threads (6b32523d00262f74):
```
eivanov89@ydb-vla-dev04-003> LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 ./hello_coroutines -w 10000 -i 10 --max-threads 128 --multi
terminals: 1, RPS: 930, total tasks done: 9300, duration: 10 seconds
terminals: 2, RPS: 1841.1, total tasks done: 18411, duration: 10 seconds
terminals: 4, RPS: 3608, total tasks done: 36080, duration: 10 seconds
terminals: 8, RPS: 7135, total tasks done: 71350, duration: 10 seconds
terminals: 16, RPS: 13424.6, total tasks done: 134246, duration: 10 seconds
terminals: 32, RPS: 23154.9, total tasks done: 231549, duration: 10 seconds
terminals: 64, RPS: 39781.6, total tasks done: 397816, duration: 10 seconds
terminals: 128, RPS: 32221.2, total tasks done: 322212, duration: 10 seconds
terminals: 256, RPS: 41398, total tasks done: 413980, duration: 10 seconds
terminals: 512, RPS: 47485.7, total tasks done: 474857, duration: 10 seconds
terminals: 1024, RPS: 48886.8, total tasks done: 488868, duration: 10 seconds
terminals: 2048, RPS: 55995.4, total tasks done: 559954, duration: 10 seconds
terminals: 4096, RPS: 55383.7, total tasks done: 553837, duration: 10 seconds
terminals: 8192, RPS: 57399.8, total tasks done: 573998, duration: 10 seconds
```

Removed condvar in future(4be5eef44f2b1):
```
> LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 ./hello_coroutines -w 10000 -i 10 --max-threads 128 --multi
terminals: 1, RPS: 939.8, total tasks done: 9398, duration: 10 seconds
terminals: 2, RPS: 1835.8, total tasks done: 18358, duration: 10 seconds
terminals: 4, RPS: 3663.5, total tasks done: 36635, duration: 10 seconds
terminals: 8, RPS: 7177.7, total tasks done: 71777, duration: 10 seconds
terminals: 16, RPS: 13626.1, total tasks done: 136261, duration: 10 seconds
terminals: 32, RPS: 23457.9, total tasks done: 234579, duration: 10 seconds
terminals: 64, RPS: 40125.9, total tasks done: 401259, duration: 10 seconds
terminals: 128, RPS: 34128.9, total tasks done: 341289, duration: 10 seconds
terminals: 256, RPS: 45553.6, total tasks done: 455536, duration: 10 seconds
terminals: 512, RPS: 49689.4, total tasks done: 496894, duration: 10 seconds
terminals: 1024, RPS: 57526.6, total tasks done: 575266, duration: 10 seconds
terminals: 2048, RPS: 57586.7, total tasks done: 575867, duration: 10 seconds
terminals: 4096, RPS: 60188.2, total tasks done: 601882, duration: 10 seconds
terminals: 8192, RPS: 69687.9, total tasks done: 696879, duration: 10 seconds
```

For comparison same version, but 1 thread:
```
> LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 ./hello_coroutines -w 10000 -i 5 --max-threads 2 --multi
terminals: 1, RPS: 939, total tasks done: 4695, duration: 5 seconds
terminals: 2, RPS: 1862, total tasks done: 9310, duration: 5 seconds
terminals: 4, RPS: 3689.6, total tasks done: 18448, duration: 5 seconds
terminals: 8, RPS: 7472, total tasks done: 37360, duration: 5 seconds
terminals: 16, RPS: 14403.2, total tasks done: 72016, duration: 5 seconds
terminals: 32, RPS: 28793.6, total tasks done: 143968, duration: 5 seconds
terminals: 64, RPS: 54425.6, total tasks done: 272128, duration: 5 seconds
terminals: 128, RPS: 98995.2, total tasks done: 494976, duration: 5 seconds
terminals: 256, RPS: 156262, total tasks done: 781312, duration: 5 seconds
terminals: 512, RPS: 238080, total tasks done: 1190400, duration: 5 seconds
terminals: 1024, RPS: 251085, total tasks done: 1255424, duration: 5 seconds
terminals: 2048, RPS: 246579, total tasks done: 1232896, duration: 5 seconds
terminals: 4096, RPS: 228557, total tasks done: 1142784, duration: 5 seconds
terminals: 8192, RPS: 224461, total tasks done: 1122304, duration: 5 seconds
```

Timer thread with busy wait (e9b3a72b0db27ef):
```
eivanov89@ydb-vla-dev04-003> LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 ./hello_coroutines -w 10000 -i 5 --max-threads 1 --multi
terminals: 1, RPS: 975.6, total tasks done: 4878, duration: 5 seconds
terminals: 2, RPS: 1952.4, total tasks done: 9762, duration: 5 seconds
terminals: 4, RPS: 3908, total tasks done: 19540, duration: 5 seconds
terminals: 8, RPS: 7683.2, total tasks done: 38416, duration: 5 seconds
terminals: 16, RPS: 15299.2, total tasks done: 76496, duration: 5 seconds
terminals: 32, RPS: 30451.2, total tasks done: 152256, duration: 5 seconds
terminals: 64, RPS: 60800, total tasks done: 304000, duration: 5 seconds
terminals: 128, RPS: 112000, total tasks done: 560000, duration: 5 seconds
terminals: 256, RPS: 189944, total tasks done: 949722, duration: 5 seconds
terminals: 512, RPS: 235418, total tasks done: 1177088, duration: 5 seconds
terminals: 1024, RPS: 226124, total tasks done: 1130620, duration: 5 seconds
terminals: 2048, RPS: 216678, total tasks done: 1083392, duration: 5 seconds
terminals: 4096, RPS: 249856, total tasks done: 1249280, duration: 5 seconds
terminals: 8192, RPS: 250675, total tasks done: 1253376, duration: 5 seconds

> LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 ./hello_coroutines -w 10000 -i 5 --max-threads 2 --multi
terminals: 1, RPS: 976, total tasks done: 4880, duration: 5 seconds
terminals: 2, RPS: 1952.8, total tasks done: 9764, duration: 5 seconds
terminals: 4, RPS: 3861.6, total tasks done: 19308, duration: 5 seconds
terminals: 8, RPS: 7672, total tasks done: 38360, duration: 5 seconds
terminals: 16, RPS: 15331.2, total tasks done: 76656, duration: 5 seconds
terminals: 32, RPS: 30393.6, total tasks done: 151968, duration: 5 seconds
terminals: 64, RPS: 60352, total tasks done: 301760, duration: 5 seconds
terminals: 128, RPS: 113408, total tasks done: 567040, duration: 5 seconds
terminals: 256, RPS: 169728, total tasks done: 848640, duration: 5 seconds
terminals: 512, RPS: 167731, total tasks done: 838656, duration: 5 seconds
terminals: 1024, RPS: 167936, total tasks done: 839680, duration: 5 seconds
terminals: 2048, RPS: 165478, total tasks done: 827392, duration: 5 seconds
terminals: 4096, RPS: 236556, total tasks done: 1182780, duration: 5 seconds
terminals: 8192, RPS: 170394, total tasks done: 851968, duration: 5 seconds

eivanov89@ydb-vla-dev04-003> LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 ./hello_coroutines -w 10000 -i 5 --max-threads 128 --multi
terminals: 1, RPS: 979, total tasks done: 4895, duration: 5 seconds
terminals: 2, RPS: 1950.8, total tasks done: 9754, duration: 5 seconds
terminals: 4, RPS: 3880, total tasks done: 19400, duration: 5 seconds
terminals: 8, RPS: 7720, total tasks done: 38600, duration: 5 seconds
terminals: 16, RPS: 15333.6, total tasks done: 76668, duration: 5 seconds
terminals: 32, RPS: 30436.2, total tasks done: 152181, duration: 5 seconds
terminals: 64, RPS: 58703, total tasks done: 293515, duration: 5 seconds
terminals: 128, RPS: 48198.4, total tasks done: 240992, duration: 5 seconds
terminals: 256, RPS: 47672.8, total tasks done: 238364, duration: 5 seconds
terminals: 512, RPS: 63870.8, total tasks done: 319354, duration: 5 seconds
terminals: 1024, RPS: 59423.8, total tasks done: 297119, duration: 5 seconds
terminals: 2048, RPS: 70394.4, total tasks done: 351972, duration: 5 seconds
terminals: 4096, RPS: 62747.6, total tasks done: 313738, duration: 5 seconds
terminals: 8192, RPS: 61676, total tasks done: 308380, duration: 5 seconds
```

Timer manager, i.e. multiple timer threads (f8d0312e1cbf778a):
```
eivanov89@ydb-vla-dev04-003> LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 ./hello_coroutines -w 130000 -i 5 --max-threads 128 --multi
terminals: 1, RPS: 977, total tasks done: 4885, duration: 5 seconds
terminals: 2, RPS: 1948.8, total tasks done: 9744, duration: 5 seconds
terminals: 4, RPS: 3874.8, total tasks done: 19374, duration: 5 seconds
terminals: 8, RPS: 7790.4, total tasks done: 38952, duration: 5 seconds
terminals: 16, RPS: 15612.6, total tasks done: 78063, duration: 5 seconds
terminals: 32, RPS: 31181.6, total tasks done: 155908, duration: 5 seconds
terminals: 64, RPS: 62321.8, total tasks done: 311609, duration: 5 seconds
terminals: 128, RPS: 121297, total tasks done: 606484, duration: 5 seconds
terminals: 256, RPS: 241200, total tasks done: 1206000, duration: 5 seconds
terminals: 512, RPS: 479443, total tasks done: 2397216, duration: 5 seconds
terminals: 1024, RPS: 923134, total tasks done: 4615668, duration: 5 seconds
terminals: 2048, RPS: 1.30377e+06, total tasks done: 6518857, duration: 5 seconds
terminals: 4096, RPS: 1.39185e+06, total tasks done: 6959247, duration: 5 seconds
terminals: 8192, RPS: 1.81254e+06, total tasks done: 9062689, duration: 5 seconds
terminals: 16384, RPS: 1.88345e+06, total tasks done: 9417267, duration: 5 seconds
terminals: 32768, RPS: 1.91472e+06, total tasks done: 9573615, duration: 5 seconds
terminals: 65536, RPS: 1.91176e+06, total tasks done: 9558810, duration: 5 seconds
```

Improved timer thread:
```
terminals: 1, RPS: 983.4, total tasks done: 4917, duration: 5 seconds
terminals: 2, RPS: 1946.6, total tasks done: 9733, duration: 5 seconds
terminals: 4, RPS: 3890.2, total tasks done: 19451, duration: 5 seconds
terminals: 8, RPS: 7794.4, total tasks done: 38972, duration: 5 seconds
terminals: 16, RPS: 15604.8, total tasks done: 78024, duration: 5 seconds
terminals: 32, RPS: 31175.4, total tasks done: 155877, duration: 5 seconds
terminals: 64, RPS: 62281, total tasks done: 311405, duration: 5 seconds
terminals: 128, RPS: 122081, total tasks done: 610407, duration: 5 seconds
terminals: 256, RPS: 243484, total tasks done: 1217421, duration: 5 seconds
terminals: 512, RPS: 484024, total tasks done: 2420120, duration: 5 seconds
terminals: 1024, RPS: 944383, total tasks done: 4721914, duration: 5 seconds
terminals: 2048, RPS: 1.33939e+06, total tasks done: 6696970, duration: 5 seconds
terminals: 4096, RPS: 1.41109e+06, total tasks done: 7055456, duration: 5 seconds
terminals: 8192, RPS: 1.79347e+06, total tasks done: 8967364, duration: 5 seconds
terminals: 16384, RPS: 1.79706e+06, total tasks done: 8985276, duration: 5 seconds
terminals: 32768, RPS: 1.7209e+06, total tasks done: 8604502, duration: 5 seconds
terminals: 65536, RPS: 1.70038e+06, total tasks done: 8501919, duration: 5 seconds
```

x2 timer threads:
```
eivanov89@ydb-vla-dev04-003> LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 ./hello_coroutines -w 130000 -i 5 --max-threads 128 --multi
terminals: 1, RPS: 983.4, total tasks done: 4917, duration: 5 seconds
terminals: 2, RPS: 1949.6, total tasks done: 9748, duration: 5 seconds
terminals: 4, RPS: 3881.4, total tasks done: 19407, duration: 5 seconds
terminals: 8, RPS: 7791.8, total tasks done: 38959, duration: 5 seconds
terminals: 16, RPS: 15612.6, total tasks done: 78063, duration: 5 seconds
terminals: 32, RPS: 31291.6, total tasks done: 156458, duration: 5 seconds
terminals: 64, RPS: 62601.8, total tasks done: 313009, duration: 5 seconds
terminals: 128, RPS: 123625, total tasks done: 618125, duration: 5 seconds
terminals: 256, RPS: 246397, total tasks done: 1231983, duration: 5 seconds
terminals: 512, RPS: 489340, total tasks done: 2446699, duration: 5 seconds
terminals: 1024, RPS: 971795, total tasks done: 4858973, duration: 5 seconds
terminals: 2048, RPS: 1.7667e+06, total tasks done: 8833517, duration: 5 seconds
terminals: 4096, RPS: 1.9732e+06, total tasks done: 9866012, duration: 5 seconds
terminals: 8192, RPS: 2.00563e+06, total tasks done: 10028158, duration: 5 seconds
terminals: 16384, RPS: 2.01666e+06, total tasks done: 10083305, duration: 5 seconds
terminals: 32768, RPS: 2.0178e+06, total tasks done: 10089023, duration: 5 seconds
terminals: 65536, RPS: 2.0228e+06, total tasks done: 10113979, duration: 5 seconds
```

Change mutexes to spinlocks
```
eivanov89@ydb-vla-dev04-003> LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 ./hello_coroutines -w 130000 -i 5 --max-threads 128 --multi
terminals: 1, RPS: 963.8, total tasks done: 4819, duration: 5 seconds
terminals: 2, RPS: 1955.6, total tasks done: 9778, duration: 5 seconds
terminals: 4, RPS: 3913.6, total tasks done: 19568, duration: 5 seconds
terminals: 8, RPS: 6376.6, total tasks done: 31883, duration: 5 seconds
terminals: 16, RPS: 14290.6, total tasks done: 71453, duration: 5 seconds
terminals: 32, RPS: 28601.8, total tasks done: 143009, duration: 5 seconds
terminals: 64, RPS: 58362.2, total tasks done: 291811, duration: 5 seconds
terminals: 128, RPS: 122380, total tasks done: 611898, duration: 5 seconds
terminals: 256, RPS: 243090, total tasks done: 1215452, duration: 5 seconds
terminals: 512, RPS: 486627, total tasks done: 2433137, duration: 5 seconds
terminals: 1024, RPS: 970420, total tasks done: 4852098, duration: 5 seconds
terminals: 2048, RPS: 1.8267e+06, total tasks done: 9133484, duration: 5 seconds
terminals: 4096, RPS: 2.01e+06, total tasks done: 10049989, duration: 5 seconds
terminals: 8192, RPS: 2.0501e+06, total tasks done: 10250501, duration: 5 seconds
terminals: 16384, RPS: 2.07679e+06, total tasks done: 10383959, duration: 5 seconds
terminals: 32768, RPS: 2.0806e+06, total tasks done: 10402976, duration: 5 seconds
terminals: 65536, RPS: 2.0929e+06, total tasks done: 10464495, duration: 5 seconds
```

Circular buffer and fast AsyncSleep (cedd5a911):
```
eivanov89@ydb-vla-dev04-003> LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libjemalloc.so.2 ./hello_coroutines -w 130000 -i 5 --max-threads 120 --multi
terminals: 1, RPS: 973.8, total tasks done: 4869, duration: 5 seconds
terminals: 2, RPS: 1860.2, total tasks done: 9301, duration: 5 seconds
terminals: 4, RPS: 3431.8, total tasks done: 17159, duration: 5 seconds
terminals: 8, RPS: 7287.4, total tasks done: 36437, duration: 5 seconds
terminals: 16, RPS: 14774.2, total tasks done: 73871, duration: 5 seconds
terminals: 32, RPS: 29755, total tasks done: 148775, duration: 5 seconds
terminals: 64, RPS: 61789.4, total tasks done: 308947, duration: 5 seconds
terminals: 128, RPS: 124163, total tasks done: 620813, duration: 5 seconds
terminals: 256, RPS: 248629, total tasks done: 1243146, duration: 5 seconds
terminals: 512, RPS: 486853, total tasks done: 2434263, duration: 5 seconds
terminals: 1024, RPS: 968085, total tasks done: 4840425, duration: 5 seconds
terminals: 2048, RPS: 1.82756e+06, total tasks done: 9137776, duration: 5 seconds
terminals: 4096, RPS: 2.17314e+06, total tasks done: 10865713, duration: 5 seconds
terminals: 8192, RPS: 2.31093e+06, total tasks done: 11554649, duration: 5 seconds
terminals: 16384, RPS: 2.4308e+06, total tasks done: 12154005, duration: 5 seconds
terminals: 32768, RPS: 2.42015e+06, total tasks done: 12100770, duration: 5 seconds
terminals: 65536, RPS: 2.35686e+06, total tasks done: 11784278, duration: 5 seconds
```