pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import QtQuick.Window

ApplicationWindow {
	id: window
	required property var controller
	objectName: "mainWindow"
	visible: true
	width: 840
	height: 700
	minimumWidth: 600
	minimumHeight: 500
	title: "PointerAccel" + (window.controller.dirty ? " *" : "")
	property bool closeAfterApply: false
	readonly property bool editable: window.controller.valid && !window.controller.applying
	readonly property var sample: window.controller.diagnostics.last || ({})

	function yesNo(value) {
		return value === undefined ? "—" : value ? "是" : "否"
	}
	function text(value) {
		return value === undefined || value === null ? "—" : String(value)
	}
	function stateName(value) {
		const labels = {
			"active": "正在生效",
			"idle": "已启用，等待鼠标输入",
			"disabled": "已停用",
			"error": "运行故障",
			"ready": "就绪",
			"not-installed": "未安装",
			"pending-login": "需要重新登录",
			"repair-required": "需要修复",
			"unsupported": "KWin 版本尚未支持",
			"blocked": "已阻止加载",
			"pending-kwin": "等待 Plasma 会话",
			"load-failed": "模块加载失败"
		}
		return labels[value] || text(value)
	}
	onVisibilityChanged: function (visibility) {
		window.controller.setPollingActive(window.visible && visibility !== Window.Minimized)
	}
	onClosing: function (event) {
		if (window.controller.dirty) {
			event.accepted = false
			if (!window.controller.applying)
				closeDialog.open()
		}
	}
	Connections {
		target: window.controller
		function onApplyFinished(saved, applied) {
			if (window.closeAfterApply) {
				window.closeAfterApply = false
				if (saved && applied)
					Qt.callLater(function () {
						window.close()
					})
			}
		}
	}
	component Fact: RowLayout {
		property string label
		property string value
		Layout.fillWidth: true
		spacing: 16
		Label {
			text: parent.label
			Layout.preferredWidth: 145
			color: window.palette.placeholderText
		}
		Label {
			text: parent.value
			Layout.fillWidth: true
			wrapMode: Text.Wrap
			textFormat: Text.PlainText
		}
	}
	component SectionTitle: Label {
		font.bold: true
		font.pixelSize: 18
		Layout.topMargin: 10
	}
	component SelectableText: TextArea {
		readOnly: true
		selectByMouse: true
		wrapMode: TextEdit.Wrap
		textFormat: TextEdit.PlainText
		Layout.fillWidth: true
		background: null
		padding: 0
	}

	header: ColumnLayout {
		spacing: 0
		RowLayout {
			Layout.fillWidth: true
			Layout.margins: 16
			ColumnLayout {
				spacing: 4
				Label {
					text: "PointerAccel"
					font.pixelSize: 24
					font.bold: true
				}
				Label {
					text: "Windows 指针算法 · 查看与配置"
					color: window.palette.placeholderText
				}
			}
			Item {
				Layout.fillWidth: true
			}
			BusyIndicator {
				running: window.controller.busy
				visible: running
				Layout.preferredWidth: 28
				Layout.preferredHeight: 28
			}
			Button {
				text: "刷新"
				enabled: !window.controller.busy
				onClicked: window.controller.refresh()
				objectName: "refreshButton"
			}
		}
		TabBar {
			id: tabs
			Layout.fillWidth: true
			objectName: "pageTabs"
			currentIndex: window.controller.page
			onCurrentIndexChanged: window.controller.page = currentIndex
			TabButton {
				text: "设置"
				objectName: "settingsTab"
			}
			TabButton {
				text: "设备"
				objectName: "devicesTab"
			}
			TabButton {
				text: "诊断"
				objectName: "diagnosticsTab"
			}
		}
	}

	ColumnLayout {
		anchors.fill: parent
		spacing: 0
		Frame {
			Layout.fillWidth: true
			Layout.margins: 16
			visible: window.controller.configError.length > 0 || window.controller.message.length > 0
					 || window.controller.conflict

			ColumnLayout {
				width: parent.width
				Label {
					Layout.fillWidth: true
					text: window.controller.configError.length > 0 ? "配置无效，已禁止保存：" + window.controller.configError
																	 + "\n" + window.controller.configFile :
																	 window.controller.conflict
																	 ? "配置已被外部修改，草稿已保留。请撤销修改以重新加载。" :
																	   window.controller.message
					wrapMode: Text.Wrap
					textFormat: Text.PlainText
					objectName: "messageLabel"
				}
				Button {
					visible: window.controller.configError.length > 0
					text: "复制配置路径"
					onClicked: window.controller.copyText(window.controller.configFile)
				}
			}
		}
		StackLayout {
			Layout.fillWidth: true
			Layout.fillHeight: true
			currentIndex: tabs.currentIndex

			ScrollView {
				clip: true
				contentWidth: availableWidth
				ColumnLayout {
					width: parent.width
					spacing: 14
					// Keep margins inside the scrollable content on small windows.
					Item {
						Layout.preferredHeight: 2
					}
					ColumnLayout {
						Layout.fillWidth: true
						Layout.leftMargin: 24
						Layout.rightMargin: 24
						spacing: 14
						SectionTitle {
							text: "指针设置"
						}
						Switch {
							text: "启用 PointerAccel"
							checked: window.controller.draft.enabled || false
							enabled: window.editable
							onClicked: window.controller.setValue("enabled", checked)
							objectName: "enabledSwitch"
						}
						Label {
							text: "指针速度"
							font.bold: true
						}
						RowLayout {
							Layout.fillWidth: true
							Label {
								text: "慢"
							}
							Slider {
								Layout.fillWidth: true
								from: 1
								to: 20
								stepSize: 1
								snapMode: Slider.SnapAlways
								value: window.controller.draft.pointer_speed || 10
								enabled: window.editable
								onMoved: window.controller.setValue("pointer_speed", Math.round(value))
								Accessible.name: "指针速度"
								objectName: "speedSlider"
							}
							Label {
								text: "快"
							}
							SpinBox {
								from: 1
								to: 20
								editable: true
								value: window.controller.draft.pointer_speed || 10
								enabled: window.editable
								onValueModified: window.controller.setValue("pointer_speed", value)
								Accessible.name: "指针速度档位"
								objectName: "speedSpinBox"
							}
							Label {
								text: "/ 20"
							}
						}
						CheckBox {
							text: "提高指针精确度（EPP）"
							checked: window.controller.draft.enhance_pointer_precision || false
							enabled: window.editable
							onClicked: window.controller.setValue("enhance_pointer_precision", checked)
							objectName: "eppCheckBox"
						}
						Label {
							Layout.fillWidth: true
							text: "以上为待应用设置。点击“应用”后统一保存，并清除 CLI 设置的临时启停覆盖。"
							wrapMode: Text.Wrap
							color: window.palette.placeholderText
						}
						ToolSeparator {
							orientation: Qt.Horizontal
							Layout.fillWidth: true
						}
						SectionTitle {
							text: "当前运行状态"
						}
						Fact {
							label: "运行状态"
							value: window.stateName(window.controller.status.state)
						}
						Fact {
							label: "保存的启用设置"
							value: window.yesNo(window.controller.status.configured_enabled)
						}
						Fact {
							label: "模块已加载"
							value: window.yesNo(window.controller.status.loaded)
						}
						Fact {
							label: "实际生效"
							value: window.yesNo(window.controller.status.active)
						}
						Fact {
							label: "CLI 临时覆盖"
							value: window.yesNo(window.controller.status.temporary_override)
						}
						Fact {
							label: "健康状态"
							value: window.controller.status.healthy === undefined ? "正在检查…" : window.controller.status.healthy
																					? "正常" : "需要处理"
						}
						Fact {
							label: "下次启动"
							value: window.stateName(window.controller.status.startup_state)
						}
						Label {
							Layout.fillWidth: true
							visible: !!window.controller.status.error
							text: window.controller.status.error || ""
							wrapMode: Text.Wrap
							textFormat: Text.PlainText
						}
						Label {
							text: "关闭面板后，算法和会话检查服务继续独立运行。"
							Layout.fillWidth: true
							wrapMode: Text.Wrap
							color: window.palette.placeholderText
						}
						Item {
							Layout.preferredHeight: 20
						}
					}
				}
			}

			ScrollView {
				clip: true
				contentWidth: availableWidth
				ColumnLayout {
					width: parent.width
					spacing: 14
					Label {
						Layout.fillWidth: true
						Layout.margins: 20
						text: "排除的设备使用系统原生处理。更改需点击底部“应用”。"
						wrapMode: Text.Wrap
					}
					Label {
						visible: !!window.controller.status.devices_error
						text: window.controller.status.devices_error || ""
						textFormat: Text.PlainText
						wrapMode: Text.Wrap
						Layout.fillWidth: true
						Layout.leftMargin: 20
						Layout.rightMargin: 20
					}
					Repeater {
						model: window.controller.devices
						delegate: Frame {
							id: deviceCard
							required property var modelData
							Layout.fillWidth: true
							Layout.leftMargin: 20
							Layout.rightMargin: 20
							ColumnLayout {
								width: parent.width
								spacing: 8
								Label {
									text: deviceCard.modelData.name
									font.bold: true
									Layout.fillWidth: true
									wrapMode: Text.Wrap
									textFormat: Text.PlainText
								}
								SelectableText {
									text: deviceCard.modelData.id
									font.family: "monospace"
								}
								Label {
									Layout.fillWidth: true
									wrapMode: Text.Wrap
									text: !deviceCard.modelData.connected ? "未连接 · 保留排除记录" :
																			!deviceCard.modelData.eligible ? "不适用 · 透传" :
																											 deviceCard.modelData.accelerated
																											 ? "当前由 PointerAccel 处理" :
																											   "当前使用系统原生处理"
								}
								CheckBox {
									text: "排除此设备"
									checked: deviceCard.modelData.draft_excluded
									enabled: window.editable && deviceCard.modelData.eligible
									onClicked: window.controller.setExcluded(deviceCard.modelData.id, checked)
									Accessible.name: "排除 " + deviceCard.modelData.name
								}
							}
						}
					}
					Label {
						visible: window.controller.devices.length === 0
						text: "暂无可显示的设备。"
						Layout.margins: 20
					}
					Item {
						Layout.preferredHeight: 12
					}
				}
			}

			ScrollView {
				clip: true
				contentWidth: availableWidth
				ColumnLayout {
					width: parent.width
					ColumnLayout {
						Layout.fillWidth: true
						Layout.margins: 24
						spacing: 12
						SectionTitle {
							text: "健康与兼容性"
						}
						Fact {
							label: "运行模块兼容"
							value: window.yesNo(window.controller.status.running_compatible)
						}
						Fact {
							label: "下次启动兼容"
							value: window.yesNo(window.controller.status.startup_compatible)
						}
						Fact {
							label: "需要修复"
							value: window.yesNo(window.controller.status.repair_required)
						}
						Fact {
							label: "需要重新登录"
							value: window.yesNo(window.controller.status.relogin_required)
						}
						Fact {
							label: "已安装 KWin"
							value: window.text(window.controller.status.installed_kwin_version)
						}
						Label {
							text: window.controller.status.error || ""
							visible: text.length > 0
							Layout.fillWidth: true
							wrapMode: Text.Wrap
							textFormat: Text.PlainText
						}
						SectionTitle {
							text: "输入诊断"
						}
						Fact {
							label: "已处理报告"
							value: window.text(window.controller.diagnostics.processed)
						}
						Fact {
							label: "透传报告"
							value: window.text(window.controller.diagnostics.passthrough)
						}
						Fact {
							label: "输入批次"
							value: window.text(window.controller.diagnostics.batches)
						}
						Fact {
							label: "最近样本距今"
							value: window.controller.diagnostics.last_age_ms >= 0
								   ? window.controller.diagnostics.last_age_ms + " ms" : "尚无输入样本"
						}
						Fact {
							label: "样本设备"
							value: window.text(window.sample.device)
						}
						Fact {
							label: "原始位移"
							value: window.sample.raw ? window.sample.raw.join(", ") : "—"
						}
						Fact {
							label: "处理后位移"
							value: window.sample.output ? window.sample.output.join(", ") : "—"
						}
						Fact {
							label: "屏幕缩放 / DPI"
							value: window.sample.scale ? window.sample.scale + " / " + window.sample.dpi : "—"
						}
						Label {
							text: window.controller.diagnostics.error || ""
							visible: text.length > 0
							Layout.fillWidth: true
							wrapMode: Text.Wrap
							textFormat: Text.PlainText
						}
						CheckBox {
							text: "记录逐报告 trace（待应用设置）"
							checked: window.controller.draft.trace || false
							enabled: window.editable
							onClicked: window.controller.setValue("trace", checked)
							objectName: "traceCheckBox"
						}
						Fact {
							label: "实际 trace 状态"
							value: window.controller.diagnostics.trace_enabled === undefined ? "—" :
																							   window.controller.diagnostics.trace_enabled
																							   ? "记录中" : "已关闭"
						}
						Fact {
							label: "trace 样本 / 容量"
							value: window.text(window.controller.diagnostics.trace_samples) + " / " + window.text(
									   window.controller.diagnostics.trace_capacity)
						}
						Label {
							text: "trace 仅保存在模块内存，最多 4096 份；关闭记录后清空。关闭 GUI 不会自动关闭记录。"
							Layout.fillWidth: true
							wrapMode: Text.Wrap
							color: window.palette.placeholderText
						}
						Flow {
							Layout.fillWidth: true
							spacing: 10
							Button {
								text: "导出诊断 JSON…"
								enabled: !window.controller.busy
								onClicked: exportDialog.open()
								objectName: "exportButton"
							}
							Button {
								text: "重置算法状态"
								enabled: !window.controller.busy && !!window.controller.status.loaded
								onClicked: window.controller.resetState()
								objectName: "resetButton"
							}
						}
						SectionTitle {
							text: "终端维护"
						}
						SelectableText {
							text: "pointeraccel doctor\npointeraccel repair"
							font.family: "monospace"
						}
						RowLayout {
							Button {
								text: "复制诊断命令"
								onClicked: window.controller.copyText("pointeraccel doctor")
							}
							Button {
								text: "复制修复命令"
								onClicked: window.controller.copyText("pointeraccel repair")
							}
						}
						Label {
							text: "修复在终端执行；修复完成后手动重新登录 Plasma。"
							Layout.fillWidth: true
							wrapMode: Text.Wrap
							color: window.palette.placeholderText
						}
					}
				}
			}
		}
	}

	footer: ToolBar {
		implicitHeight: footerRow.implicitHeight + 20
		RowLayout {
			id: footerRow
			anchors.fill: parent
			anchors.margins: 10
			Label {
				Layout.fillWidth: true
				wrapMode: Text.Wrap
				text: window.controller.conflict ? "外部配置已变化" : window.controller.dirty ? "有未应用的修改" : "草稿与保存配置一致"
				objectName: "draftStateLabel"
			}
			Button {
				text: "撤销修改"
				enabled: !window.controller.busy && (window.controller.dirty || window.controller.conflict)
				onClicked: window.controller.revert()
				objectName: "revertButton"
			}
			Button {
				text: "应用"
				highlighted: true
				enabled: window.editable && !window.controller.busy && window.controller.dirty &&
						 !window.controller.conflict
				onClicked: window.controller.apply()
				objectName: "applyButton"
			}
		}
	}
	Dialog {
		id: closeDialog
		objectName: "closeDialog"
		title: "有未保存的修改"
		anchors.centerIn: parent
		modal: true
		closePolicy: Popup.CloseOnEscape
		ColumnLayout {
			Label {
				text: "将修改应用后关闭，或放弃本次草稿。"
			}
			RowLayout {
				Button {
					text: "应用并关闭"
					enabled: window.editable && !window.controller.busy && !window.controller.conflict
					onClicked: {
						closeDialog.close()
						window.closeAfterApply = true
						window.controller.apply()
					}
					objectName: "applyCloseButton"
				}
				Button {
					text: "放弃修改"
					onClicked: Qt.quit()
					objectName: "discardCloseButton"
				}
				Button {
					text: "取消"
					onClicked: closeDialog.close()
					objectName: "cancelCloseButton"
				}
			}
		}
	}
	FileDialog {
		id: exportDialog
		title: "导出 PointerAccel 诊断"
		fileMode: FileDialog.SaveFile
		nameFilters: ["JSON 文件 (*.json)"]
		defaultSuffix: "json"
		onAccepted: window.controller.exportDiagnostics(selectedFile)
	}
}
