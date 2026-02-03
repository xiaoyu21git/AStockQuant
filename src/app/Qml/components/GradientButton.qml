import QtQuick 2.15
import QtQuick.Controls 2.15

Button {
	id: control
	// 自定义属性
	property string iconText: ""
	property string buttonText: "执行策略"
	property bool showIcon: iconText !== ""

	// 尺寸属性
	implicitWidth: 120
	implicitHeight: 40

	// 文本内容
	text: showIcon ? iconText + " " + buttonText : buttonText

	// 字体样式
	font {
		family: "Inter, Noto Sans SC"
		pixelSize: 14
		weight: Font.DemiBold
		letterSpacing: 0.5
	}

	// 背景渐变
	background: Rectangle {
		id: bgRect
		radius: 8
		gradient: Gradient {
			GradientStop { position: 0.0; color: "#3B82F6" }
			GradientStop { position: 1.0; color: "#1D4ED8" }
		}
		// 阴影效果
		layer.enabled: true
		layer.effect: DropShadow {
			transparentBorder: true
			color: "#3B82F680"  // 半透明蓝色
			radius: 8
			samples: 17
			horizontalOffset: 0
			verticalOffset: 2
		}
		// 高光层
		Rectangle {
			anchors.fill: parent
			radius: parent.radius
			gradient: Gradient {
				GradientStop { position: 0.0; color: "#FFFFFF10" }
				GradientStop { position: 0.5; color: "transparent" }
			}
			opacity: control.hovered ? 1 : 0
			Behavior on opacity { NumberAnimation { duration: 200 } }
		}
	}

	// 内容项
	contentItem: Text {
		text: control.text
		font: control.font
		color: "white"
		horizontalAlignment: Text.AlignHCenter
		verticalAlignment: Text.AlignVCenter
		elide: Text.ElideRight
	}

	// 悬停效果
	onHoveredChanged: {
		if (hovered && enabled) {
			bgRect.scale = 1.02
			bgRect.layer.effect.verticalOffset = 4
			bgRect.layer.effect.radius = 12
		} else {
			bgRect.scale = 1.0
			bgRect.layer.effect.verticalOffset = 2
			bgRect.layer.effect.radius = 8
		}
	}

	// 点击效果
	onPressedChanged: {
		if (pressed) {
			bgRect.scale = 0.98
			bgRect.layer.effect.verticalOffset = 0
		} else if (hovered) {
			bgRect.scale = 1.02
			bgRect.layer.effect.verticalOffset = 4
		} else {
			bgRect.scale = 1.0
			bgRect.layer.effect.verticalOffset = 2
		}
	}

	// 禁用状态
	onEnabledChanged: {
		if (!enabled) {
			bgRect.opacity = 0.4
			bgRect.layer.effect.opacity = 0.4
		} else {
			bgRect.opacity = 1.0
			bgRect.layer.effect.opacity = 1.0
		}
	}
}
