import QtQuick 2.15
import QtQuick.Controls 2.15
import "./FactorParamItem.qml"

Column {
    id: root
    property var parametersMeta // 传入json参数定义
    property var parametersValue // 传入/输出参数对象
    spacing: 10

    Repeater {
        model: parametersMeta ? Object.keys(parametersMeta) : []
        delegate: FactorParamItem {
            paramName: modelData
            paramMeta: parametersMeta[modelData]
            paramValue: parametersValue[modelData] !== undefined ? parametersValue[modelData] : paramMeta.default
            onValueChanged: {
                parametersValue[paramName] = value
            }
        }
    }
}
