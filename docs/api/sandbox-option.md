# `sandbox` Option

> Create a browser window with a sandboxed renderer. With this
option enabled, the renderer must communicate via IPC to the main process in order to access node APIs.

One of the key security features of Chromium is that all code related to rendering
web content is executed within a sandbox. This sandbox uses OS-specific features
to ensure that exploits in the renderer process cannot harm the system. The
sandbox is Chromium's last line of defense in ensuring that web content cannot
access confidential information or make persistent changes to a user's machine.

In other words, when the sandbox is enabled, the renderers can only make changes
to the system by delegating tasks to the main process via IPC.
Chromium [maintains detailed documentation with more information about the sandbox][sandbox-docs].
If you just want the short version, check out the [FAQ][faq].

By default, Electron disables this sandbox. It does so in the service of making
the development of desktop applications with web technologies easier, in part
by allowing developers to use Node.js from within the browser. Node.js requires
unhindered access to the operating system – a call as simple as `require()` is
not possible without file system permissions, which are not available in a
sandboxed environment.

As long as all code and content displayed by the renderer is trusted, that is
an acceptable trade-off. However, when displaying un-trusted web content – 
like user-generated images, videos, or whole web-sites that you do not control
entirely – you should enable the sandbox.

A sandboxed renderer doesn't have a Node.js environment running and doesn't
expose Node.js JavaScript APIs to client code. The only exception is the
preload script, which has access to a subset of Electron's `renderer` APIs.

Another difference is that sandboxed renderers don't modify any of the default
JavaScript APIs. Consequently, some APIs such as `window.open` will work as they
do in Chromium (i.e. they do not return a [`BrowserWindowProxy`](browser-window-proxy.md)).

## Example

To create a sandboxed window, pass `sandbox: true` to `webPreferences`:

```js
let win

app.on('ready', () => {
  win = new BrowserWindow({
    webPreferences: {
      sandbox: true
    }
  })
  win.loadURL('http://google.com')
})
```

In the above code, the [`BrowserWindow`](browser-window.md) that was created
has Node.js disabled and can communicate only via IPC. The use of this option
stops Electron from creating a Node.js runtime in the renderer. Also, within
this new window, `window.open` follows the native behavior (by default Electron creates a [`BrowserWindow`](browser-window.md)
and returns a proxy to this via `window.open`).

[`app.enableSandbox`](app.md#appenablesandbox-experimental) can be used to force `sandbox: true` for all `BrowserWindow` instances.

```js
let win

app.enableSandbox()
app.on('ready', () => {
  // no need to pass `sandbox: true` since `app.enableSandbox()` was called.
  win = new BrowserWindow()
  win.loadURL('http://google.com')
})
```

## Preload

Even when the sandbox is enabled, an app can make customizations to sandboxed
renderers with the help of a `preload` script. Here's an example:

```js
let win

app.on('ready', () => {
  win = new BrowserWindow({
    webPreferences: {
      sandbox: true,
      preload: path.join(app.getAppPath(), 'preload.js')
    }
  })
  win.loadURL('http://google.com')
})
```

Let's take a look at the `preload.js` we're loading in the example above:

```js
// This file is loaded whenever a JavaScript context is created. It runs in a
// private scope that can access a subset of Electron's `renderer` APIs.
// We must be careful to not leak any objects into the global scope!
const { ipcRenderer, remote } = require('electron')
const fs = remote.require('fs')

// read a configuration file using the `fs` module
const buf = fs.readFileSync('allowed-popup-urls.json')
const allowedUrls = JSON.parse(buf.toString('utf8'))

const defaultWindowOpen = window.open

function customWindowOpen (url, ...args) {
  if (allowedUrls.indexOf(url) === -1) {
    ipcRenderer.sendSync('blocked-popup-notification', location.origin, url)
    return null
  }
  return defaultWindowOpen(url, ...args)
}

window.open = customWindowOpen
```

Important things to notice in the `preload` script:

- Even though the sandboxed renderer doesn't have Node.js running, it still has
  access to a limited Node.js-like environment: `Buffer`, `process`, `setImmediate`,
  `clearImmediate` and `require` are available.
- The preload script can indirectly access all APIs from the main process through the
  `remote` and `ipcRenderer` modules.
- The preload script must be contained in a single script, but it is possible to have
  complex preload code composed with multiple modules by using a tool like
  `webpack` or `browserify`. An example of using browserify is below.

To create a `browserify` bundle and use it as a preload script, something like
the following should be used:

```sh
  browserify preload/index.js \
    -x electron \
    --insert-global-vars=__filename,__dirname -o preload.js
```

The `-x` flag should be used with any required module that is already exposed in
the preload scope, and tells browserify to use the enclosing `require` function
for it. `--insert-global-vars` will ensure that `process`, `Buffer` and
`setImmediate` are also taken from the enclosing scope(normally browserify
injects code for those).

Currently the `require` function provided in the preload scope exposes the
following modules:

- `electron`
  - `crashReporter`
  - `desktopCapturer`
  - `ipcRenderer`
  - `nativeImage`
  - `remote`
  - `webFrame`
- `events`
- `timers`
- `url`

More may be added as needed to expose more Electron APIs in the sandbox, but any
module in the main process can already be used through
`electron.remote.require`.

## Status

Please use the `sandbox` option with care, as it is still an experimental
feature. We are still not aware of the security implications of exposing some
Electron renderer APIs to the preload script, but here are some things to
consider before rendering un-trusted content:

- A preload script can accidentally leak privileged APIs to un-trusted code,
  unless [`contextIsolation`][contextIsolation] is also enabled.
- A bug in the V8 engine may allow malicious code to access the renderer preload
  APIs, effectively granting full access to the system through the `remote`
  module. Therefore, it is highly recommended to
  [disable the `remote` module](../tutorial/security.md#15-disable-the-remote-module).
  If disabling is not feasible, you should selectively
  [filter the `remote` module](../tutorial/security.md#16-filter-the-remote-module).

Since rendering un-trusted content in Electron is still uncharted territory,
the APIs exposed to the sandbox preload script should be considered more
unstable than the rest of Electron APIs, and may have breaking changes to fix
security issues.

[sandbox-docs]: https://chromium.googlesource.com/chromium/src/+/master/docs/design/sandbox.md
[faq]: https://chromium.googlesource.com/chromium/src/+/master/docs/design/sandbox_faq.md
[contextIsolation]: ../tutorial/security.md#3-enable-context-isolation-for-remote-content
