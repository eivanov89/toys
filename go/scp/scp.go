package main

import (
    "encoding/binary"
    "fmt"
    "flag"
    "io"
    "net"
    "os"
    "strings"
    "sync"
    "time"
)

//------------------------------------------------------------------------------

const CHUNK_SIZE = 1024
const CHUNK_DEADLINE = time.Second
const DESC_PATH_DEADLINE = time.Second
const RETRY_DELAY = 100 * time.Millisecond

const RETRY = -2

//------------------------------------------------------------------------------

func server(host string) {
    server, err := net.Listen("tcp", host)
    if err != nil {
        fmt.Println("Error listening:", err)
        os.Exit(1)
    }
    defer server.Close()
    fmt.Println("Server is listening on", host)

    for {
        connection, err := server.Accept()
        if err != nil {
            fmt.Println("Failed to accept connection:", err)
            os.Exit(1)
        }
        go receive_file(connection)
    }
}

//------------------------------------------------------------------------------

func receive_file(connection net.Conn) {
    fmt.Println("A client has connected")
    defer connection.Close()
    connection.SetDeadline(time.Now().Add(DESC_PATH_DEADLINE))

    var desc [2]int64
    err := binary.Read(connection, binary.LittleEndian, &desc)
    if err != nil {
        fmt.Println("Failed to read file name length and append mode:", err)
        return
    }

    append_mode := desc[0] == 1
    fname_len := desc[1]

    fname_buf := make([]byte, fname_len)
    _, err = connection.Read(fname_buf)
    if err != nil {
        fmt.Println("Failed to read file name:", err)
        return
    }
    fname := string(fname_buf)

    flags := os.O_CREATE|os.O_WRONLY
    file, err := os.OpenFile(fname, flags, 0600)
    if err != nil {
        fmt.Println("Failed to open/create file:", err)
        binary.Write(connection, binary.LittleEndian, int64(-1))
        return
    }
    defer file.Close()

    var offset int64 = 0
    if append_mode {
        offset, err = file.Seek(0, os.SEEK_END)
        if err != nil {
            fmt.Println("Failed to seek file to the end:", err)
            binary.Write(connection, binary.LittleEndian, int64(-1))
            return
        }
    } else {
        err = file.Truncate(0)
        if err != nil {
            fmt.Println("Failed to truncate file:", err)
            binary.Write(connection, binary.LittleEndian, int64(-1))
            return
        }
    }
    binary.Write(connection, binary.LittleEndian, offset)

    fmt.Println("Receiving:", fname, ", offset=", offset)
    buffer := make([]byte, CHUNK_SIZE * 10)
    for {
        connection.SetDeadline(time.Now().Add(CHUNK_DEADLINE))
        n, err := connection.Read(buffer)
        if (err != nil) {
            break
        }
        _, err = file.Write(buffer[:n])
        if (err != nil) {
            /* Our simple protocol do not allow to tell client that
             * write operation failed, but since we have received the
             * data client thinks we have written this chunk to file and
             * advances read position.
             * Since it is a naive implementation we assume that FS ops
             * are always OK and don't fix this issue.
             */
            fmt.Println("Failed to write file:", err)
            os.Exit(1)
            break
        }
    }
}

//------------------------------------------------------------------------------

func client(files []string) {
    if len(files) < 2 {
        fmt.Println("Specify one src file and at least one dst")
        return
    }

    var wg sync.WaitGroup
    for _, file := range files[1:] {
        wg.Add(1)
        go func(src, dst string) {
            defer wg.Done()
            send_file(src, dst)
        }(files[0], file)
    }
    wg.Wait()
}

//------------------------------------------------------------------------------

func send_file(src, dst string) {
    rc := do_send_file(src, dst, true)
    if rc == RETRY {
        for rc != 0 {
            /* Some chunk failed, continue sending file.
             * Now ignore all errors, e.g. dial can fail for some time
             * while remote host is restarting its network
             */
            rc = do_send_file(src, dst, false)
        }
    }
    if rc == 0 {
        fmt.Println(dst, "OK")
    }
}

//------------------------------------------------------------------------------

// Return 0 if succeeded, RETRY if some chunk has been sent, -1 in case of error
func do_send_file(src, dst string, first_time bool) int {
    var append_mode int64 = 0
    if !first_time {
        /* Consider situation when
         * 1. Server read some data, but didn't write to file
         * 2. Client discovers network error and reconnects
         * 3. Now on server side write to file can happen from two
         *    goroutines.
         * This sleep is hack to avoid such scenario as much as possible
         * without making protocol more complicated. Also even without
         * sleep such scenario is doubtful because of deadlines.
         */
        time.Sleep(RETRY_DELAY)
        append_mode = 1
    }

    file, err := os.Open(src)
    if err != nil {
        fmt.Println("Failed to open", src)
        return -1
    }
    defer file.Close()

    pos := strings.Index(dst, "/")
    host := dst[:pos]
    path := dst[pos:]

    connection, err := net.DialTimeout("tcp", host, time.Second)
    if err != nil {
        fmt.Println("Failed to connect to", host, ":", err)
        return -1
    }
    defer connection.Close()

    connection.SetDeadline(time.Now().Add(DESC_PATH_DEADLINE))

    var fname_len int64 = int64(len(path))
    desc := []int64 { append_mode, fname_len }
    err = binary.Write(connection, binary.LittleEndian, desc)
    if err != nil {
        fmt.Println("Failed to send dst name length and append mode:", err)
        return -1
    }
    _, err = connection.Write([]byte(path))
    if err != nil {
        fmt.Println("Failed to send dst path:", err)
        return -1
    }

    var offset int64
    err = binary.Read(connection, binary.LittleEndian, &offset)
    if err != nil {
        fmt.Println("err:", err)
        return -1
    }
    if offset == -1 {
        fmt.Println("Failed to transfer", src, "to", dst, ": server side error")
        return -1
    }
    file.Seek(offset, io.SeekStart)

    EOF_reached := false
    buffer := make([]byte, CHUNK_SIZE)
    for {
        n, err := file.Read(buffer)
        if (err == io.EOF) {
            EOF_reached = true
            break;
        }
        if (err != nil) {
            fmt.Println("Failed to read file", src)
            return -1
        }
        connection.SetDeadline(time.Now().Add(CHUNK_DEADLINE))
        _, err = connection.Write(buffer[:n])
        if err != nil {
            file.Seek(int64(-n), os.SEEK_CUR)
            break
        }
    }
    if EOF_reached {
        return 0
    }
    return RETRY
}

//------------------------------------------------------------------------------

func main() {
    server_mode := flag.Bool("s", false, "server mode")
    server_iface := flag.String("host", "localhost:12345", "listen on specified interface/port")

    flag.Usage = func() {
        fmt.Fprintf(os.Stderr, "usage: %s [-s] [-host host:port]\n", os.Args[0])
        flag.PrintDefaults()
    }

    flag.Parse()

    if *server_mode {
        server(*server_iface)
    } else {
        client(flag.Args())
    }
}
