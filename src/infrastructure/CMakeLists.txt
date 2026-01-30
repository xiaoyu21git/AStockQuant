# CMakeLists.txt
cmake_minimum_required(VERSION 3.15)












































































































Write-Host "  3. Run: python -c 'from astock_engine.data import database_wrapper; print(database_wrapper)'"Write-Host "  2. Set DB_PASSWORD environment variable"Write-Host "  1. If MySQL is not installed, install MySQL Server 8.0"Write-Host "`nNext steps:" -ForegroundColor Cyan}    Write-Host "⚠ NOT BUILT (MySQL required)" -ForegroundColor Yellow} else {    Write-Host "✓ SUCCESS" -ForegroundColor Greenif (Test-Path "astock_engine\database_native.pyd") {Write-Host "database_native module: " -NoNewline}    Write-Host "✗ FAILED" -ForegroundColor Red} else {    Write-Host "✓ SUCCESS" -ForegroundColor Greenif (Test-Path "build\src\infrastructure\Debug\infrastructure.lib") {Write-Host "Infrastructure library: " -NoNewlineWrite-Host "`n=== Build Summary ===" -ForegroundColor CyanSet-Location ..}    Write-Host "  ⚠ database_native.pyd not found (MySQL may not be available)" -ForegroundColor Yellow} else {    }        Write-Host "  ✓ Copied to: $destDir" -ForegroundColor Green        Copy-Item $pydFile.FullName -Destination $destDir -Force    if (Test-Path $destDir) {    $destDir = "..\astock_engine"    # 复制到Python包目录        Write-Host "  ✓ Found: $($pydFile.FullName)" -ForegroundColor Greenif ($pydFile) {$pydFile = Get-ChildItem -Path . -Filter "database_native.pyd" -Recurse -ErrorAction SilentlyContinue | Select-Object -First 1Write-Host "`n[5/5] Locating database_native.pyd..." -ForegroundColor Yellow# 5. 查找生成的.pyd文件Write-Host "  ✓ database_native module built" -ForegroundColor Green}    exit 0  # 不强制失败    Set-Location ..    Write-Host "  Note: This may be expected if MySQL is not properly configured" -ForegroundColor Yellow    Write-Host "  ✗ database_native build failed" -ForegroundColor Redif ($LASTEXITCODE -ne 0) {cmake --build . --target database_native --config DebugWrite-Host "`n[4/5] Building database_native Python module..." -ForegroundColor Yellow# 4. 编译database_native模块Write-Host "  ✓ Infrastructure library built" -ForegroundColor Green}    exit 1    Set-Location ..    Write-Host "  ✗ Infrastructure build failed" -ForegroundColor Redif ($LASTEXITCODE -ne 0) {cmake --build . --target infrastructure --config DebugWrite-Host "`n[3/5] Building infrastructure library..." -ForegroundColor Yellow# 3. 编译infrastructure库Write-Host "  ✓ CMake configuration successful" -ForegroundColor Green}    exit 1    Set-Location ..    Write-Host "  ✗ CMake configuration failed" -ForegroundColor Redif ($LASTEXITCODE -ne 0) {cmake .. -DBUILD_PYTHON_EXTENSIONS=ON -G "Visual Studio 16 2019" -A x64Set-Location build}    New-Item -ItemType Directory -Path "build" | Out-Nullif (-not (Test-Path "build")) {Write-Host "`n[2/5] Configuring CMake..." -ForegroundColor Yellow# 2. 重新配置CMake}    exit 1    Write-Host "  Download from: https://dev.mysql.com/downloads/" -ForegroundColor Yellow    Write-Host "  ✗ MySQL not found. Please install MySQL Server 8.0 or MySQL Connector C++" -ForegroundColor Redif (-not $mysqlFound) {}    }        break        $mysqlFound = $true        Write-Host "  ✓ Found MySQL: $path" -ForegroundColor Green    if (Test-Path $path) {foreach ($path in $mysqlPaths) {$mysqlFound = $false)    "C:\Program Files\MySQL\MySQL Connector C++ 8.0\lib64\mysqlclient.lib"    "C:\Program Files\MySQL\MySQL Server 8.0\lib\libmysql.dll",$mysqlPaths = @(Write-Host "`n[1/5] Checking MySQL installation..." -ForegroundColor Yellow# 1. 检查MySQL安装Write-Host "=== AStock Database Module Build ===" -ForegroundColor Cyanproject(infrastructure LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if (WIN32)
    set(OPENSSL_ROOT_DIR "C:/libs/OpenSSL")
    set(OPENSSL_USE_STATIC_LIBS ON)
endif()
find_package(OpenSSL REQUIRED)

# 查找MySQL库
find_library(MYSQL_LIB
    NAMES libmysql mysqlclient
    PATHS
        "C:/Program Files/MySQL/MySQL Server 8.0/lib"
        "C:/Program Files/MySQL/MySQL Connector C++ 8.0/lib64"
        "/usr/lib/x86_64-linux-gnu"
        "/usr/local/lib"
)

find_path(MYSQL_INCLUDE_DIR
    NAMES mysql.h mysql/mysql.h
    PATHS
        "C:/Program Files/MySQL/MySQL Server 8.0/include"
        "C:/Program Files/MySQL/MySQL Connector C++ 8.0/include"
        "/usr/include/mysql"
        "/usr/local/include/mysql"
)

if(MYSQL_LIB AND MYSQL_INCLUDE_DIR)
    message(STATUS "Found MySQL: ${MYSQL_LIB}")
    message(STATUS "MySQL include: ${MYSQL_INCLUDE_DIR}")
else()
    message(WARNING "MySQL not found. Database features will be disabled.")
endif()

# 添加源码
add_library(infrastructure STATIC
    src/database/ConnectionPool.cpp
    src/database/MarketDataRepository.cpp
#    Cache/CacheManager.cpp
#    Cache/MemoryCache.cpp
#    Cache/RedisCache.cpp
#    database/DatabaseManager.cpp
#    database/IDatabaseConnection.cpp
#    database/MySQLRepository.cpp
#    database/QueryBuilder.cpp
#    database/SQLiteRepository.cpp
#    database/Transaction.cpp
#    datasource/ApiDataSource.cpp
#    datasource/DatabaseSource.cpp
#    datasource/DataSourceChain.cpp
#    datasource/DataSourceContext.cpp
#    datasource/DataSourceFactory.cpp
#    datasource/FileDataSource.cpp
)
target_link_libraries(infrastructure
    PUBLIC
        foundation
        domain_model
    PRIVATE
        OpenSSL::SSL
        OpenSSL::Crypto
)

if(MYSQL_LIB)
    target_link_libraries(infrastructure PRIVATE ${MYSQL_LIB})
endif()

# 添加线程库支持
find_package(Threads REQUIRED)
target_link_libraries(infrastructure PRIVATE Threads::Threads)

target_include_directories(infrastructure
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MYSQL_INCLUDE_DIR)
    target_include_directories(infrastructure PRIVATE ${MYSQL_INCLUDE_DIR})
endif()


# 链接选项
set_target_properties(infrastructure PROPERTIES
    INTERPROCEDURAL_OPTIMIZATION TRUE
)

# 测试可执行文件
if(BUILD_TESTS)
    add_executable(infrastructure
        
    )
    
    target_link_libraries(infrastructure_test  ${openssl}  foundation domain_model astock_market gtest gtest_main )
    
    add_test(NAME infrastructure_test COMMAND infrastructure_test)
endif()