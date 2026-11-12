Gem::Specification.new do |s|
  s.name = 'browser-control-sdk'
  s.version = '0.2.1'
  s.summary = 'Direct Windows named-pipe SDK for BrowserControl'
  s.description = 'Ruby bindings to the native BrowserControl SDK. All protocol methods, bounded transport, UTF-8 JSON and structured errors. Windows x64 only; no Python dependency.'
  s.authors = ['TitanCSA']
  s.license = 'MIT'
  s.homepage = 'https://github.com/TitanCSA/Browser-Control-SDK'
  s.metadata = {'source_code_uri'=>'https://github.com/TitanCSA/Browser-Control-SDK/tree/main/Browser-Control-SDK-Ruby-rubygems', 'documentation_uri'=>'https://github.com/TitanCSA/Browser-Control-SDK/tree/main/Browser-Control-SDK-Ruby-rubygems/docs', 'rubygems_mfa_required'=>'true'}
  s.required_ruby_version = '>= 3.1'
  s.add_runtime_dependency 'fiddle', '>= 1.1', '< 2'
  s.files = Dir['lib/**/*', 'docs/**/*', 'examples/**/*', 'LICENSE', 'README.md']
  s.require_paths = ['lib']
end
