#include "PythonEventBridge.h"
#include "foundation/log/logging.hpp"

#define PY_SSIZE_T_CLEAN
#include <Python.h>

#ifdef _WIN32
#include <windows.h>
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
    return ".";
#endif
}

PythonEventBridge& PythonEventBridge::instance() {
    static PythonEventBridge s;
    return s;
}

PythonEventBridge::~PythonEventBridge() { stop(); }

bool PythonEventBridge::start() {
    if (m_running.load()) return true;

    const char* pyHome = std::getenv("PYTHONHOME");
    std::string homePath = pyHome ? std::string(pyHome) : "";
    if (homePath.empty()) {
        homePath = PYTHON_EXECUTABLE;
        auto pos = homePath.rfind('\\');
        if (pos == std::string::npos) pos = homePath.rfind('/');
        if (pos != std::string::npos) homePath = homePath.substr(0, pos);
    }
    std::wstring whome(homePath.begin(), homePath.end());
    Py_SetPythonHome(whome.c_str());

    // 启动前禁用不需要的模块，避免 import readline 等崩溃
    PyImport_AppendInittab("readline", nullptr);

    // UTF-8: 解决Windows下中文乱码
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
    Py_Initialize();
    if (!Py_IsInitialized()) {
        INTERNAL_ERROR_STREAM << "[PyBridge] Py_Initialize 失败";
        return false;
    }

    // 释放GIL，允许子线程运行Python
    PyEval_InitThreads();
    m_gilState = static_cast<void*>(PyEval_SaveThread());  // 主线程释放GIL

    std::string exeDir = getExeDir();
    std::string eventsPath = exeDir + "/../../../astock_engine/events";
    std::string binPath = exeDir + "/../lib/Release";

    INTERNAL_INFO_STREAM << "[PyBridge] Python " << Py_GetVersion()
                         << " home=" << homePath << " exeDir=" << exeDir;

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
        PyEval_RestoreThread(static_cast<PyThreadState*>(m_gilState));
        Py_FinalizeEx();
        INTERNAL_INFO_STREAM << "[PyBridge] Python卸载";
    }
}

void PythonEventBridge::schedulerThread(std::string eventsPath, std::string binPath) {
    PyGILState_STATE gstate = PyGILState_Ensure();

    std::string script =
        "import sys, io\n"
        "sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding='utf-8')\n"
        "sys.stderr = io.TextIOWrapper(sys.stderr.buffer, encoding='utf-8')\n"
        "sys.path.insert(0, r'" + eventsPath + "/..')\n"
        "sys.path.insert(0, r'" + binPath + "')\n"
        "import os\n"
        "os.chdir(r'" + eventsPath + "')\n"
        "try:\n"
        "    from astock_engine.events.scheduler import main\n"
        "    main()\n"
        "except Exception:\n"
        "    import traceback\n"
        "    traceback.print_exc()\n";

    INTERNAL_INFO_STREAM << "[PyBridge] scheduler start";
    int ret = PyRun_SimpleString(script.c_str());
    if (ret != 0) {
        INTERNAL_ERROR_STREAM << "[PyBridge] exit code=" << ret;
        PyErr_Print();
    }
    PyGILState_Release(gstate);
    INTERNAL_INFO_STREAM << "[PyBridge] scheduler exit";
}

} // namespace app
