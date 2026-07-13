#include "PythonEventBridge.h"
#include "foundation/log/logging.hpp"

// Python C API
#define PY_SSIZE_T_CLEAN
#include <Python.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <libgen.h>
#endif

namespace app {

static std::string getExeDir() {
#ifdef _WIN32
    char buf[MAX_PATH];
    DWORD len = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (len == 0 || len >= sizeof(buf)) return ".";
    std::string path(buf, len);
    auto pos = path.rfind('\\');
    return (pos != std::string::npos) ? path.substr(0, pos) : ".";
#else
    char buf[4096];
    ssize_t len = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len <= 0) return ".";
    buf[len] = '\0';
    std::string path(buf);
    auto pos = path.rfind('/');
    return (pos != std::string::npos) ? path.substr(0, pos) : ".";
#endif
}

PythonEventBridge& PythonEventBridge::instance() {
    static PythonEventBridge s;
    return s;
}

PythonEventBridge::~PythonEventBridge() { stop(); }

bool PythonEventBridge::start() {
    if (m_running.load()) return true;

    // Python Home: 优先环境变量, 否则用编译时的 Python 路径
    const char* pyHome = std::getenv("PYTHONHOME");
    std::string homePath;
    if (pyHome && pyHome[0]) {
        homePath = pyHome;
    } else {
        // 回退: 去掉 "python.exe" 后缀取目录
        homePath = PYTHON_EXECUTABLE;
        auto pos = homePath.rfind('\\');
        if (pos == std::string::npos) pos = homePath.rfind('/');
        if (pos != std::string::npos) homePath = homePath.substr(0, pos);
    }
    std::wstring whome(homePath.begin(), homePath.end());
    Py_SetPythonHome(whome.c_str());

    Py_Initialize();
    if (!Py_IsInitialized()) {
        INTERNAL_ERROR_STREAM << "[PyBridge] Py_Initialize 失败";
        return false;
    }

    // 路径计算: 从可执行文件位置推导
    std::string exeDir = getExeDir();
    // eventsPath: ../../../astock_engine/events (from bin/Release to project root)
    std::string eventsPath = exeDir + "/../../../astock_engine/events";
    std::string binPath = exeDir + "/../lib/Release";  // eventbus_native.dll

    INTERNAL_INFO_STREAM << "[PyBridge] Python " << Py_GetVersion()
                         << " home=" << homePath
                         << " exeDir=" << exeDir;

    m_running.store(true);
    m_thread = std::make_unique<std::thread>(schedulerThread, eventsPath, binPath);
    return true;
}

void PythonEventBridge::stop() {
    m_running.store(false);
    if (m_thread && m_thread->joinable()) {
        m_thread->join();
        m_thread.reset();
    }
    if (Py_IsInitialized()) {
        Py_FinalizeEx();
        INTERNAL_INFO_STREAM << "[PyBridge] Python已卸载";
    }
}

void PythonEventBridge::schedulerThread(const std::string& eventsPath,
                                         const std::string& binPath) {
    std::string script =
        "import sys\n"
        "sys.path.insert(0, r'" + eventsPath + "/..')\n"
        "sys.path.insert(0, r'" + binPath + "')\n"
        "import os\n"
        "os.chdir(r'" + eventsPath + "')\n"
        "try:\n"
        "    from astock_engine.events.scheduler import main\n"
        "    main()\n"
        "except Exception as e:\n"
        "    import traceback; traceback.print_exc()\n";

    INTERNAL_INFO_STREAM << "[PyBridge] 调度器线程启动";
    int ret = PyRun_SimpleString(script.c_str());
    if (ret != 0) {
        INTERNAL_ERROR_STREAM << "[PyBridge] 调度器异常退出 code=" << ret;
        if (PyErr_Occurred()) PyErr_Print();
    }
    INTERNAL_INFO_STREAM << "[PyBridge] 调度器线程退出";
}

} // namespace app
