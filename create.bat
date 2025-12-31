:: 根目录
mkdir src qml resources data python

:: src 下的结构
mkdir src\foundation src\domain src\engine src\ui src\app

:: foundation
mkdir src\foundation\json
mkdir src\foundation\yaml
mkdir src\foundation\net
mkdir src\foundation\public

:: domain（量化核心）
mkdir src\domain\indicators
mkdir src\domain\strategies
mkdir src\domain\signals

:: engine
mkdir src\engine

:: ui
mkdir src\ui\controllers
mkdir src\ui\bridge

:: app
mkdir src\app

:: python 工程
mkdir python\astock_engine
mkdir python\astock_engine\indicators
mkdir python\astock_engine\strategies
mkdir python\astock_engine\services

:: 资源与数据
mkdir resources\icons
mkdir data\config
type nul > src\domain\strategies\IStrategy.h
type nul > src\engine\PythonEngineBridge.h
type nul > python\astock_engine\__init__.py
type nul > python\astock_engine\services\__init__.py