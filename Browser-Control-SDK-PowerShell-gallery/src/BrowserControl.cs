using System.Buffers.Binary;
using System.IO.Pipes;
using System.Text;
using System.Text.Json;

namespace TitanCSA.BrowserControl.PowerShell;

public sealed class BrowserException(string code, string message, string? detailsJson = null) : Exception(message) {
    public string Code { get; } = code;
    public string? DetailsJson { get; } = detailsJson;
}

public sealed class BrowserClient {
    private readonly string pipe, token;
    public string EndpointPath { get; }
    private BrowserClient(string filename) {
        EndpointPath = Path.GetFullPath(filename);
        try {
            var file = new FileInfo(EndpointPath);
            if (!file.Exists || file.Length > 16384) throw new InvalidDataException();
            using var document = JsonDocument.Parse(File.ReadAllText(EndpointPath,Encoding.UTF8));
            var endpoint = document.RootElement;
            if (endpoint.GetProperty("version").GetInt32() != 1) throw new InvalidDataException();
            pipe = endpoint.GetProperty("pipe").GetString() ?? "";
            token = endpoint.GetProperty("token").GetString() ?? "";
            if (!pipe.StartsWith(@"\\.\pipe\BrowserControl-",StringComparison.Ordinal) || pipe.Length > 240 || token.Length is <16 or >1024) throw new InvalidDataException();
        } catch { throw new BrowserException("invalid_endpoint","The private browser endpoint file is missing or invalid"); }
    }
    private static void Timeout(int milliseconds) {
        if (milliseconds is <1 or >120000) throw new ArgumentOutOfRangeException(nameof(milliseconds),"Timeout must be between 1 and 120000 ms");
    }
    public static BrowserClient Connect(string? endpointPath = null,int timeoutMs = 1500) => ConnectAsync(endpointPath,timeoutMs).GetAwaiter().GetResult();
    public static async Task<BrowserClient> ConnectAsync(string? endpointPath = null,int timeoutMs = 1500) {
        if (!OperatingSystem.IsWindows()) throw new PlatformNotSupportedException("BrowserControl requires Windows");
        Timeout(timeoutMs);
        if (endpointPath != null) {
            var client = new BrowserClient(endpointPath);
            await client.CallAsync("browser.info","{}",timeoutMs).ConfigureAwait(false);
            return client;
        }
        var folder = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),"TitanCSA","BrowserControl","instances");
        var candidates = Directory.Exists(folder) ? Directory.EnumerateFiles(folder,"*.json").Select(f=>new FileInfo(f)).OrderByDescending(f=>f.LastWriteTimeUtc).Take(128).Select(f=>f.FullName).ToArray() : [];
        var clock = System.Diagnostics.Stopwatch.StartNew();
        foreach (var candidate in candidates) {
            var left = timeoutMs - (int)clock.ElapsedMilliseconds;
            if (left <= 0) break;
            try { return await ConnectAsync(candidate,Math.Min(left,500)).ConfigureAwait(false); }
            catch (BrowserException) { }
        }
        throw new BrowserException("browser_not_running","Start BrowserControl.exe or supply its private endpoint path");
    }
    public string Call(string method,string parametersJson = "{}",int timeoutMs = 15000) => CallAsync(method,parametersJson,timeoutMs).GetAwaiter().GetResult();
    public async Task<string> CallAsync(string method,string parametersJson = "{}",int timeoutMs = 15000) {
        Timeout(timeoutMs);
        if (string.IsNullOrWhiteSpace(method) || method.Length>128) throw new ArgumentException("Supply a nonempty method name of at most 128 characters");
        using var parameters = JsonDocument.Parse(parametersJson);
        if (parameters.RootElement.ValueKind != JsonValueKind.Object) throw new ArgumentException("Parameters must be a JSON object");
        var id = Guid.NewGuid().ToString();
        var request = JsonSerializer.SerializeToUtf8Bytes(new { version=1,id,token,method,@params=parameters.RootElement,timeout_ms=timeoutMs });
        if (request.Length > 1024*1024) throw new BrowserException("request_too_large","The request exceeds 1 MiB");
        var frame = new byte[request.Length+4]; BinaryPrimitives.WriteInt32LittleEndian(frame,request.Length);request.CopyTo(frame.AsSpan(4));
        using var deadline = new CancellationTokenSource(timeoutMs+1000);
        using var stream = new NamedPipeClientStream(".",pipe[9..],PipeDirection.InOut,PipeOptions.Asynchronous,System.Security.Principal.TokenImpersonationLevel.Identification);
        bool wrote=false,confirmed=false;
        try {
            await stream.ConnectAsync(deadline.Token).ConfigureAwait(false);
            wrote=true;
            await stream.WriteAsync(frame,deadline.Token).ConfigureAwait(false);
            await stream.FlushAsync(deadline.Token).ConfigureAwait(false);
            var header=new byte[4];await stream.ReadExactlyAsync(header,deadline.Token).ConfigureAwait(false);
            int size=BinaryPrimitives.ReadInt32LittleEndian(header);
            if(size is <1 or >64*1024*1024) throw new BrowserException("invalid_response","Response size is outside the supported bound");
            var body=new byte[size];await stream.ReadExactlyAsync(body,deadline.Token).ConfigureAwait(false);
            using var response=JsonDocument.Parse(body);var root=response.RootElement;
            if(root.ValueKind!=JsonValueKind.Object || root.GetProperty("id").GetString()!=id) throw new BrowserException("invalid_response","The browser returned an unexpected response id");
            if(root.GetProperty("ok").ValueKind==JsonValueKind.True && root.TryGetProperty("result",out var result)) { confirmed=true;return result.GetRawText(); }
            if(root.GetProperty("ok").ValueKind==JsonValueKind.False && root.TryGetProperty("error",out var error) && error.TryGetProperty("code",out var code) && error.TryGetProperty("message",out var message) && code.ValueKind==JsonValueKind.String && message.ValueKind==JsonValueKind.String) {
                confirmed=true;throw new BrowserException(code.GetString()!,message.GetString()!,error.TryGetProperty("details",out var details)?details.GetRawText():null);
            }
            throw new BrowserException("invalid_response","The browser returned an invalid response");
        } catch (Exception error) when (!confirmed) {
            string cause=error is OperationCanceledException?"transport_timeout":error is BrowserException browser?browser.Code:"connection_failed";
            if(wrote) throw new BrowserException("result_unknown","No confirmed browser reply; inspect state before retrying the action",JsonSerializer.Serialize(new { may_have_executed=true,cause_code=cause,method }));
            throw new BrowserException(cause,"Could not communicate with the local BrowserControl instance");
        }
    }
}
