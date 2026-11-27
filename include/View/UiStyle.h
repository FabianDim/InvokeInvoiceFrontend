#pragma once
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVariant>
#include <QVBoxLayout>
#include <QScrollArea>
#include <QFrame>
#include <QFormLayout>
#include <QResizeEvent>
#include <QVector>
#include <QTableWidget>
#include <QHeaderView>
#include <QCheckBox>

namespace App::Views::UiStyle {
inline constexpr int field_label_width = 176;

class FormToggle : public QCheckBox {
  public:
    using QCheckBox::QCheckBox;
  protected:
    bool hitButton(const QPoint& point) const override { return rect().contains(point); }
};

inline void fit_table(QTableWidget* table, int maximum_rows = 10) {
    table->ensurePolished();
    table->verticalHeader()->setDefaultSectionSize(36);
    const int rows = qBound(1, table->rowCount(), maximum_rows);
    const int header = qMax(40, table->horizontalHeader()->sizeHint().height());
    // Reserve a scrollbar gutter so horizontal scrolling never covers a row.
    table->setFixedHeight(header + rows * 36 + 14);
}

// Separate form sections use the same label column, including nested widgets.
inline void form(QFormLayout* layout, bool stacked = false) {
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(16);
    layout->setVerticalSpacing(10);
    layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    layout->setFormAlignment(Qt::AlignTop | Qt::AlignLeft);
    layout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    layout->setRowWrapPolicy(stacked ? QFormLayout::WrapAllRows : QFormLayout::DontWrapRows);
    for (int row = 0; row < layout->rowCount(); ++row) {
        auto* item = layout->itemAt(row, QFormLayout::LabelRole);
        auto* label = item ? qobject_cast<QLabel*>(item->widget()) : nullptr;
        if (!label) continue;
        label->setProperty("role", "field");
        label->setWordWrap(true);
        label->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        if (!stacked) label->setFixedWidth(field_label_width);
    }
}

// Action rows wrap as a group instead of forcing the whole page wider or
// truncating button captions on a small window or a high-DPI display.
class ActionRow : public QWidget {
  public:
    explicit ActionRow(QWidget* parent = nullptr) : QWidget(parent), row_(new QBoxLayout(QBoxLayout::LeftToRight, this)) {
        row_->setContentsMargins(0, 0, 0, 0);
        row_->setSpacing(10);
        row_->setSizeConstraint(QLayout::SetNoConstraint);
        auto policy = QSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        policy.setHeightForWidth(true);
        setSizePolicy(policy);
    }
    void addWidget(QPushButton* button, int stretch = 1) {
        buttons_.append(button);
        row_->addWidget(button, stretch);
    }
    QSize sizeHint() const override { return {preferred_width(), row_height()}; }
    QSize minimumSizeHint() const override { return {0, row_height()}; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override {
        if (width >= preferred_width()) return row_height();
        int height = 0, count = 0;
        for (const auto* button : buttons_) if (!button->isHidden()) { height += button->sizeHint().height(); ++count; }
        return height + qMax(0, count - 1) * row_->spacing();
    }
  protected:
    bool event(QEvent* event) override {
        const auto handled = QWidget::event(event);
        if (event->type() == QEvent::LayoutRequest) update_layout();
        return handled;
    }
    void resizeEvent(QResizeEvent* event) override {
        QWidget::resizeEvent(event);
        update_layout();
    }
  private:
    void update_layout() {
        row_->setDirection(width() < preferred_width() ? QBoxLayout::TopToBottom : QBoxLayout::LeftToRight);
        const auto height = heightForWidth(width());
        if (minimumHeight() != height || maximumHeight() != height) setFixedHeight(height);
    }
    int preferred_width() const {
        int width = 0, count = 0;
        for (const auto* button : buttons_) if (!button->isHidden()) { width += button->sizeHint().width(); ++count; }
        return width + qMax(0, count - 1) * row_->spacing();
    }
    int row_height() const {
        int height = 0;
        for (const auto* button : buttons_) if (!button->isHidden()) height = qMax(height, button->sizeHint().height());
        return height;
    }
    QBoxLayout* row_;
    QVector<QPushButton*> buttons_;
};

// Widget types define shared geometry; dynamic roles select appearance in QSS.
inline void button(QPushButton* widget, const char* role = "secondary", bool dashboard = false) {
    widget->setProperty("role", role);
    widget->setProperty("controlSize", dashboard ? "dashboard" : "standard");
    widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    widget->setAutoDefault(false);
    widget->setCursor(Qt::PointingHandCursor);
}

inline QLabel* label(const QString& text, QWidget* parent, const char* role = "field") {
    auto* widget = new QLabel(text, parent);
    widget->setProperty("role", role);
    widget->setTextFormat(Qt::PlainText);
    widget->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    if (QString::fromLatin1(role) == "status" && text.isEmpty()) widget->hide();
    return widget;
}

inline void message(QLabel* widget, const QString& text) {
    widget->setText(text);
    widget->setVisible(!text.isEmpty());
}

inline QLabel* field_label(const QString& text, QWidget* parent) {
    auto* widget = label(text, parent);
    widget->setFixedWidth(field_label_width);
    widget->setWordWrap(true);
    return widget;
}

inline QWidget* page_content(QWidget* page, int maximum_width = 760, bool panel = true) {
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    auto* scroll = new QScrollArea(page);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setObjectName("page_scroll");
    auto* canvas = new QWidget;
    canvas->setObjectName("page_canvas");
    auto* root = new QVBoxLayout(canvas);
    root->setSizeConstraint(QLayout::SetMinAndMaxSize);
    root->setContentsMargins(28, 24, 28, 24);
    auto* row = new QHBoxLayout;
    auto* content = new QFrame(canvas);
    content->setObjectName(panel ? "page_card" : "workspace_content");
    content->setMaximumWidth(maximum_width);
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Maximum);
    row->addStretch();
    row->addWidget(content, 1);
    row->addStretch();
    root->addLayout(row);
    root->addStretch(1);
    scroll->setWidget(canvas);
    outer->addWidget(scroll);
    return content;
}
} // namespace App::Views::UiStyle
