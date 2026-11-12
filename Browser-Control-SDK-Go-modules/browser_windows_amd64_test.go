package browsercontrol

import (
	"encoding/json"
	"errors"
	"os"
	"strings"
	"sync"
	"testing"
)

func TestPrivatePipeProtocol(t *testing.T) {
	endpoint := os.Getenv("BROWSER_SDK_TEST_ENDPOINT")
	if endpoint == "" {
		t.Skip("set BROWSER_SDK_TEST_ENDPOINT to a synthetic server endpoint")
	}
	browser, err := Connect(endpoint)
	if err != nil {
		t.Fatal(err)
	}
	defer browser.Close()
	info, err := browser.Info()
	if err != nil || !strings.Contains(string(info), "café") {
		t.Fatalf("unicode: %s %v", info, err)
	}
	result, err := browser.Call("fixture.uint64", nil, 1000)
	if err != nil || string(result) != "18446744073709551615" {
		t.Fatalf("uint64: %s %v", result, err)
	}
	tab, err := browser.Call("tabs.create", map[string]any{"url": "about:blank"}, 1000)
	if err != nil || string(tab) != "{\"id\":\"test-tab\"}" {
		t.Fatalf("tab: %s %v", tab, err)
	}
	result, err = browser.Call("page.navigate", map[string]any{"tab": "test-tab", "url": "https://example.com/é"}, 1000)
	if err != nil || !strings.Contains(string(result), "example.com/é") {
		t.Fatalf("navigation: %s %v", result, err)
	}
	_, err = browser.Call("fixture.error", nil, 1000)
	var remote *Error
	if !errors.As(err, &remote) || remote.Code != "test_error" || string(remote.Details) != "{\"sample\":1}" {
		t.Fatalf("remote error: %v", err)
	}
	for _, method := range []string{"fixture.drop", "fixture.bad_id", "fixture.hang", "fixture.oversize"} {
		_, err = browser.Call(method, nil, 10)
		if !errors.As(err, &remote) || remote.Code != "result_unknown" {
			t.Fatalf("%s: %v", method, err)
		}
	}
	count, err := browser.Call("fixture.count", nil, 1000)
	if err != nil || string(count) != "1" {
		t.Fatalf("unsafe retry: %s %v", count, err)
	}
	if _, err = browser.Call("fixture.echo", []string{"invalid"}, 1000); err == nil {
		t.Fatal("array accepted as object")
	}
	_, err = browser.Call("fixture.echo", map[string]any{"large": strings.Repeat("x", 1024*1024)}, 1000)
	if !errors.As(err, &remote) || remote.Code != "request_too_large" {
		t.Fatalf("large request: %v", err)
	}
	var wg sync.WaitGroup
	for i := 0; i < 8; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			data, err := browser.Info()
			if err != nil || !json.Valid(data) {
				t.Errorf("concurrent call: %v", err)
			}
		}()
	}
	wg.Wait()
	browser.Close()
	browser.Close()
	_, err = browser.Info()
	if !errors.As(err, &remote) || remote.Code != "closed" {
		t.Fatalf("closed client: %v", err)
	}
}
