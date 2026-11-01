#pragma once
#include "state.hpp"
#include <QAbstractTableModel>
namespace pa
{
class DeviceModel : public QAbstractTableModel
{
	Q_OBJECT
public:
	enum Column
	{
		Name,
		State,
		Excluded,
		Id,
		ColumnCount
	};
	explicit DeviceModel(QObject* parent = nullptr) : QAbstractTableModel(parent)
	{
	}
	int rowCount(const QModelIndex& parent = {}) const override;
	int columnCount(const QModelIndex& parent = {}) const override;
	QVariant data(const QModelIndex& index, int role) const override;
	QVariant headerData(int section, Qt::Orientation orientation, int role) const override;
	Qt::ItemFlags flags(const QModelIndex& index) const override;
	bool setData(const QModelIndex& index, const QVariant& value, int role) override;
	void update(const QList<DeviceInfo>& devices, bool editable);
signals:
	void exclusionChanged(const QString& id, bool excluded);

private:
	QList<DeviceInfo> rows;
	bool editable = false;
};
}
