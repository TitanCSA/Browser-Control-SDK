require 'json'
require 'fiddle/import'
require 'thread'
module BrowserControlSDK
  VERSION = '0.2.2'
  class Error < StandardError
    attr_reader :code, :details
    def initialize(data)
      @code = data.fetch('code', 'sdk_error'); @details = data.fetch('details', {})
      super(data.fetch('message', @code))
    end
  end
  module Native
    extend Fiddle::Importer
    raise LoadError, 'BrowserControlSDK requires Windows x64' unless RUBY_PLATFORM.match?(/mswin|mingw/) && Fiddle::SIZEOF_VOIDP == 8
    dlload File.expand_path('native/BrowserControlBridge.dll', __dir__)
    extern 'unsigned long long bcs_connect(const char*, void*)'
    extern 'int bcs_call(unsigned long long, const char*, const char*, unsigned int, void*)'
    extern 'void bcs_close(unsigned long long)'
    extern 'void bcs_free(void*)'
    def self.output
      ptr = Fiddle::Pointer.malloc(Fiddle::SIZEOF_VOIDP, Fiddle::RUBY_FREE)
      ptr[0, Fiddle::SIZEOF_VOIDP] = [0].pack('J'); ptr
    end
    def self.take(out)
      address = out[0, Fiddle::SIZEOF_VOIDP].unpack1('J')
      raise Error.new('code'=>'sdk_error', 'message'=>'Native SDK returned no response') if address.zero?
      text = Fiddle::Pointer.new(address)
      begin
        JSON.parse(text.to_s.force_encoding(Encoding::UTF_8))
      ensure
        bcs_free(text)
      end
    end
  end
  class Browser
    def self.connect(endpoint: nil)
      client = new(endpoint)
      return client unless block_given?
      begin
        yield client
      ensure
        client.close
      end
    end
    def initialize(endpoint = nil)
      @mutex = Mutex.new
      raise ArgumentError, 'Endpoint must be a String or nil' unless endpoint.nil? || endpoint.is_a?(String)
      path = endpoint&.encode(Encoding::UTF_8)
      raise ArgumentError, 'Endpoint contains a NUL byte' if path&.include?("\0")
      out = Native.output; @handle = Native.bcs_connect(path, out)
      raise Error.new(Native.take(out)) if @handle.zero?
    end
    def call(method, params = {}, timeout_ms: 15_000)
      raise ArgumentError, 'Parameters must be a Hash' unless params.is_a?(Hash)
      raise ArgumentError, 'Use a nonempty method' unless method.is_a?(String) && !method.empty? && !method.include?("\0")
      raise ArgumentError, 'Timeout must be 1..120000 milliseconds' unless timeout_ms.is_a?(Integer) && (1..120_000).cover?(timeout_ms)
      @mutex.synchronize do
        raise Error.new('code'=>'closed', 'message'=>'Browser client is closed') if @handle.zero?
        out = Native.output
        code = Native.bcs_call(@handle, method.encode(Encoding::UTF_8), JSON.generate(params), timeout_ms, out)
        data = Native.take(out)
        raise Error.new(data) unless code.zero?
        data
      end
    end
    def info; call('browser.info'); end
    def tabs; call('tabs.list'); end
    def new_tab(options = {}); call('tabs.create', options); end
    def navigate(tab, url, timeout_ms: 15_000)
      call('page.navigate', {'tab'=>tab, 'url'=>url, 'wait'=>'load', 'timeout_ms'=>timeout_ms}, timeout_ms: timeout_ms)
    end
    def close
      @mutex.synchronize { Native.bcs_close(@handle) unless @handle.zero?; @handle = 0 }
      nil
    end
  end
end
