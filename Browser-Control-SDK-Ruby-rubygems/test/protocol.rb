require 'browser_control_sdk'
def check(value, message)
  raise message unless value
end
client = BrowserControlSDK::Browser.connect(endpoint: ARGV.fetch(0))
begin
  check(client.info.fetch('title') == 'café', 'UTF-8 or fragmented reply failed')
  tab = client.new_tab('url'=>'about:blank')
  check(tab.fetch('id') == 'test-tab', 'Tab creation failed')
  reply = client.navigate(tab.fetch('id'), 'https://example.com/é')
  check(reply.fetch('parameters').fetch('url').end_with?('/é'), 'Unicode request failed')
  nested = {'nested'=>{'list'=>[1, true, nil, 'é']}, 'counter'=>9007199254740991}
  check(client.call('fixture.echo', nested).fetch('parameters') == nested, 'Nested JSON lost')
  check(client.call('fixture.uint64') == 18446744073709551615, 'uint64 response lost')
  begin
    client.call('fixture.error')
    raise 'Expected remote error'
  rescue BrowserControlSDK::Error => e
    check(e.code == 'test_error' && e.details == {'sample'=>1}, 'Remote error details lost')
  end
  ['fixture.drop', 'fixture.bad_id'].each do |method|
    begin
      client.call(method)
      raise 'Expected uncertain result'
    rescue BrowserControlSDK::Error => e
      check(e.code == 'result_unknown', 'Uncertain reply was misclassified')
    end
  end
  check(client.call('fixture.count') == 1, 'An unsafe request was retried')
  ['fixture.hang', 'fixture.oversize'].each do |method|
    begin
      client.call(method, {}, timeout_ms: 10)
      raise 'Expected bounded transport failure'
    rescue BrowserControlSDK::Error => e
      check(e.code == 'result_unknown', 'Timeout or oversized reply was misclassified')
    end
  end
  begin
    client.call('fixture.echo', {'large'=>'x' * (1024 * 1024)})
    raise 'Oversized request accepted'
  rescue BrowserControlSDK::Error => e
    check(e.code == 'request_too_large', 'Request size error was misclassified')
  end
  begin
    client.call('fixture.echo', [])
    raise 'Non-object parameters accepted'
  rescue ArgumentError
  end
  begin
    client.call('fixture.echo', {}, timeout_ms: 0)
    raise 'Invalid timeout accepted'
  rescue ArgumentError
  end
  client.close
  client.close
  begin
    client.info
    raise 'Closed client accepted'
  rescue BrowserControlSDK::Error => e
    check(e.code == 'closed', 'Closed client not detected')
  end
ensure
  client.close
end
puts 'Ruby native Browser SDK: fragmented transport, UTF-8, uint64, tab operations, errors, no retries, close passed'
