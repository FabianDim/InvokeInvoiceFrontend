#pragma once
#include "Domain/Business/Clients/CustomerDetails.h"
#include <QJsonObject>
#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>
#include <stdexcept>

namespace Domain::Clients {
// Empty terms and absent stock prices inherit the current business defaults.
inline QJsonObject preferences(const QJsonObject& data) {
    QJsonObject result;
    if (data.contains("CustomerDetails")) result["CustomerDetails"] = customer_details(data.value("CustomerDetails"));
    if (data.contains("ContactName")) {
        if (!data.value("ContactName").isString() || data.value("ContactName").toString().size() > 200)
            throw std::invalid_argument("Enter a contact name of at most 200 characters.");
        result["ContactName"] = data.value("ContactName").toString().trimmed();
    }
    if (data.contains("PaymentTerms")) {
        const auto value = data.value("PaymentTerms");
        if (!value.isString() || (value != "" && value != "COD" && value != "14" && value != "30"))
            throw std::invalid_argument("Choose business default, COD, 14 or 30 days for client payment terms.");
        result["PaymentTerms"] = value;
    }
    if (data.contains("StockPrices")) {
        if (!data.value("StockPrices").isObject())
            throw std::invalid_argument("Client stock prices must be a set of stock item prices.");
        const auto prices = data.value("StockPrices").toObject();
        if (prices.size() > 1000) throw std::invalid_argument("Enter at most 1,000 client stock prices.");
        static const QRegularExpression id("^(?:[a-fA-F0-9]{24}|DEMO-[0-9]+)$");
        for (auto it = prices.begin(); it != prices.end(); ++it)
            if (!id.match(it.key()).hasMatch() || !it.value().isDouble() ||
                !std::isfinite(it.value().toDouble()) || it.value().toDouble() < 0 || it.value().toDouble() > 1e9)
                throw std::invalid_argument("Enter valid client prices between 0 and 1,000,000,000.");
        result["StockPrices"] = prices;
    }
    return result;
}
inline QString payment_terms(const QJsonObject& client, const QString& business_default) {
    const auto terms = client.value("PaymentTerms").toString();
    return terms.isEmpty() ? business_default : terms;
}
inline double stock_price(const QJsonObject& client, const QJsonObject& stock) {
    return client.value("StockPrices").toObject().value(stock.value("_id").toString()).toDouble(stock.value("Price").toDouble());
}
inline void validate_stock_prices(const QJsonObject& prices, const QJsonArray& stock) {
    for (auto it = prices.begin(); it != prices.end(); ++it) {
        bool found = false;
        for (const auto& item : stock)
            if (item.toObject().value("_id").toString() == it.key()) { found = true; break; }
        if (!found) throw std::invalid_argument("Choose stock belonging to this business for client prices.");
    }
}
}
