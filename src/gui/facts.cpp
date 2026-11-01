#include "facts.hpp"
#include <QFormLayout>
#include <QLabel>
namespace pa
{
QString displayText(const QJsonValue& value)
{
	if (value.isUndefined() || value.isNull())
		return "—";
	return value.toVariant().toString();
}
QString yesNo(const QJsonValue& value)
{
	return value.isUndefined() ? "—" : value.toBool() ? "是" : "否";
}
QString stateName(const QJsonValue& value)
{
	static const QHash<QString, QString> labels{{"active", "正在生效"},
	    {"idle", "已启用，等待鼠标输入"},
	    {"disabled", "已停用"},
	    {"error", "运行故障"},
	    {"ready", "就绪"},
	    {"not-installed", "未安装"},
	    {"pending-login", "需要重新登录"},
	    {"repair-required", "需要修复"},
	    {"unsupported", "KWin 版本尚未支持"},
	    {"blocked", "已阻止加载"},
	    {"pending-kwin", "等待 Plasma 会话"},
	    {"load-failed", "模块加载失败"}};
	return labels.value(value.toString(), displayText(value));
}
Facts::Facts(QWidget* parent) : QWidget(parent), layout(new QFormLayout(this))
{
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
	layout->setRowWrapPolicy(QFormLayout::WrapLongRows);
	layout->setVerticalSpacing(9);
}
void Facts::add(const QString& key, const QString& label)
{
	auto* value = new QLabel("—", this);
	value->setTextFormat(Qt::PlainText);
	value->setWordWrap(true);
	value->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
	value->setMinimumWidth(50);
	values.insert(key, value);
	layout->addRow(label + "：", value);
}
void Facts::set(const QString& key, const QString& text)
{
	values.value(key)->setText(text);
}
}
