import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

TableView {
	id: tradeTable

	property alias model: tableView.model

	clip: true

	TableViewColumn { role: "time";   title: "时间";   width: 120 }
	TableViewColumn { role: "symbol"; title: "标的";   width: 80  }
	TableViewColumn { role: "price";  title: "价格";   width: 80  }
	TableViewColumn { role: "volume"; title: "数量";   width: 80  }
	TableViewColumn { role: "side";   title: "方向";   width: 60  }

	Component {
		id: tableView
		TableView {}
	}
}

