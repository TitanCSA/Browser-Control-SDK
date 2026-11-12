# Synthetic protocol fixture

Run `node protocol-fixture.mjs endpoint.json`. After READY, pass the generated endpoint file to the Ruby or Dart protocol test, or set BROWSER_SDK_TEST_ENDPOINT before `go test ./...` in the Go SDK directory. The fixture starts no browser or driver and accepts only its randomly generated token. Stop it by sending any line on stdin. Do not commit endpoint.json.
