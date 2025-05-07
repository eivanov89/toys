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