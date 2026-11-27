# InvokeInvoiceFrontend

Public C++20 / Qt 6 desktop frontend and API contract for Invoke Invoice.
The HTTP server, MongoDB services and production PDF engine live in the private
`FabianDim/InvokeInvoiceBackend` repository. This project builds without its source.

## Demo

Choose **Try a demo invoice**. Business, client, stock and invoice records stay in
memory. PDF export sends the invoice snapshot, including contact/payment details
and any logo, to `POST /demo/invoices/pdf`. Export needs internet access but no login.
The service does not persist demo records in an account or database. Rendering uses
a temporary directory which is removed after the request. Use sample demo details.

Failed downloads or local saves leave the existing PDF, stock and history unchanged.
Retries do not deduct stock twice. Exiting the demo clears its records; exported
PDFs remain on your computer. The demo supports up to 100 lines and an 8 MiB request.

## Build on Windows

Install Visual Studio 2022 or newer with C++ desktop tools, CMake, Ninja and Git.
In an x64 Developer PowerShell, set `VCPKG_ROOT` to a bootstrapped vcpkg checkout:

```powershell
git clone https://github.com/FabianDim/InvokeInvoiceFrontend.git
cd InvokeInvoiceFrontend
$env:VCPKG_ROOT = 'C:/dev/vcpkg'
cmake --preset x64-release
cmake --build --preset x64-release --parallel 2
& ./out/build/x64-release/InvokeInvoiceSystem.exe
```

The manifest pins dependencies. Qt is installed by vcpkg. For a standalone EXE,
use `x64-static-release`, then run:

```powershell
./.github/scripts/package-frontend.ps1 -Version 0.2.0 -BuildDirectory out/build/x64-static-release -Static
```

The executable and settings retain the `InvokeInvoiceSystem` product name.
The default API is `https://invoice-backend-app-69d26786220b.herokuapp.com`.
For a separately running local backend set
`$env:INVOICE_API_URL = 'http://127.0.0.1:1234'` before launching the app.
Other URLs must use HTTPS. The frontend never starts an embedded server and needs
no MongoDB credentials. Account requests and demo exports use the same service URL.

## Tests

```powershell
cmake --build --preset x64-release --target all-tests
ctest --test-dir out/build/x64-release -L local --output-on-failure
python -m unittest discover -s tests -p test_update_signing_keys.py -v
```

FrontendSession tests mock HTTP transport, login, demo export, retries, rate limits,
response limits and session isolation. BusinessSession tests forms and local records.
Update and Logging cover signed updates and logging. No private source or database
is needed. UI capture is opt-in. To test the real backend locally:

```powershell
$env:INVOICE_TEST_DEMO_URL = 'http://127.0.0.1:1234'
& ./out/build/x64-release/FrontendSessionTests.exe hosted_backend_roundtrip
```

## API and releases

[api/README.md](api/README.md) documents the interface; [api/openapi.json](api/openapi.json)
defines hosted demo export. Clients send `X-Invoice-API-Version: 1`. API compatibility
is independent of frontend/backend release numbers. The backend vendors a pinned
contract copy, so both repositories build independently.

Work goes through a feature PR into `Development`, then a `Development` PR into
`main`. The main ruleset requires **All available tests** and **Development PR
required**, with no bypass, force pushes or branch deletion. Development PRs run
every registered frontend CTest suite and signing tests. Main promotions reuse
successful Development PR test evidence only for the exact same Git tree; changed
files or expired evidence require a new tested Development PR.

Every push to `main` automatically builds, tests and publishes a signed Windows EXE.
The version is the manifest version plus the release workflow run number in the
patch component; reruns keep their version and never overwrite a published release.
An explicit `vMAJOR.MINOR.PATCH` tag also supports a manually selected version.
`INVOICE_UPDATE_SIGNING_KEY` is a private repository secret;
only the public trust key is committed. Deploy additive API changes before releasing
the frontend that needs them.

Updates now use `FabianDim/InvokeInvoiceFrontend` releases. Previously installed apps
still point at the old repository and need one manual installation of a new frontend
release. Once the old repository is private, its public update feed is unavailable.
Existing invoice work remains usable.

This repository starts with fresh history and contains no server/PDF implementation.
Pre-split UI edits were preserved. The original local working folder remains a backup.

## License

GNU GPL v3. See [LICENSE](LICENSE).
