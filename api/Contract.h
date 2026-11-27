#pragma once
namespace InvoiceApi {
inline constexpr char version[] = "1";
inline constexpr char demo_pdf_path[] = "/demo/invoices/pdf";
inline constexpr int demo_body_limit = 8 * 1024 * 1024;
inline constexpr int demo_pdf_limit = 16 * 1024 * 1024;
inline constexpr int demo_item_limit = 100;
}
