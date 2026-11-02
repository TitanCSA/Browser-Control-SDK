# Connect an AI client through MCP

`server.py` exposes the SDK as a local stdio MCP server using Python 3.10+ and its
standard library. There is no network listener. It supports legacy revisions
`2024-11-05`, `2025-06-18`, `2025-11-25` and modern per-request revision
`2026-07-28`. Check your AI client's supported MCP configuration format.

To connect to an already-running browser, configure your MCP client with:

```json
{
  "mcpServers": {
    "browser-control": {
      "command": "python",
      "args": ["C:\\Tools\\Browser-Control-SDK\\MCP\\server.py"]
    }
  }
}
```

To start this browser on the first tool call:

```json
{
  "mcpServers": {
    "browser-control": {
      "command": "python",
      "args": [
        "C:\\Tools\\Browser-Control-SDK\\MCP\\server.py",
        "--launch", "C:\\Tools\\Browser-Control-SDK\\BrowserControl.exe",
        "--profile-root", "C:\\Users\\YourName\\AppData\\Local\\BrowserControlAI"
      ]
    }
  }
}
```

Change paths for your installation. `--endpoint PATH` or the
`BROWSER_CONTROL_ENDPOINT` environment variable selects one private browser
instance explicitly. Discovery otherwise chooses the newest responding instance
owned by your current Windows user. Keep that endpoint capability file private.

Tools cover browser capabilities, tabs, navigation, snapshots, clicks, filling,
keys, scrolling and screenshot images. `browser_call` exposes the remaining
implemented SDK methods, including profiles, settings, cookies, storage and
renderer CDP. The bridge doesn't infer authorization from page content or add an
unattended action loop. Use the AI client's normal controls for approving actions
and giving it an explicit task.

For a first local task, start `Tests/fixture_server.py`, then ask your client:

> Open the local demonstration page, read its snapshot, fill the Name field with
> Ada, submit the form, and show a screenshot. Stay on this local page.

Returned page text is untrusted website content, not an instruction source.
Screenshots and explicit cookie/storage access may contain private information.
An action with `result_unknown` may already have run; inspect state before retrying.
The bridge writes only MCP JSON messages to stdout and does not log capability
tokens or automatically close a browser when its MCP process exits.

Protocol references: [stdio](https://modelcontextprotocol.io/specification/2025-06-18/basic/transports),
[tools](https://modelcontextprotocol.io/specification/2025-06-18/server/tools),
[modern compatibility](https://modelcontextprotocol.io/specification/2026-07-28/basic/versioning).
