//go:build windows && amd64

package browsercontrol

import (
	"crypto/sha256"
	_ "embed"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"sync"
	"syscall"
	"unsafe"
)

//go:embed native/BrowserControlBridge.dll
var bridge []byte
var native struct {
	sync.Once
	err                        error
	connect, call, close, free *syscall.LazyProc
}

func load() error {
	native.Do(func() {
		cache, err := os.UserCacheDir()
		if err != nil {
			native.err = err
			return
		}
		hash := sha256.Sum256(bridge)
		dir := filepath.Join(cache, "TitanCSA", "browser-control-sdk", hex.EncodeToString(hash[:]))
		if err = os.MkdirAll(dir, 0700); err != nil {
			native.err = err
			return
		}
		path := filepath.Join(dir, "BrowserControlBridge.dll")
		if data, readErr := os.ReadFile(path); readErr != nil || sha256.Sum256(data) != hash {
			file, err := os.CreateTemp(dir, "bridge-*.tmp")
			if err != nil {
				native.err = err
				return
			}
			temp := file.Name()
			defer os.Remove(temp)
			_, writeErr := file.Write(bridge)
			closeErr := file.Close()
			if writeErr != nil {
				native.err = writeErr
				return
			}
			if closeErr != nil {
				native.err = closeErr
				return
			}
			if err = os.Rename(temp, path); err != nil {
				data, readErr := os.ReadFile(path)
				if readErr != nil || sha256.Sum256(data) != hash {
					native.err = err
					return
				}
			}
		}
		data, err := os.ReadFile(path)
		if err != nil {
			native.err = err
			return
		}
		if sha256.Sum256(data) != hash {
			native.err = errors.New("native bridge checksum mismatch")
			return
		}
		dll := syscall.NewLazyDLL(path)
		if err = dll.Load(); err != nil {
			native.err = err
			return
		}
		native.connect = dll.NewProc("bcs_connect")
		native.call = dll.NewProc("bcs_call")
		native.close = dll.NewProc("bcs_close")
		native.free = dll.NewProc("bcs_free")
		for _, proc := range []*syscall.LazyProc{native.connect, native.call, native.close, native.free} {
			if err = proc.Find(); err != nil {
				native.err = err
				return
			}
		}
	})
	return native.err
}

// Error is a structured native or remote error. Code result_unknown means the
// action may have executed. Details contains the original protocol details.
type Error struct {
	Code    string          `json:"code"`
	Message string          `json:"message"`
	Details json.RawMessage `json:"details"`
}

func (e *Error) Error() string { return e.Code + ": " + e.Message }

func take(address unsafe.Pointer) (json.RawMessage, error) {
	if address == nil {
		return nil, errors.New("native SDK returned no response")
	}
	defer native.free.Call(uintptr(address))
	// Native strings are owned by the DLL and always NUL terminated.
	p := (*byte)(address)
	length := 0
	for length < 64*1024*1024+16384 {
		if *(*byte)(unsafe.Add(unsafe.Pointer(p), length)) == 0 {
			return append(json.RawMessage(nil), unsafe.Slice(p, length)...), nil
		}
		length++
	}
	return nil, errors.New("native SDK response exceeds supported bound")
}
func replyError(data json.RawMessage) error {
	var value Error
	if err := json.Unmarshal(data, &value); err != nil {
		return err
	}
	return &value
}

// Browser owns one native client. Close it when no longer needed. Calls on a
// single Browser are serialized; independent clients may run concurrently.
type Browser struct {
	mu     sync.Mutex
	handle uintptr
}

// Connect confirms a running instance. Empty endpoint discovers an instance for
// the current Windows user. The endpoint JSON contains a private token.
func Connect(endpoint string) (*Browser, error) {
	if err := load(); err != nil {
		return nil, err
	}
	var path *byte
	var err error
	if endpoint != "" {
		path, err = syscall.BytePtrFromString(endpoint)
		if err != nil {
			return nil, err
		}
	}
	var out unsafe.Pointer
	handle, _, _ := native.connect.Call(uintptr(unsafe.Pointer(path)), uintptr(unsafe.Pointer(&out)))
	runtime.KeepAlive(path)
	if handle == 0 {
		data, err := take(out)
		if err != nil {
			return nil, err
		}
		return nil, replyError(data)
	}
	return &Browser{handle: handle}, nil
}

// Call invokes any protocol method. Params must be a JSON object or nil.
// Timeout is in milliseconds (1..120000). Requests are never automatically retried.
// Results use json.RawMessage to preserve large JSON integers exactly.
func (b *Browser) Call(method string, params any, timeoutMS uint32) (json.RawMessage, error) {
	if timeoutMS < 1 || timeoutMS > 120000 || method == "" {
		return nil, errors.New("use a nonempty method and timeout 1..120000")
	}
	if params == nil {
		params = map[string]any{}
	}
	encoded, err := json.Marshal(params)
	if err != nil {
		return nil, err
	}
	if len(encoded) == 0 || encoded[0] != '{' {
		return nil, errors.New("parameters must be a JSON object")
	}
	name, err := syscall.BytePtrFromString(method)
	if err != nil {
		return nil, err
	}
	body, err := syscall.BytePtrFromString(string(encoded))
	if err != nil {
		return nil, err
	}
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.handle == 0 {
		return nil, &Error{Code: "closed", Message: "browser client is closed"}
	}
	var out unsafe.Pointer
	status, _, _ := native.call.Call(b.handle, uintptr(unsafe.Pointer(name)), uintptr(unsafe.Pointer(body)), uintptr(timeoutMS), uintptr(unsafe.Pointer(&out)))
	runtime.KeepAlive(name)
	runtime.KeepAlive(body)
	data, err := take(out)
	if err != nil {
		return nil, err
	}
	if status != 0 {
		return nil, replyError(data)
	}
	if !json.Valid(data) {
		return nil, fmt.Errorf("invalid native JSON reply")
	}
	return data, nil
}
func (b *Browser) Info() (json.RawMessage, error) { return b.Call("browser.info", nil, 15000) }
func (b *Browser) Tabs() (json.RawMessage, error) { return b.Call("tabs.list", nil, 15000) }

// Close is idempotent and waits for this client's in-flight call to complete.
func (b *Browser) Close() error {
	b.mu.Lock()
	defer b.mu.Unlock()
	if b.handle != 0 {
		native.close.Call(b.handle)
		b.handle = 0
	}
	return nil
}
