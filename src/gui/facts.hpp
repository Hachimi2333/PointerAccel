#pragma once
#include <QWidget>
#include <QJsonValue>
#include <QHash>
class QFormLayout;
class QLabel;
namespace pa
{
QString displayText(const QJsonValue& value);
QString yesNo(const QJsonValue& value);
QString stateName(const QJsonValue& value);
class Facts : public QWidget
{
public:
	explicit Facts(QWidget* parent = nullptr);
	void add(const QString& key, const QString& label);
	void set(const QString& key, const QString& text);

private:
	QFormLayout* layout;
	QHash<QString, QLabel*> values;
};
}
