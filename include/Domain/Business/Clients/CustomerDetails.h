#pragma once
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUrl>
#include <stdexcept>

namespace Domain::Clients {
// Additive customer metadata. Legacy invoice-facing fields remain Name,
// ContactName, Email, Phone and Address; older clients can omit CustomerDetails.
inline QJsonObject customer_details(const QJsonValue& value) {
    if (!value.isObject()) throw std::invalid_argument("Customer details must be an object.");
    const auto source = value.toObject();
    QJsonObject result;
    const auto text = [](const QJsonObject& object, const QString& key, int limit = 200) {
        const auto v = object.value(key);
        if (v.isUndefined()) return QString{};
        if (!v.isString() || v.toString().size() > limit)
            throw std::invalid_argument(QString("%1 must be text of at most %2 characters.").arg(key).arg(limit).toStdString());
        return v.toString().trimmed();
    };
    const auto email = [](const QString& address) {
        static const QRegularExpression pattern(R"(^[^\s@]+@[^\s@]+\.[^\s@]+$)");
        if (!address.isEmpty() && !pattern.match(address).hasMatch())
            throw std::invalid_argument("Enter a valid email address for each contact, or leave it blank.");
    };
    const auto type = text(source, "Type");
    if (type != "Business" && type != "Individual")
        throw std::invalid_argument("Choose Business or Individual for the customer type.");
    result["Type"] = type;
    for (const auto* key : {"Salutation", "FirstName", "LastName", "CompanyName", "Mobile"})
        result[key] = text(source, key);
    const auto website = text(source, "Website", 2000);
    if (!website.isEmpty()) {
        const auto url = QUrl::fromUserInput(website);
        if (!url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() ||
            (url.scheme() != "https" && url.scheme() != "http"))
            throw std::invalid_argument("Enter a valid customer website, such as https://example.com.");
        result["Website"] = url.toString();
    } else result["Website"] = "";
    result["Remarks"] = text(source, "Remarks", 10000);
    for (const auto* key : {"BillingAddress", "ShippingAddress"}) {
        if (source.contains(key) && !source.value(key).isObject())
            throw std::invalid_argument("Customer addresses must contain named address fields.");
        const auto address = source.value(key).toObject();
        QJsonObject cleaned;
        for (const auto* field : {"Attention", "Country", "Street1", "Street2", "City", "State", "Postcode", "Phone", "Fax"})
            cleaned[field] = text(address, field, QString(field).startsWith("Street") ? 2000 : 200);
        result[key] = cleaned;
    }
    if (source.contains("Contacts") && (!source.value("Contacts").isArray() || source.value("Contacts").toArray().size() > 20))
        throw std::invalid_argument("Add at most 20 contact persons.");
    QJsonArray contacts;
    for (const auto& entry : source.value("Contacts").toArray()) {
        if (!entry.isObject()) throw std::invalid_argument("Each contact person must contain named fields.");
        QJsonObject row;
        bool populated = false;
        for (const auto* field : {"Salutation", "FirstName", "LastName", "Email", "Phone", "Mobile"}) {
            const auto content = text(entry.toObject(), field);
            row[field] = content; populated |= !content.isEmpty();
        }
        if (!populated) continue;
        email(row.value("Email").toString());
        contacts.append(row);
    }
    result["Contacts"] = contacts;
    return result;
}
inline QString customer_billing_address(const QJsonObject& details) {
    const auto address = details.value("BillingAddress").toObject();
    QStringList parts;
    for (const auto* key : {"Attention", "Street1", "Street2", "City", "State", "Postcode", "Country"}) {
        const auto value = address.value(key).toString().trimmed();
        if (!value.isEmpty()) parts.append(value);
    }
    return parts.join(", ");
}
}
