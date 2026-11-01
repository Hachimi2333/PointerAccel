#include "device_model.hpp"
namespace pa
{
int DeviceModel::rowCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : rows.size();
}
int DeviceModel::columnCount(const QModelIndex& parent) const
{
	return parent.isValid() ? 0 : ColumnCount;
}
QVariant DeviceModel::data(const QModelIndex& index, int role) const
{
	if (!index.isValid() || index.row() >= rows.size())
		return {};
	const auto& d = rows[index.row()];
	if (role == Qt::ToolTipRole)
		return d.name + "\n" + d.id;
	if (role == Qt::CheckStateRole && index.column() == Excluded)
		return d.excluded ? Qt::Checked : Qt::Unchecked;
	if (role != Qt::DisplayRole)
		return {};
	switch (index.column())
	{
	case Name:
		return d.name;
	case State:
		return !d.connected    ? "未连接 · 保留排除记录"
		       : !d.eligible   ? "不适用 · 透传"
		       : d.accelerated ? "PointerAccel"
		                       : "系统原生处理";
	case Id:
		return d.id;
	default:
		return {};
	}
}
QVariant DeviceModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
		return {};
	return QStringList{"设备", "当前处理", "排除", "设备 ID"}.value(section);
}
Qt::ItemFlags DeviceModel::flags(const QModelIndex& index) const
{
	if (!index.isValid() || index.row() >= rows.size())
		return Qt::NoItemFlags;
	auto result = Qt::ItemIsEnabled | Qt::ItemIsSelectable;
	if (index.column() == Excluded && editable && rows[index.row()].eligible)
		result |= Qt::ItemIsUserCheckable;
	else if (index.column() == Excluded)
		result &= ~Qt::ItemIsEnabled;
	return result;
}
bool DeviceModel::setData(const QModelIndex& index, const QVariant& value, int role)
{
	if (role != Qt::CheckStateRole || !(flags(index) & Qt::ItemIsUserCheckable))
		return false;
	emit exclusionChanged(rows[index.row()].id, value.toInt() == Qt::Checked);
	return true;
}
void DeviceModel::update(const QList<DeviceInfo>& devices, bool canEdit)
{
	if (rows == devices && editable == canEdit)
		return;
	beginResetModel();
	rows = devices;
	editable = canEdit;
	endResetModel();
}
}
