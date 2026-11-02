# Sessions and saved logins

Profiles stay local. No account information is uploaded to a project server and
no existing Chrome/Edge profile is imported.

## Human setup

1. Open the intended profile. Create one with `browser.create_profile("work")`
   and open it with `browser.new_tab(url, profile="work")` if needed.
2. Sign in normally. Persistent cookies and site storage remain with the profile,
   subject to the site's expiration and revocation rules.
3. For a password fallback, enter the username/password on a visible login form,
   then choose **Logins → Save current login** before submitting. Confirm the
   displayed origin and profile.
4. Use **Fill saved login**, or explicitly toggle automatic filling. A native
   menu selects an account when several are saved. Autofill does not submit forms.

The complete vault document is encrypted with Windows current-user DPAPI in the
app's private data folder. It is separate from cookie storage. No cloud password
sync or extra service is installed. The app disables WebView2's separate password
save prompt so there is one clear saving flow.

## AI and SDK use

Prefer an existing cookie session. When a login form appears, inspect status and
use an account authorized for the task:

```python
status = tab.login()
if status["available"]:
    result = tab.login("fill", submit=True)
    print(result)           # Metadata only, never the password.
    print(tab.observe())   # Inspect the result or complete required MFA.
```

`browser.saved_logins(profile="work", origin="https://example.org")` returns
IDs, profile, origin, username and autofill preference. Choose `credential_id=`
when several accounts match. There is no password-export or password-save RPC;
saving requires explicit native UI confirmation.

`browser.set_saved_login_autofill(id, True)` opts a login into filling compatible
forms when a document becomes ready. `browser.delete_saved_login(id)` forgets
it. Forgetting a password does not end an existing cookie session; use the site's
sign-out action or explicitly clear its cookies when intended.

MCP provides `browser_login` and `browser_saved_logins`. Its normal results never
return stored passwords. Usernames, screenshots, deliberate page evaluation and
raw cookie exports can still contain sensitive information.

## Matching and limitations

- A login is bound to its profile and exact **scheme + hostname + port**. It is
  not reused on lookalike domains, subdomains or another profile.
- Saving/filling requires HTTPS except loopback HTTP for local fixtures. Private
  profiles cannot save or fill vault credentials.
- Helpers need one visible main-frame login form. Focus the intended form when
  needed. Ambiguous forms, iframe-only login, unusual multi-step flows, OTPs,
  passkeys and MFA may require human input.
- Session cookies may disappear on restart; persistent cookies can expire or be
  revoked. A stored cookie is not a promise of permanent access.
- Corrupt or undecryptable vaults return a specific English error and are not
  silently overwritten. Moving data to another Windows installation/account may
  make DPAPI-protected credentials unreadable. This is not a portable vault.
- Protection covers data at rest under this Windows account. Same-user malicious
  code, an authorized SDK client and administrators remain outside that boundary.
  A website necessarily receives a password when you fill its form.

Some sites restrict embedded browsers. Their supported authentication rules still
apply; the app cannot guarantee login compatibility with every website.

References: [Windows DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata),
[WebView2 profile data](https://learn.microsoft.com/en-us/microsoft-edge/webview2/concepts/user-data-folder).
