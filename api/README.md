# Invoke Invoice API 1

Base URL: `https://invoice-backend-app-69d26786220b.herokuapp.com`.
Send `X-Invoice-API-Version: 1` and JSON content type for POST requests.
Unsupported versions return HTTP 400 with `code: unsupported_api_version`.
Account routes accept an absent header as legacy API 1.

## Anonymous demo export

`POST /demo/invoices/pdf` takes a version 2 snapshot as in [demo-invoice.json](demo-invoice.json).
It returns `application/pdf` and `Cache-Control: no-store`. No authentication or
database access occurs. The client owns the output filename and local save.
Do not send passwords, tokens or file paths. Saved demo invoices use this same route.

Supported fields are copied into a fresh snapshot and totals are recalculated.
Unknown fields, IDs, supplied totals and legacy flags cannot select account records
or files. Logos are embedded base64 PNG/JPEG data; remote URLs are never fetched.

Limits: 8 MiB JSON; 1–100 items; integral quantity 1–1,000,000; price 0–1,000,000,000;
16 MiB PDF; logos at most 2048 pixels per side before normalization. Names, dates and
invoice number are required. Dates use ISO `YYYY-MM-DD`; payment terms are `COD`,
`14` or `30`. Boolean and numeric fields must have their JSON types.

Errors are JSON with `error`: 400 invalid input, 413 oversized input/output, 415 wrong
content type, 429 quota exhausted with `Retry-After` seconds, 500 render failure,
503 service unavailable. Quotas are 10 requests/minute per direct peer and 60/minute
per process; counters reset on restart. Forwarded IP headers are ignored, so users
behind a reverse proxy conservatively share its peer quota. The application body
check happens after Qt receives the body; configure an ingress upload limit too.

The desktop stages stock/history changes until a valid PDF is saved atomically.
Failures leave local data unchanged. Completed invoice retries do not deduct stock
again. Changing session, business or invoice invalidates pending export results.

## Account routes

`POST /auth/login` accepts `email` and `password`, returning `ok`, `token`, and `user`
(including `_id`). Send `Authorization: Bearer <token>` for protected routes.
Tokens expire after eight hours and are revoked by `POST /auth/logout`.
`POST /auth/signup` creates an account. The desktop keeps tokens in memory.

| Method | Route | Purpose |
| --- | --- | --- |
| GET | `/` | Service version; anonymous |
| GET | `/business/list` | Session-owned businesses |
| GET | `/business/items?BusinessID=...` | Business, clients and stock |
| GET | `/stock/list?BusinessID=...` | Business stock |
| POST | `/data/business`, `/data/client`, `/data/stock` | Create owned records |
| POST | `/business/update`, `/client/update`, `/stock/update` | Edit owned records |
| POST | `/business/invoice-settings` | Invoice numbering and preferences |
| POST | `/invoices/invoice_start` | Start an invoice for an owned business/client |
| POST | `/invoices/stock-list` | Finalize, archive and return PDF bytes |
| GET | `/invoices/list?BusinessID=...&offset=0` | Archived invoices |
| POST | `/invoices/payment` | Set `Paid` using `BusinessID` and `InvoiceID` |
| GET | `/invoices/pdf?BusinessID=...&InvoiceID=...` | Regenerate an account PDF |

Account IDs are ObjectId hex strings. Demo IDs never grant access to account records.
Account stock changes and rendering remain server-authoritative. Stock shortages
return HTTP 409 with `code: stock_shortage` and an `items` array. JSON field names
are case-sensitive. The public client source describes existing account payloads.
