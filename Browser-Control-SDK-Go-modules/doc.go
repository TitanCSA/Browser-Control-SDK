// Package browsercontrol connects to the BrowserControl Windows application.
// The native Windows x64 bridge is embedded; no C compiler or Python is required.
// Call Connect with an endpoint path, or an empty string for discovery.
// A ResultUnknown error means an action may have executed; never retry blindly.
package browsercontrol
